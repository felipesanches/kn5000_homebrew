/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * main.c -- PDF.BIN: the viewer on the KN7000, run by KN7APP.BIN (src/app/) on its program task.
 *   * memory: the regions KN7APP hands over, less the program's own image, segments and bss (pdf.ld), are one heap
 *     (TLSF, src/third/tlsf/), malloc and its family for MuPDF, FreeType and the C library alike;
 *   * files: the card's PDF\ folder, read by sectors anywhere in a file (PfHost::rawOpen, rawRead), through newlib's
 *     open/read/lseek, which MuPDF opens a document with (fz_open_file). Nothing is written;
 *   * the log: what MuPDF prints (its warnings and errors), on the LCD when shown;
 *   * the end: exit() comes back here.
 * (Doom's main.c, with its zone and newlib's heap replaced by TLSF.) */
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "kn7pdf.h"
#include "tlsf.h"

extern char __bss_lo[], __bss_hi[], __init_lo[], __init_hi[], __game_end[], __image_end[];
int viewer_main(void);
int pdf_entry(const PfHost *h);

/* ---- the header KN7APP.BIN checks (pfhost.h PfGameHeader) and the segments after it; mkpdf.py fills in the sizes, the
 * CRC-32s and the segments ---- */
__attribute__((section(".header"), used)) const PfGameHeader pdf_header = {
    PFGAME_MAGIC, PFHOST_ABI, 0, PDF_PROGRAM, (u32)__bss_lo, (u32)__bss_hi, (u32)__init_lo, (u32)__init_hi,
    (u32)__game_end, pdf_entry, 0, (u32)&pdf_header, {0, 0, 0, 0}};
__attribute__((section(".segtab"), used)) const PfSegment pdf_segments[PF_MAX_SEGMENTS];

const PfHost *host;

/* ---- memory --------------------------------------------------------------------------------------------------- */
#define MAX_POOLS 24
#define MIN_POOL 0x1000  /* smaller pieces are left unused */
static tlsf_t heap;
static pool_t pools[MAX_POOLS];
static unsigned heap_total, heap_regions;
static size_t heap_used, heap_peak;
static unsigned heap_failed;  /* allocations refused (MuPDF had given back what its store held first) */

unsigned kn7HeapFailures(void) { return heap_failed; }

/* What the program itself takes of the regions: its main image, its segments and its bss. */
#define OWN (2 + PF_MAX_SEGMENTS)
static u32 ownLo[OWN], ownHi[OWN];

static void pool(u32 lo, u32 hi) {
  lo = (lo + 15) & ~15u;
  hi &= ~15u;
  if (hi <= lo || hi - lo < MIN_POOL || heap_regions >= MAX_POOLS) return;
  if (!heap) {
    heap = tlsf_create_with_pool((void *)lo, hi - lo);
    pools[heap_regions] = tlsf_get_pool(heap);
  } else {
    pools[heap_regions] = tlsf_add_pool(heap, (void *)lo, hi - lo);
  }
  heap_total += hi - lo;
  ++heap_regions;
}

/* [lo, hi) less what the program takes, into the heap */
static void place(u32 lo, u32 hi, int from) {
  for (int k = from; k < OWN; ++k)
    if (ownHi[k] > ownLo[k] && ownLo[k] < hi && ownHi[k] > lo) {
      if (ownLo[k] > lo) place(lo, ownLo[k], k + 1);
      if (ownHi[k] < hi) place(ownHi[k], hi, k + 1);
      return;
    }
  pool(lo, hi);
}

static void memoryInit(void) {
  ownLo[0] = pdf_header.base, ownHi[0] = (u32)__image_end;
  ownLo[1] = (u32)__bss_lo, ownHi[1] = ((u32)__game_end + 15) & ~15u;
  /* (read through volatile: mkpdf.py fills in the count and the table after the link, and to the compiler both are
   * constant zeros, so it read neither and the heap took the segments' code: FreeType's, in ARENA4, was overwritten) */
  const u32 nseg = ((const volatile PfGameHeader *)&pdf_header)->reserved[0];
  const volatile PfSegment *seg = pdf_segments;
  for (u32 i = 0; i < nseg && i < PF_MAX_SEGMENTS; ++i)
    ownLo[2 + i] = seg[i].addr, ownHi[2 + i] = (seg[i].addr + seg[i].bytes + 15) & ~15u;
  heap = NULL;
  heap_total = heap_regions = 0;
  heap_used = heap_peak = 0;
  for (const PfRegion *r = host->regions; r->hi; ++r) place(r->lo, r->hi, 0);
}

static void count(void *p, int sign) {
  if (!p) return;
  heap_used += sign * tlsf_block_size(p);
  if (heap_used > heap_peak) heap_peak = heap_used;
}

void *malloc(size_t n) {
  void *p = heap ? tlsf_malloc(heap, n ? n : 1) : NULL;
  count(p, 1);
  if (!p) errno = ENOMEM, ++heap_failed;
  return p;
}

void free(void *p) {
  if (!p) return;
  count(p, -1);
  tlsf_free(heap, p);
}

void *realloc(void *p, size_t n) {
  if (!p) return malloc(n);
  if (!n) {
    free(p);
    return NULL;
  }
  const size_t old = tlsf_block_size(p);
  void *q = tlsf_realloc(heap, p, n);
  if (q) {
    heap_used -= old;
    count(q, 1);
  } else {
    errno = ENOMEM;
    ++heap_failed;
  }
  return q;
}

void *calloc(size_t a, size_t b) {
  if (b && a > (size_t)-1 / b) return NULL;
  void *p = malloc(a * b);
  if (p) memset(p, 0, a * b);
  return p;
}

/* (newlib's own callers) */
struct _reent;
void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; free(p); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void)r; return calloc(a, b); }

static void largest_walker(void *ptr, size_t size, int used, void *user) {
  (void)ptr;
  if (!used && size > *(size_t *)user) *(size_t *)user = size;
}

void kn7HeapStats(HeapStats *s) {
  size_t largest = 0;
  s->total = heap_total;
  s->used = heap_used;
  s->peak = heap_peak;
  s->regions = heap_regions;
  for (unsigned i = 0; heap && i < heap_regions; ++i)
    if (pools[i]) tlsf_walk_pool(pools[i], largest_walker, &largest);
  s->largest = largest;
}

/* ---- the card's files ------------------------------------------------------------------------------------------ */
#define BOUNCE_SECTORS 16
static u8 bounce[BOUNCE_SECTORS * 512] __attribute__((aligned(16)));  /* (work RAM: the program's bss) */

int kn7FileOpen(const char *path, PfRawFile *f) {
  const char *name = path;
  for (const char *s = path; *s; ++s)
    if (*s == '/' || *s == '\\') name = s + 1;
  memset(f, 0, sizeof *f);
  f->bounce = bounce;
  f->bounceSectors = BOUNCE_SECTORS;
  s32 r = host->rawOpen(name, f);
  if (r == -5) {
    f->maxClusters = f->clusters;
    f->chain = malloc(f->clusters * sizeof *f->chain);
    if (f->chain == NULL) {
      errno = ENOMEM;
      return -1;
    }
    r = host->rawOpen(name, f);
  }
  if (r) {
    kn7FileClose(f);
    errno = r == -3 ? ENOENT : EIO;
    return -1;
  }
  return 0;
}

int kn7FileRead(const PfRawFile *f, unsigned offset, void *dst, unsigned bytes) {
  if (offset >= f->bytes) return 0;
  if (bytes > f->bytes - offset) bytes = f->bytes - offset;
  return bytes ? host->rawRead(f, offset, dst, bytes) : 0;
}

void kn7FileClose(PfRawFile *f) {
  free(f->chain);
  f->chain = NULL;
}

/* newlib's file calls: files opened to read, at descriptors 3 up; 1 and 2 the log */
#define MAX_FILES 4
static struct {
  int used;
  PfRawFile f;
  u32 at;
} file[MAX_FILES];

int _open(const char *path, int flags, int mode) {
  (void)mode;
  if ((flags & O_ACCMODE) != O_RDONLY) {
    errno = EROFS;
    return -1;
  }
  for (int i = 0; i < MAX_FILES; ++i)
    if (!file[i].used) {
      if (kn7FileOpen(path, &file[i].f)) return -1;
      file[i].used = 1;
      file[i].at = 0;
      return 3 + i;
    }
  errno = EMFILE;
  return -1;
}

static int fileOf(int fd) {
  const int i = fd - 3;
  if (i < 0 || i >= MAX_FILES || !file[i].used) {
    errno = EBADF;
    return -1;
  }
  return i;
}

int _read(int fd, void *p, int n) {
  const int i = fileOf(fd);
  if (i < 0) return -1;
  const int got = kn7FileRead(&file[i].f, file[i].at, p, (unsigned)n);
  if (got < 0) {
    errno = EIO;
    return -1;
  }
  file[i].at += (u32)got;
  return got;
}

int _lseek(int fd, int offset, int whence) {
  const int i = fileOf(fd);
  if (i < 0) return -1;
  const s32 base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (s32)file[i].at : (s32)file[i].f.bytes;
  if (base + offset < 0) {
    errno = EINVAL;
    return -1;
  }
  file[i].at = (u32)(base + offset);
  return (int)file[i].at;
}

int _close(int fd) {
  const int i = fileOf(fd);
  if (i < 0) return fd >= 0 && fd < 3 ? 0 : -1;
  kn7FileClose(&file[i].f);
  file[i].used = 0;
  return 0;
}

int _fstat(int fd, struct stat *s) {
  memset(s, 0, sizeof *s);
  const int i = fd - 3;
  if (i >= 0 && i < MAX_FILES && file[i].used) {
    s->st_mode = S_IFREG;
    s->st_size = (off_t)file[i].f.bytes;
  } else {
    s->st_mode = S_IFCHR;
  }
  return 0;
}

int _stat(const char *path, struct stat *s) {
  const int fd = _open(path, O_RDONLY, 0);
  if (fd < 0) return -1;
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

int _gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  const u32 ms = host->ms();
  tv->tv_sec = ms / 1000;
  tv->tv_usec = (ms % 1000) * 1000;
  return 0;
}

/* ---- the log --------------------------------------------------------------------------------------------------- */
#define LOG_COLS 80
#define LOG_ROWS 18
static char logLine[LOG_ROWS][LOG_COLS + 1];
static int logRows, logCol, logShown;

static void logDraw(void) {
  host->fill(0, 0, FB_W, FB_H, 0);
  for (int r = 0; r < logRows; ++r) host->text(logLine[r], 0, 1 + 13 * r, 0xFFFF, 0);
}

void kn7LogShow(int on) {
  logShown = on;
  if (on) logDraw();
}

static void logChar(char c) {
  if (logRows == 0) logRows = 1;
  if (c == '\n' || logCol >= LOG_COLS) {
    if (logShown) host->text(logLine[logRows - 1], 0, 1 + 13 * (logRows - 1), 0xFFFF, 0);
    if (logRows == LOG_ROWS) {
      memmove(logLine[0], logLine[1], sizeof logLine[0] * (LOG_ROWS - 1));
      if (logShown) logDraw();
    } else {
      ++logRows;
    }
    logCol = 0;
    logLine[logRows - 1][0] = 0;
    if (c == '\n') return;
  }
  if (c == '\r' || c == '\b') return;
  logLine[logRows - 1][logCol++] = c;
  logLine[logRows - 1][logCol] = 0;
}

int _write(int fd, const void *p, int n) {
  if (fd != 1 && fd != 2) {
    errno = EBADF;
    return -1;
  }
  const char *s = p;
  for (int i = 0; i < n; ++i) logChar(s[i]);
  if (logShown && logCol) host->text(logLine[logRows - 1], 0, 1 + 13 * (logRows - 1), 0xFFFF, 0);
  return n;
}

/* ---- the end --------------------------------------------------------------------------------------------------- */
static jmp_buf quitJmp;
static int exitCode;

void __assert_func(const char *file, int line, const char *func, const char *expr) {
  printf("assert %s failed: %s line %d (%s)\n", expr, file, line, func ? func : "");
  _exit(3);
}

void abort(void) {
  printf("abort\n");
  _exit(3);
}

void _exit(int code) {
  exitCode = code;
  longjmp(quitJmp, 1);
}

int pdf_entry(const PfHost *h) {
  host = h;
  memoryInit();
  if (setjmp(quitJmp) == 0) exitCode = viewer_main();
  if (exitCode != 0) {
    kn7LogShow(1);
    host->status("PDF VIEWER STOPPED: HOLD EXIT");
    for (;;) host->yield(100);  /* until the player holds EXIT (the task ends in the yield) */
  }
  return 0;
}
