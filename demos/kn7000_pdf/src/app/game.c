/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * game.c -- the program task: PDF.BIN (the viewer: MuPDF, src/mupdf/, and src/kn7/) read to where its header says, its
 * main image in ARENA2 and its segments in other regions (src/kn7/pdf.ld, pfhost.h PfSegment), checked, started with a
 * PfHost; when it returns, the conversion ends. Holding EXIT ends everything as well (entry.c). The firmware draws
 * nothing on the LCD meanwhile (task.c, fwdraw.c). Doom's game.c (ProTracker's, the Pinball port's), with the segments
 * added, without the effects DSP, and with only the frame buffers of the firmware RAM Doom borrowed: the instrument
 * plays on while the viewer runs, so the rest stays its own. */
#include "app.h"

extern const unsigned char font8x13[95][13];

#define WHITE 0xFFFF
#define GREY RGB565(160, 160, 160)
#define RED RGB565(255, 64, 64)

#define PROGRAM_FILE "PDF.BIN"

/* The RAM the program may use, its own image, segments and bss included (it takes them out: src/kn7/main.c), and on
 * programs 893 and 941 the firmware's frame buffers (borrow.c). ARENA3 is left out on program 942 (its SD plug-in lives
 * there), ARENA5-7 on all but 893 and 941. The list ends at the first {0, 0}. */
static PfRegion regions[24];

static void regions_make(void) {
  int n = 0;
  const int any = fw.build == 893 || fw.build == 941;
  regions[n++] = (PfRegion){GAME_BASE, GAME_END};
  regions[n++] = (PfRegion){ARENA2_LO, ARENA2_HI};
  regions[n++] = (PfRegion){ARENA1_LO, ARENA1_HI};
  regions[n++] = (PfRegion){ARENA4_LO, ARENA4_HI};
  if (fw.build != BUILD_SP7001) regions[n++] = (PfRegion){ARENA3_LO, ARENA3_HI};
  if (any) {
    regions[n++] = (PfRegion){ARENA5_LO, ARENA5_HI};
    regions[n++] = (PfRegion){ARENA6_LO, ARENA6_HI};
    regions[n++] = (PfRegion){ARENA7_LO, ARENA7_HI};
  }
  n += borrow_regions(regions + n, (int)(sizeof regions / sizeof *regions) - n - 1);
  regions[n] = (PfRegion){0, 0};
}
static u32 host_ms(void) { return now_ms(); }
static void host_status(const char* s) { set_status(s); }

static PfHost host = {
    .magic = PFHOST_MAGIC,
    .abi = PFHOST_ABI,
    .prefix = prefix,
    .ms = host_ms,
    .yield = task_yield,
    .load = file_load,
    .save = file_save,
    .pollNotes = notes_poll,
    .notes = notes,
    .regions = regions,
    .text = lcd_text,
    .fill = lcd_fill,
    .status = host_status,
    .keybedSound = keys_sound,
    .keybedSoundOn = keys_sound_on,
    .sustain = &notes_sustain,
    .lcdRestore = lcdc_restore,
    .wheel = &wheel_steps,
    .lastTable = -1,
    .font = &font8x13[0][0],
    .wheelLed = wheel_led,
    .panelScan = wheel_scan,
    .noteOns = notes_ons,
    .modWheel = &notes_mod_wheel,
    .list = file_list,
    .rawOpen = file_raw_open,
    .rawRead = file_raw_read,
    .panelTake = panel_take,
    .diag = diag_line,
    .diagSave = diag_save,
};

static u32 crc32(const u8* p, u32 n) {
  u32 c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

/* The CPU's caches (an MN103002A: instruction cache, and a write-back data cache, 4 KB each in two ways of 16-byte
 * lines, both on: CHCTR 0x008F on the unit) after DOOM.BIN was read with data stores: its bytes written back to RAM and
 * no older line of GAME_BASE left in the instruction cache, as program 893 does it (its routines at 0x48407B..: the
 * instruction cache off (CHCTR bit 0), waited for (bit 2), invalidated (bit 4), on again; and func_48407C39: the data
 * cache off (bit 1), waited for (bit 3), each line of both ways written back by reading the purge window 0x28400000 and
 * 0x28401000 at 16-byte steps (unless the cache writes through, bit 6, or a way is locked, bits 12 and 13), invalidated
 * (bit 5), on again). With interrupts off: while the data cache is off, a handler would read RAM that its dirty lines
 * have not reached. (The Pinball port's caches_sync, unchanged: a sled of nops in its place stopped the unit.) */
#define CHCTR16 (*(volatile u16*)0x20000070u)
#define CHCTR32 (*(volatile u32*)0x20000070u)

void caches_sync(void) {
  u32 psw;
  __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
  u32 c = CHCTR16;
  if (c & 0x02) {  /* the data cache: off, written back, invalidated, on */
    u32 d = c & 0xFFFD;
    CHCTR32 = d;
    for (int n = 0; n < 100000 && (CHCTR16 & 0x08); ++n) {}
    if (!(c & 0x40)) {
      if (!(c & 0x1000))
        for (u32 a = 0x28400000u; a < 0x28400000u + 0x800; a += 0x10) (void)*(volatile u32*)a;
      if (!(c & 0x2000))
        for (u32 a = 0x28401000u; a < 0x28401000u + 0x800; a += 0x10) (void)*(volatile u32*)a;
    }
    d |= 0x20;
    CHCTR32 = d;
    d |= 0x02;
    CHCTR16 = (u16)d;
  }
  c = CHCTR16;
  if (c & 0x01) {  /* the instruction cache: off, invalidated, on */
    u32 d = c & 0xFFFE;
    CHCTR32 = d;
    for (int n = 0; n < 100000 && (CHCTR16 & 0x04); ++n) {}
    d |= 0x10;
    CHCTR32 = d;
    d |= 0x01;
    CHCTR16 = (u16)d;
  }
  __asm__ volatile("mov %0,psw\n\tnop\n\tnop" ::"d"(psw) : "memory");
}

/* 1 when [lo, hi) lies in one region (regions_make): where the program's image, segments and bss may be */
static int in_region(u32 lo, u32 hi) {
  for (const PfRegion* r = regions; r->hi; ++r)
    if (lo <= hi && lo >= r->lo && hi <= r->hi) return 1;
  return 0;
}

/* Reads PDF.BIN where its header says, its segments where the table after the header says, and checks them; 0, or why
 * not. The file is read once, from its start: the header and the table first, to know where the rest goes. */
static struct {
  PfGameHeader h;
  PfSegment seg[PF_MAX_SEGMENTS];
} head;

static const char* load_program(void) {
  PfPiece piece = {&head, sizeof head};
  const s32 got = file_load(PROGRAM_FILE, &piece, 1);
  diag("load: the header: %d bytes", got);
  if (got < (s32)sizeof head.h) return "ITS PROGRAM (PDF.BIN) IS NOT ON THE CARD";
  const PfGameHeader* const hd = &head.h;
  diag("load: magic %08X abi %u table %X size %u base %08X bss %08X-%08X entry %08X crc %08X segments %u", hd->magic,
       hd->abi, hd->table, hd->size, hd->base, hd->bssLo, hd->bssHi, (u32)hd->entry, hd->crc, hd->reserved[0]);
  if (hd->magic != PFGAME_MAGIC || hd->abi != PFHOST_ABI || hd->table != PDF_PROGRAM) return "PDF.BIN IS NOT THIS BUILD'S";
  const u32 nseg = hd->reserved[0];
  for (u32 i = 0; i < nseg && i < PF_MAX_SEGMENTS; ++i)
    diag("load: segment %u: %u bytes at %08X, crc %08X", i, head.seg[i].bytes, head.seg[i].addr, head.seg[i].crc);
  if (nseg > PF_MAX_SEGMENTS || hd->size < sizeof head || !in_region(hd->base, hd->base + hd->size) ||
      !in_region(hd->bssLo, hd->bssHi))
    return "PDF.BIN IS DAMAGED (SIZE)";
  for (u32 i = 0; i < nseg; ++i)
    if (!in_region(head.seg[i].addr, head.seg[i].addr + head.seg[i].bytes)) return "PDF.BIN DOES NOT FIT THIS FIRMWARE";
  diag("load: the whole file, %u bytes", hd->size + (nseg ? head.seg[0].bytes : 0) + (nseg > 1 ? head.seg[1].bytes : 0) +
       (nseg > 2 ? head.seg[2].bytes : 0));
  diag_save();  /* (before the long read: if it never returns, the log says so) */
  PfPiece pieces[1 + PF_MAX_SEGMENTS];
  u32 total = hd->size;
  pieces[0] = (PfPiece){(void*)hd->base, hd->size};
  for (u32 i = 0; i < nseg; ++i) {
    pieces[1 + i] = (PfPiece){(void*)head.seg[i].addr, head.seg[i].bytes};
    total += head.seg[i].bytes;
  }
  const s32 read = file_load(PROGRAM_FILE, pieces, 1 + nseg);
  diag("load: %d of %u bytes read", read, total);
  if (read != (s32)total) return "PDF.BIN IS DAMAGED (SIZE)";
  const PfGameHeader* h = (const PfGameHeader*)hd->base;
  const u32 crc = crc32((const u8*)h + 0x40, h->size - 0x40);
  diag("load: the main image's crc %08X, the header says %08X", crc, h->crc);
  if (crc != h->crc) return "PDF.BIN IS DAMAGED (CRC)";
  const PfSegment* seg = (const PfSegment*)(hd->base + 0x40);
  for (u32 i = 0; i < nseg; ++i) {
    const u32 c = crc32((const u8*)seg[i].addr, seg[i].bytes);
    diag("load: segment %u's crc %08X, the table says %08X", i, c, seg[i].crc);
    if (c != seg[i].crc) return "PDF.BIN IS DAMAGED (SEGMENT CRC)";
  }
  for (u32 a = h->bssLo; a < h->bssHi; a += 4) *(volatile u32*)a = 0;
  caches_sync();
  diag("load: bss cleared, caches synced, %u constructors", (h->initHi - h->initLo) / 4);
  for (u32 a = h->initLo; a < h->initHi; a += 4) (*(void (**)(void))a)();
  return 0;
}

void game_task(void) {
  host.build = fw.build;
  host.panel = panel_real;  /* (entry.c: the switches the firmware no longer sees) */
  host.msCounter = fw.ms;
  regions_make();
  for (const PfRegion* r = regions; r->hi; ++r) diag("region %08X-%08X (%u KB)", r->lo, r->hi, (r->hi - r->lo) / 1024);
  crash_arm();
  fwdraw_settle();  /* (the firmware's last copy to the LCD, before ours) */
  set_status("HOLD EXIT TO QUIT");
  lcd_fill(0, 0, FB_W, FB_H, 0);
  lcd_text("PDF VIEWER", 280, 100, WHITE, 0);
  lcd_text("Loading...", 280, 120, GREY, 0);
  borrow_save();  /* (the firmware drew last before fwdraw_settle returned) */
  diag("borrowed RAM saved");
  diag_save();
  const char* why = load_program();
  if (why) {
    diag("load FAILED: %s", why);
    diag_save();
    int n = 0;
    while (why[n]) ++n;
    lcd_text(why, (FB_W - 8 * n) / 2, 140, RED, 0);
    lcd_text("Hold EXIT to leave", 248, 222, GREY, 0);
    set_status(why);
    for (;;) task_yield(50);
  }
  const PfGameHeader* h = (const PfGameHeader*)head.h.base;
  diag("PDF.BIN loaded: its entry %08X", (u32)h->entry);
  diag_save();
  h->entry(&host);
  diag("PDF.BIN returned");
  diag_save();
  wheel_led(0);
  lcdc_restore();
  keys_sound(1);
  quit_req = 1;  /* the program left: the conversion ends (entry.c) */
  for (;;) task_yield(20);
}
