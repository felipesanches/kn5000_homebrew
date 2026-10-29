/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * main.c -- DOOM.BIN: Chocolate Doom (src/choco/) on the KN7000, run by KN7APP.BIN (src/app/) on its program task.
 *   * memory: the regions KN7APP hands over, less the program's own image and bss (doom.ld): the heap newlib's malloc
 *     grows into is the rest of the program area (GAME_BASE..GAME_END) and ARENA6; the zone (z_zone.c) everything else,
 *     one zone over all of it (I_ZoneRegions);
 *   * files: the card's DOOM\ folder, read by sectors anywhere in a file (PfHost::rawOpen, rawRead): the WAD
 *     (w_file_kn7.c) and, through newlib's stdio, what Doom opens to read (its configuration). Nothing is written;
 *   * the startup log: what Doom prints, on the LCD until the game draws there;
 *   * the end: exit() comes back here (Doom's quit, or I_Error, whose message stays on the LCD until EXIT is held). */
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "kn7doom.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"

extern char __bss_lo[], __bss_hi[], __init_lo[], __init_hi[], __game_end[], __image_end[], __bss2_lo[], __bss2_hi[];
void D_DoomMain(void);
int doom_entry(const PfHost *h);

/* ---- the header KN7APP.BIN checks (pfhost.h PfGameHeader); mkgame.py fills in the size and the CRC-32 ---- */
__attribute__((section(".header"), used)) const PfGameHeader doom_header = {
    PFGAME_MAGIC, PFHOST_ABI, 0, DOOM_PROGRAM, (u32)__bss_lo, (u32)__bss_hi, (u32)__init_lo, (u32)__init_hi,
    (u32)__game_end, doom_entry, 0, (u32)&doom_header, {0, 0, 0, 0}};

const PfHost *host;

/* ---- memory --------------------------------------------------------------------------------------------------- */
#define MAX_REGIONS 24
#define MIN_REGION 0x1000  /* smaller pieces are left unused */

static kn7_region_t zone[MAX_REGIONS], heap[MAX_REGIONS];
static int zones, heaps, heapAt;
static u32 brk;

static void add(kn7_region_t *list, int *n, u32 lo, u32 hi)
{
    lo = (lo + 15) & ~15u;
    hi &= ~15u;
    if (hi > lo && hi - lo >= MIN_REGION && *n < MAX_REGIONS)
        list[(*n)++] = (kn7_region_t){lo, hi};
}

/* What the program itself takes of the regions: its image, its bss and its second bss (doom.ld). */
#define OWN 3
static u32 ownLo[OWN], ownHi[OWN];

/* [lo, hi) less what the program takes, into the heap's list when it starts where the bss ends or is ARENA6, else the
 * zone's */
static void place(u32 lo, u32 hi, int from)
{
    for (int k = from; k < OWN; ++k)
        if (ownLo[k] < hi && ownHi[k] > lo)
        {
            if (ownLo[k] > lo)
                place(lo, ownLo[k], k + 1);
            if (ownHi[k] < hi)
                place(ownHi[k], hi, k + 1);
            return;
        }
    if (lo == ownHi[1] || lo == ARENA6_LO)
        add(heap, &heaps, lo, hi);
    else
        add(zone, &zones, lo, hi);
}

static void memoryInit(void)
{
    ownLo[0] = doom_header.base, ownHi[0] = (u32)__image_end;
    ownLo[1] = (u32)__bss_lo, ownHi[1] = ((u32)__game_end + 15) & ~15u;
    ownLo[2] = (u32)__bss2_lo, ownHi[2] = ((u32)__bss2_hi + 15) & ~15u;
    memset(__bss2_lo, 0, __bss2_hi - __bss2_lo);
    zones = heaps = 0;
    for (const PfRegion *r = host->regions; r->hi; ++r)
        place(r->lo, r->hi, 0);
    /* the zone's regions in address order (z_zone.c joins them) */
    for (int i = 1; i < zones; ++i)
        for (int j = i; j > 0 && zone[j - 1].lo > zone[j].lo; --j)
        {
            const kn7_region_t t = zone[j];
            zone[j] = zone[j - 1];
            zone[j - 1] = t;
        }
    heapAt = 0;
    brk = heaps ? heap[0].lo : 0;
}

const kn7_region_t *I_ZoneRegions(int *count)
{
    *count = zones;
    return zone;
}

/* newlib's malloc grows the heap through here: in the region it is in, or the next one with room (a block never spans
 * two, so a request larger than a region fails) */
void *_sbrk(ptrdiff_t n)
{
    const u32 bytes = (u32)n;
    while (heapAt < heaps)
    {
        if (brk >= heap[heapAt].lo && brk + bytes <= heap[heapAt].hi)
        {
            const u32 at = brk;
            brk += bytes;
            return (void *)at;
        }
        if (++heapAt < heaps)
            brk = heap[heapAt].lo;
    }
    errno = ENOMEM;
    return (void *)-1;
}

/* ---- the card's files ------------------------------------------------------------------------------------------ */
#define BOUNCE_SECTORS 16
static u8 bounce[BOUNCE_SECTORS * 512] __attribute__((aligned(16)));  /* (work RAM: the program's bss) */

int kn7FileOpen(const char *path, PfRawFile *f)
{
    const char *name = path;
    for (const char *s = path; *s; ++s)
        if (*s == '/' || *s == '\\')
            name = s + 1;
    memset(f, 0, sizeof *f);
    f->bounce = bounce;
    f->bounceSectors = BOUNCE_SECTORS;
    s32 r = host->rawOpen(name, f);
    if (r == -5)
    {
        f->maxClusters = f->clusters;
        f->chain = malloc(f->clusters * sizeof *f->chain);
        if (f->chain == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
        r = host->rawOpen(name, f);
    }
    if (r)
    {
        kn7FileClose(f);
        errno = r == -3 ? ENOENT : EIO;
        return -1;
    }
    return 0;
}

int kn7FileRead(const PfRawFile *f, unsigned offset, void *dst, unsigned bytes)
{
    return bytes ? host->rawRead(f, offset, dst, bytes) : 0;
}

void kn7FileClose(PfRawFile *f)
{
    free(f->chain);
    f->chain = NULL;
}

/* newlib's file calls: files opened to read, at descriptors 3 up; 1 and 2 the log */
#define MAX_FILES 4
static struct
{
    int used;
    PfRawFile f;
    u32 at;
} file[MAX_FILES];

int _open(const char *path, int flags, int mode)
{
    (void)mode;
    if ((flags & O_ACCMODE) != O_RDONLY)
    {
        errno = EROFS;
        return -1;
    }
    for (int i = 0; i < MAX_FILES; ++i)
        if (!file[i].used)
        {
            if (kn7FileOpen(path, &file[i].f))
                return -1;
            file[i].used = 1;
            file[i].at = 0;
            return 3 + i;
        }
    errno = EMFILE;
    return -1;
}

static int fileOf(int fd)
{
    const int i = fd - 3;
    if (i < 0 || i >= MAX_FILES || !file[i].used)
    {
        errno = EBADF;
        return -1;
    }
    return i;
}

int _read(int fd, void *p, int n)
{
    const int i = fileOf(fd);
    if (i < 0)
        return -1;
    const int got = kn7FileRead(&file[i].f, file[i].at, p, (unsigned)n);
    if (got < 0)
    {
        errno = EIO;
        return -1;
    }
    file[i].at += (u32)got;
    return got;
}

int _lseek(int fd, int offset, int whence)
{
    const int i = fileOf(fd);
    if (i < 0)
        return -1;
    const s32 base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (s32)file[i].at : (s32)file[i].f.bytes;
    if (base + offset < 0)
    {
        errno = EINVAL;
        return -1;
    }
    file[i].at = (u32)(base + offset);
    return (int)file[i].at;
}

int _close(int fd)
{
    const int i = fileOf(fd);
    if (i < 0)
        return fd >= 0 && fd < 3 ? 0 : -1;
    kn7FileClose(&file[i].f);
    file[i].used = 0;
    return 0;
}

int _fstat(int fd, struct stat *s)
{
    memset(s, 0, sizeof *s);
    const int i = fd - 3;
    if (i >= 0 && i < MAX_FILES && file[i].used)
    {
        s->st_mode = S_IFREG;
        s->st_size = (off_t)file[i].f.bytes;
    }
    else
        s->st_mode = S_IFCHR;
    return 0;
}

int _stat(const char *path, struct stat *s)
{
    const int fd = _open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    _fstat(fd, s);
    _close(fd);
    return 0;
}

int _isatty(int fd) { return fd < 3; }
int _unlink(const char *path) { (void)path; errno = EROFS; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; errno = EROFS; return -1; }
int mkdir(const char *path, mode_t mode) { (void)path; (void)mode; errno = EROFS; return -1; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; errno = EINVAL; return -1; }

int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    const u32 ms = host->ms();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
    return 0;
}

/* ---- the startup log ----------------------------------------------------------------------------------------- */
#define LOG_COLS 80
#define LOG_ROWS 18
static char logLine[LOG_ROWS][LOG_COLS + 1];
static int logRows, logCol, logShown;

static void logDraw(void)
{
    host->fill(0, 0, FB_W, FB_H, 0);
    for (int r = 0; r < logRows; ++r)
        host->text(logLine[r], 0, 1 + 13 * r, 0xFFFF, 0);
}

void kn7LogShow(int on)
{
    logShown = on;
    if (on)
        logDraw();
}

static void logChar(char c)
{
    if (logRows == 0)
        logRows = 1;
    if (c == '\n' || logCol >= LOG_COLS)
    {
        if (logShown)
            host->text(logLine[logRows - 1], 0, 1 + 13 * (logRows - 1), 0xFFFF, 0);
        if (logRows == LOG_ROWS)
        {
            memmove(logLine[0], logLine[1], sizeof logLine[0] * (LOG_ROWS - 1));
            if (logShown)
                logDraw();
        }
        else
            ++logRows;
        logCol = 0;
        logLine[logRows - 1][0] = 0;
        if (c == '\n')
            return;
    }
    if (c == '\r' || c == '\b')
        return;
    logLine[logRows - 1][logCol++] = c;
    logLine[logRows - 1][logCol] = 0;
}

int _write(int fd, const void *p, int n)
{
    if (fd != 1 && fd != 2)
    {
        errno = EBADF;
        return -1;
    }
    const char *s = p;
    for (int i = 0; i < n; ++i)
        logChar(s[i]);
    if (logShown && logCol)  /* (the line so far, as Doom prints its progress dots) */
        host->text(logLine[logRows - 1], 0, 1 + 13 * (logRows - 1), 0xFFFF, 0);
    return n;
}

/* ---- the end --------------------------------------------------------------------------------------------------- */
/* assert(), as I_Error (newlib's prints through its own fiprintf: fmt.c) */
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    I_Error("assert %s failed: %s line %d (%s)", expr, file, line, func ? func : "");
}

void abort(void) { I_Error("abort"); }

static jmp_buf quitJmp;
static int exitCode;

void _exit(int code)
{
    exitCode = code;
    longjmp(quitJmp, 1);
}

int doom_entry(const PfHost *h)
{
    host = h;
    memoryInit();
    kn7LogShow(1);
    host->keybedSound(0);  /* the keybed is the game pad (i_input.c), not a piano: KN7APP turns it back on at the end */
    host->status("DOOM: HOLD EXIT TO QUIT");
    static char *argv[] = {"doom", "-iwad", "DOOM1.WAD", NULL};
    if (setjmp(quitJmp) == 0)
    {
        myargc = 3;
        myargv = argv;
        M_FindResponseFile();
        M_SetExeDir();
        D_DoomMain();
    }
    kn7VideoRelease();
    if (exitCode != 0)
    {
        kn7LogShow(1);
        host->status("DOOM STOPPED: HOLD EXIT TO QUIT");
        for (;;)
            host->yield(100);  /* until the player holds EXIT (the task ends in the yield) */
    }
    return 0;
}
