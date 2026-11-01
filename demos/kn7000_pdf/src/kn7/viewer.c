/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * viewer.c -- the viewer on the KN7000's LCD: the folder's PDFs listed, one opened, its page drawn as wide as the LCD
 * (view.c, MuPDF) into display memory and scrolled by the TEMPO/PROGRAM wheel. The rest of the panel and the keybed stay
 * the instrument's: KN7APP takes from the firmware only the switches this file names (PfHost::panelTake).
 *   * the page: 4 bits a pixel (two a byte, the first in bits 7-4), 16 grays in the controller's look-up table, lines
 *     of PITCH bytes in display memory from LCD_FRAME, as tall as the page is (at most PAGE_ROWS); the controller shows
 *     240 of them from its start address, so the wheel scrolls without a pixel moved (Doom's i_video.c, Pinball's
 *     screen.cpp). The display memory above it is the display heap (pagecache.c): the pages drawn, coded, and fonts;
 *   * pages are drawn ahead of the reader into that cache (pagecache.c), never into the page shown: the next, the one
 *     after, the one before, while the reader reads and scrolls the page shown (MuPDF yields every 20 ms, and the
 *     switches and the wheel are served then, ui()); a page asked for that is not drawn yet is drawn first, and shown
 *     when it is done. Each page drawn is saved on the card when the reader has left the controls alone a while, and
 *     comes from there the next time the document is opened;
 *   * over it, the controller's ink layer (2 bits a dot: colour 0, colour 1, or the page): the soft keys' labels
 *     beside their keys, and a line of status at the bottom while there is something to say (Pinball's screen.cpp);
 *   * the controls: the wheel scrolls (past a page's end, to the next page's top, and back); PAGE UP and PAGE DOWN
 *     the previous and the next page; the soft keys as labelled; EXIT, briefly, back to the list (in the list, held,
 *     KN7APP ends the program: entry.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kn7pdf.h"
#include "pagecache.h"
#include "readcache.h"
#include "view.h"

extern void (*fz_kn7_yield)(void);  /* (MuPDF's, KN7PDF) */
static u32 beatAt;  /* the log's last heartbeat (yieldNow) */
volatile u32 hang_test;  /* (tests/mame/hang.lua sets it: the viewer loops without yielding, for KN7APP's watchdog) */

#define LCDC(r) (*(volatile u8 *)(0x9CC00000u + (r)))
#define PITCH PC_PITCH       /* 4 bits a pixel */
#define PAGE_ROWS 1004       /* the tallest page shown (an 8-bit page filled LCD_FRAME to LCD_KEEP with these) */
/* the display heap (pagecache.c): from the page's end to LCD_KEEP (the 20 KB between the firmware's frame, whose 241
 * lines end at 0x4B500, and LCD_FRAME are left alone: what the firmware keeps there is not known); the font files may
 * take FONT_BUDGET of it */
#define DHEAP_LO ((LCD_FRAME + PAGE_ROWS * PITCH + 15) & ~15u)
#define DHEAP_HI RC_AT
#define DHEAP2_LO 0u
#define DHEAP2_HI 0u
#define FONT_BUDGET (128u << 10)
#define RC_BYTES (32u << 10)  /* the read cache's (readcache.c), at the display heap's top */
#define RC_AT (LCD_KEEP - RC_BYTES)
#define FILE_READ_MAX 1024    /* MuPDF's reads of the PDF at most (stm_open.c): the cache reads ahead */
#define SAVE_IDLE_MS 1500    /* the controls left alone this long: a page drawn goes onto the card */
#define INK LCD_INK
#define INK_PITCH (FB_W / 4)
#define VNDP_SPIN 2000
#define WHEEL_ROWS 24        /* lines a detent of the wheel scrolls */
#define BAND_ROWS 32         /* lines MuPDF draws at a time (view.c) */
#define STORE_BYTES (1u << 30)  /* MuPDF's cache: no limit of its own; it gives memory back when an allocation fails */
#define LABEL_W 56           /* the soft keys' label columns */

/* ---- the controller ---------------------------------------------------------------------------------------- */
static u8 saved40, saved46, saved47;
static int owned;

/* REG[042h]-[044h] = w, never across the start of a frame (Doom's i_video.c writeStart) */
static void writeStart(u32 w) {
  for (int spin = 0;;) {
    while ((LCDC(0x3A) & 0x80) && spin < VNDP_SPIN) ++spin;
    u32 psw;
    __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
    if (!(LCDC(0x3A) & 0x80) || spin >= VNDP_SPIN) {
      *(volatile u16 *)&LCDC(0x42) = (u16)w;
      LCDC(0x44) = (u8)(w >> 16);
      __asm__ volatile("mov %0,psw\n\tnop" ::"d"(psw) : "memory");
      return;
    }
    __asm__ volatile("mov %0,psw\n\tnop" ::"d"(psw) : "memory");
  }
}

/* the 240 lines from `top` of the page shown (the LCD is turned 180 degrees: the start address is the last word shown) */
static void show(int top) { writeStart((LCD_FRAME + (u32)(top + FB_H - 1) * PITCH) / 2 + PITCH / 2 - 1); }

static volatile u8 *line(int y) { return (volatile u8 *)(LCD_MEM + LCD_FRAME + (u32)y * PITCH); }

/* lines [y, y + h) of the page memory set to gray g (0-15) */
static void clearLines(int y, int h, u8 g) {
  const u16 two = (u16)((g | (g << 4)) * 0x101);
  for (int r = y; r < y + h; ++r) {
    volatile u16 *p = (volatile u16 *)line(r);
    for (int x = 0; x < PITCH / 2; ++x) p[x] = two;
  }
}

/* the ink layer's dot (x, y) of the viewer = code (0 colour 0, 1 colour 1, 2 the page): the layer is in the panel's order */
static void inkDot(int x, int y, int code) {
  if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) return;
  const u32 px = (u32)(FB_W - 1 - x), py = (u32)(FB_H - 1 - y);
  volatile u8 *b = (volatile u8 *)(LCD_MEM + INK + py * INK_PITCH + px / 4);
  const int shift = 6 - 2 * (int)(px & 3);
  *b = (u8)((*b & ~(3 << shift)) | (code << shift));
}

/* a box of the ink layer: code */
static void inkBox(int x, int y, int w, int h, int code) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx) inkDot(xx, yy, code);
}

/* text in KN7APP's 8 x 13 font, colour 1 on colour 0 */
static void inkText(const char *s, int x, int y) {
  for (; *s; ++s, x += 8) {
    const unsigned c = (unsigned char)*s;
    const u8 *g = host->font + 13 * ((c >= 0x20 && c < 0x7F ? c : '?') - 0x20);
    for (int r = 0; r < 13; ++r)
      for (int b = 0; b < 8; ++b) inkDot(x + b, y + r, (g[r] & (0x80 >> b)) ? 1 : 0);
  }
}

/* the ink layer all page (transparent) */
static void inkClear(void) {
  volatile u16 *p = (volatile u16 *)(LCD_MEM + INK);
  for (u32 i = 0; i < INK_PITCH * FB_H / 2; ++i) p[i] = 0xAAAA;
}

/* the look-up table's first 16 entries, which 4 bits a pixel use: gray i (S1D13506 spec, REG[1E2h]-[1E4h], bits 7-4) */
static void lutLoad(void) {
  LCDC(0x1E0) = 0;
  for (int i = 0; i < 16; ++i) {
    LCDC(0x1E2) = (u8)i;
    LCDC(0x1E4) = (u8)(i << 4);
    LCDC(0x1E4) = (u8)(i << 4);
    LCDC(0x1E4) = (u8)(i << 4);
  }
}

/* the controller ours: 4 bits a pixel (REG[040h] bits 2-0 = 010b), gray, lines of PITCH bytes, the ink layer on
 * (Pinball's Screen::own, inkOn) */
static void own(void) {
  saved40 = LCDC(0x40), saved46 = LCDC(0x46), saved47 = LCDC(0x47);
  LCDC(0x70) = 0;
  clearLines(0, FB_H, 15);
  inkClear();
  lutLoad();
  LCDC(0x46) = (u8)(PITCH / 2);
  LCDC(0x47) = (u8)((PITCH / 2) >> 8);
  LCDC(0x40) = (u8)((saved40 & ~7) | 2);
  show(0);
  /* colour 0 a dark gray, colour 1 white (5, 6, 5 bits of blue, green, red) */
  LCDC(0x76) = 6, LCDC(0x77) = 12, LCDC(0x78) = 6;
  LCDC(0x7A) = 31, LCDC(0x7B) = 63, LCDC(0x7C) = 31;
  LCDC(0x71) = (u8)((0x140000u - INK) / 8192);  /* (8 KB pages from the top of the 1,280 KB) */
  for (u32 r = 0x72; r <= 0x75; ++r) LCDC(r) = 0;
  LCDC(0x70) = 2;
  owned = 1;
}

static void release(void) {
  if (!owned) return;
  LCDC(0x70) = 0;
  LCDC(0x46) = saved46, LCDC(0x47) = saved47;
  LCDC(0x40) = saved40;
  host->lcdRestore();
  owned = 0;
}

/* ---- the soft keys and the switches ------------------------------------------------------------------------- */
enum { K_L1, K_L2, K_L3, K_L4, K_L5, K_R1, K_R2, K_R3, K_R4, K_R5, K_PAGE_UP, K_PAGE_DOWN, K_EXIT, KEYS };
static const u32 keyBit[KEYS] = {BTN_LCDL1, BTN_LCDL2, BTN_LCDL3, BTN_LCDL4, BTN_LCDL5, BTN_LCDR1, BTN_LCDR2,
                                 BTN_LCDR3, BTN_LCDR4, BTN_LCDR5, BTN_PAGE_UP, BTN_PAGE_DOWN, BTN_EXIT};
static int keyWas[KEYS];
static s32 wheelWas;

/* what the viewer takes from the firmware: these switches (the wheel KN7APP takes always) */
static void takeKeys(void) {
  u8 mask[0x40];
  memset(mask, 0, sizeof mask);
  for (int k = 0; k < KEYS; ++k) mask[keyBit[k] >> 8] |= (u8)(keyBit[k] & 0xFF);
  host->panelTake(mask);
}

static int down(int k) { return (host->panel[keyBit[k] >> 8] & (keyBit[k] & 0xFF)) != 0; }

/* the keys gone down since the last look, kept until pressed() takes them: latch() runs between pages and, through
 * MuPDF's yield, while one is drawn, so a key pressed and let go during a page of a minute counts */
static u32 latched;
static u8 panelWas[0x40];  /* every switch as last seen, the firmware's too (KN7APP logs each switch frame) */
static u32 saveAt, checkAt;  /* the log onto the card 1 s after the last switch change; the heap checked every 200 ms */
static void latch(void) {
  host->panelScan();
  const u32 t = *(volatile u32 *)host->msCounter;
  for (int i = 0; i < 0x40; ++i)
    if (host->panel[i] != panelWas[i]) {
      saveAt = t + 1000 ? t + 1000 : 1;
      panelWas[i] = host->panel[i];
    }
  static unsigned damaged;  /* (a switch's effect on the firmware shows soon after it: the heap checked here) */
  if ((s32)(t - checkAt) >= 0) {
    checkAt = t + 200;
    if (kn7HeapCheck() != damaged) {
      damaged = kn7HeapCheck();
      saveAt = 0;
      host->diagSave();  /* (the log onto the card at once: what comes next may stop the program) */
    }
  }
  if (saveAt && (s32)(t - saveAt) >= 0) {
    saveAt = 0;
    host->diagSave();
  }
  for (int k = 0; k < KEYS; ++k) {
    const int d = down(k);
    u8 *const hit = &host->panelHit[keyBit[k] >> 8];
    if ((d && !keyWas[k]) || (*hit & (keyBit[k] & 0xFF))) latched |= 1u << k;
    *hit &= (u8)~(keyBit[k] & 0xFF);
    keyWas[k] = d;
  }
}

/* the keys pressed since the last call (a bit each), and the wheel's detents */
static u32 pressed(s32 *wheel) {
  latch();
  const u32 p = latched;
  latched = 0;
  const s32 w = *host->wheel;
  *wheel = w - wheelWas;
  wheelWas = w;
  return p;
}

/* the labels beside the soft keys: five a side, 48 lines apart (Doom's i_video.c labels) */
static void labels(const char *const left[5], const char *const right[5]) {
  inkBox(0, 0, LABEL_W, FB_H, 2);
  inkBox(FB_W - LABEL_W, 0, LABEL_W, FB_H, 2);
  for (int i = 0; i < 5; ++i) {
    const int y = 48 * i + 16;
    if (left[i] && *left[i]) {
      const int w = 8 * (int)strlen(left[i]) + 8;
      inkBox(0, y, w, 17, 0);
      inkText(left[i], 4, y + 2);
    }
    if (right[i] && *right[i]) {
      const int w = 8 * (int)strlen(right[i]) + 8;
      inkBox(FB_W - w, y, w, 17, 0);
      inkText(right[i], FB_W - w + 4, y + 2);
    }
  }
}

/* the status line at the bottom, in the middle, or none (s 0) */
static int statusW;
static void status(const char *s) {
  if (statusW) inkBox((FB_W - statusW) / 2, FB_H - 17, statusW, 17, 2);
  statusW = 0;
  if (!s || !*s) return;
  statusW = 8 * (int)strlen(s) + 8;
  if (statusW > FB_W - 2 * LABEL_W) statusW = FB_W - 2 * LABEL_W;
  inkBox((FB_W - statusW) / 2, FB_H - 17, statusW, 17, 0);
  char t[80];
  snprintf(t, sizeof t, "%.*s", (statusW - 8) / 8, s);
  inkText(t, (FB_W - statusW) / 2 + 4, FB_H - 15);
}

/* ---- the file list ------------------------------------------------------------------------------------------ */
#define MAX_FILES 200
static PfFile files[MAX_FILES];
static int nFiles;

static int isPdf(const char *n) {
  const char *dot = strrchr(n, '.');
  return dot && (dot[1] == 'P' || dot[1] == 'p') && (dot[2] == 'D' || dot[2] == 'd') && (dot[3] == 'F' || dot[3] == 'f') &&
         !dot[4];
}

#define MAX_LISTED 600  /* the folder's files looked at (its cached pages, Kxxxxnnn.PGC, among them) */
static void listFiles(void) {
  PfFile *all = malloc(MAX_LISTED * sizeof *all);
  nFiles = 0;
  if (!all) return;
  const s32 n = host->list(all, MAX_LISTED);
  for (s32 i = 0; i < n && nFiles < MAX_FILES; ++i)
    if (isPdf(all[i].name)) files[nFiles++] = all[i];
  free(all);
}

/* 8 x 13 text into the page memory at (x, y), x a multiple of 2: gray fg on bg (0-15) */
static void pageText(const char *s, int x, int y, u8 fg, u8 bg) {
  for (; *s && x <= FB_W - 8; ++s, x += 8) {
    const unsigned c = (unsigned char)*s;
    const u8 *g = host->font + 13 * ((c >= 0x20 && c < 0x7F ? c : '?') - 0x20);
    for (int r = 0; r < 13; ++r) {
      volatile u8 *p = line(y + r) + x / 2;
      for (int b = 0; b < 8; b += 2)
        p[b / 2] = (u8)(((g[r] & (0x80 >> b)) ? fg : bg) << 4 | ((g[r] & (0x40 >> b)) ? fg : bg));
    }
  }
}

#define LIST_ROWS 14
static void drawList(int sel, int first) {
  clearLines(0, FB_H, 0);
  show(0);
  pageText("PDF VIEWER (MuPDF 1.1)", LABEL_W + 8, 4, 15, 0);
  char t[64];
  snprintf(t, sizeof t, "%d PDF file%s in the folder", nFiles, nFiles == 1 ? "" : "s");
  pageText(t, LABEL_W + 8, 20, 10, 0);
  for (int i = 0; i < LIST_ROWS && first + i < nFiles; ++i) {
    const PfFile *f = &files[first + i];
    snprintf(t, sizeof t, " %-12s %7lu KB ", f->name, (unsigned long)((f->bytes + 1023) / 1024));
    const int on = first + i == sel;
    pageText(t, LABEL_W + 8, 40 + 14 * i, on ? 0 : 15, on ? 15 : 0);
  }
  if (!nFiles) pageText("Put PDF files in the card's PDF folder.", LABEL_W + 8, 60, 15, 0);
}

/* ---- the page ----------------------------------------------------------------------------------------------- */
static int pages, pageNo = -1, pageH, top;  /* the page shown (-1: none yet), its height (at least FB_H), the top line */
static int want = -1, wantTop;          /* the page asked for and not shown yet (-1: none), its top line (-1: end) */
static int job = -1;                    /* the page being drawn (into the cache) */
static int quitDoc, logReq, noRoom;     /* FILES or EXIT, LOG asked for; the cache had no room for the page */
static u32 lastInput;                   /* when the reader last turned the wheel or pressed a key */
static char docName[16];

static u32 nowMs(void) { return *(volatile u32 *)host->msCounter; }

static void scrollTo(int y) {
  if (y > pageH - FB_H) y = pageH - FB_H;
  if (y < 0) y = 0;
  top = y;
  show(top);
}

/* the status line: the page shown, and what is being drawn meanwhile */
static void pageStatus(void) {
  char t[80];
  if (want >= 0)
    snprintf(t, sizeof t, "%s  page %d of %d: drawing page %d...", docName, pageNo + 1, pages, want + 1);
  else if (job >= 0)
    snprintf(t, sizeof t, "%s  page %d of %d  (drawing %d)", docName, pageNo + 1, pages, job + 1);
  else
    snprintf(t, sizeof t, "%s  page %d of %d", docName, pageNo + 1, pages);
  status(pageNo >= 0 || want >= 0 ? t : NULL);
}

/* page n out of the cache (or the card) onto the LCD, from line t (-1: its end) -> 0, or -1 */
static int showPage(int n, int t) {
  const u32 t0 = nowMs();
  const int rows = pc_show(n, LCD_FRAME, PAGE_ROWS);
  if (rows < 0) return -1;
  if (rows < FB_H) clearLines(rows, FB_H - rows, 15);
  pageNo = n, pageH = rows < FB_H ? FB_H : rows;
  if (want == n) want = -1;
  scrollTo(t < 0 ? pageH : t);
  printf("page %d shown, %lu ms\n", n + 1, (unsigned long)(nowMs() - t0));
  pageStatus();
  return 0;
}

/* page n asked for, from line t: shown now if it is drawn, else drawn next (what is being drawn instead stops) */
static void go(int n, int t) {
  if (n < 0 || n >= pages) return;
  if (n == pageNo) {
    want = -1;
    scrollTo(t < 0 ? pageH : t);
    pageStatus();
    return;
  }
  want = n, wantTop = t;
  if ((pc_have(n) || pc_onCard(n)) && showPage(n, t) == 0) return;
  if (job >= 0 && job != n) view_abort();
  pageStatus();
}

/* the switches and the wheel, from the main loop and, while a page is drawn, from MuPDF's yield */
static void ui(void) {
  s32 w;
  const u32 p = pressed(&w);
  if (!p && !w) return;
  lastInput = nowMs();
  if (p & (1u << K_EXIT | 1u << K_L1)) quitDoc = 1;
  if (p & (1u << K_L5)) logReq = 1;
  if (quitDoc || logReq) {
    if (job >= 0) view_abort();
    return;
  }
  if (pageNo < 0) return;  /* (nothing shown yet: the first page is on its way) */
  const int base = want >= 0 ? want : pageNo;
  if (p & (1u << K_PAGE_DOWN | 1u << K_L3)) go(base + 1 < pages ? base + 1 : base, 0);
  if (p & (1u << K_PAGE_UP | 1u << K_L2)) go(base > 0 ? base - 1 : 0, 0);
  if (p & (1u << K_L4)) go(0, 0);
  if (p & (1u << K_R1)) scrollTo(0);
  if (p & (1u << K_R5)) scrollTo(pageH);
  if (w > 0) {  /* (clockwise: down the page; past its end, the next page's top) */
    if (top >= pageH - FB_H) {
      if (want < 0 && pageNo + 1 < pages) go(pageNo + 1, 0);
    } else {
      scrollTo(top + w * WHEEL_ROWS);
    }
  } else if (w < 0) {  /* (back: a page asked for by the wheel is let go; past the top, the page before's end) */
    if (want >= 0) {
      want = -1;
      pageStatus();
    }
    if (top == 0 && pageNo > 0) go(pageNo - 1, -1);
    else scrollTo(top + w * WHEEL_ROWS);
  }
}

/* MuPDF's bands: into the cache, or (drawDirect) onto the LCD */
static void bandCache(void *user, int y, int h, int w, const unsigned char *gray, int stride) {
  (void)user, (void)w;
  if (!noRoom && pc_band(y, h, gray, stride) < 0) {
    noRoom = 1;
    view_abort();
  }
}

static void bandDirect(void *user, int y, int h, int w, const unsigned char *gray, int stride) {
  (void)user;
  for (int r = 0; r < h; ++r) {
    volatile u16 *dst = (volatile u16 *)line(y + r);
    const unsigned char *s = gray + r * stride;
    for (int x = 0; x < w / 4; ++x)
      dst[x] = (u16)(((s[8 * x] & 0xF0) | (s[8 * x + 2] >> 4)) | ((s[8 * x + 4] & 0xF0) | (s[8 * x + 6] >> 4)) << 8);
  }
}

static void logPage(int n, const char *how, int h, u32 t0, const ViewStats *st) {
  HeapStats hs;
  kn7HeapStats(&hs);
  u32 dt, du, fonts;
  int in;
  pc_stats(&dt, &du, &in, &fonts);
  printf("page %d %s: %d lines, %lu ms (load %u, list %u, bands %u: %u%s)%s; heap %u KB of %u, peak %u; cache %lu of "
         "%lu KB, %d pages, fonts %lu KB\n",
         n + 1, how, h, (unsigned long)(nowMs() - t0), st->load_ms, st->list_ms, st->draw_ms, st->bands,
         st->direct ? ", no list" : "", st->incomplete ? " INCOMPLETE" : "", hs.used >> 10, hs.total >> 10,
         hs.peak >> 10, (unsigned long)(du >> 10), (unsigned long)(dt >> 10), in, (unsigned long)(fonts >> 10));
  /* since the last page logged: the card's reads for the PDF (readcache.c), the reads the cache served, the Type 3
   * glyph procedures loaded */
  extern int fz_kn7_t3_loads;
  static u32 f0, b0, h0;
  static int t0s;
  extern int kn7_errBudget;
  extern unsigned kn7_errDropped;
  printf("  card reads %lu (%lu KB), from the cache %lu; Type 3 glyphs loaded %d; MuPDF's messages not logged %u\n",
         (unsigned long)(rc_fetches - f0), (unsigned long)((rc_fetch_bytes - b0) >> 10), (unsigned long)(rc_hits - h0),
         fz_kn7_t3_loads - t0s, kn7_errDropped);
  kn7_errBudget = 24, kn7_errDropped = 0;  /* (the next page's) */
  f0 = rc_fetches, b0 = rc_fetch_bytes, h0 = rc_hits, t0s = fz_kn7_t3_loads;
}

/* page n drawn straight onto the LCD (the cache had no room for it), the reader waiting */
static void drawDirect(int n) {
  ViewStats st = {0};
  const u32 t0 = nowMs();
  pageNo = -1, want = n;
  pageStatus();
  scrollTo(0);
  const int h = view_draw(n, FB_W, PAGE_ROWS, BAND_ROWS, bandDirect, NULL, &st);
  if (h < 0) {
    printf("page %d: %s\n", n + 1, view_error());
    want = -1;
    status("This page cannot be drawn: see the log (L5)");
    return;
  }
  if (h < FB_H) clearLines(h, FB_H - h, 15);
  pageNo = n, pageH = h < FB_H ? FB_H : h, want = -1;
  scrollTo(wantTop < 0 ? pageH : wantTop);
  logPage(n, "drawn onto the LCD", h, t0, &st);
  pageStatus();
}

/* page n drawn into the cache; shown when the reader waits for it */
static void render(int n) {
  ViewStats st = {0};
  const u32 t0 = nowMs();
  noRoom = pc_begin(n) < 0;
  job = n;
  pageStatus();
  printf("drawing page %d\n", n + 1);
  beatAt = nowMs();
  const int h = noRoom ? -2 : view_draw(n, FB_W, PAGE_ROWS, BAND_ROWS, bandCache, NULL, &st);
  job = -1;
  if (h >= 0 && !noRoom) {
    pc_end(h, st.incomplete);
    logPage(n, "drawn", h, t0, &st);
    if (want == n) showPage(n, wantTop);
    else pageStatus();
    return;
  }
  pc_drop();
  if (h == -2 && !noRoom) {
    printf("page %d put aside (%lu ms)\n", n + 1, (unsigned long)(nowMs() - t0));
    pageStatus();
    return;
  }
  pc_setFailed(n);  /* (not drawn ahead again) */
  if (h == -1) {
    printf("page %d: %s\n", n + 1, view_error());
    if (want == n) {
      want = -1;
      status("This page cannot be drawn: see the log (L5)");
    }
    host->diagSave();
    return;
  }
  printf("page %d: no room in the cache\n", n + 1);
  if (want == n) drawDirect(n);
  host->diagSave();
}

/* the page to draw ahead of the reader: the next, the one after, the one before, the third; or one the card has, the
 * next, into the cache -> its number, or -1 */
static int ahead(void) {
  if (pageNo < 0) return -1;
  const int order[4] = {pageNo + 1, pageNo + 2, pageNo - 1, pageNo + 3};
  for (int i = 0; i < 4; ++i) {
    const int n = order[i];
    if (n < 0 || n >= pages || pc_have(n) || pc_failed(n)) continue;
    if (pc_onCard(n)) {
      if (i == 0) pc_load(n);
      continue;
    }
    return n;
  }
  return -1;
}

static const char *const pageLeft[5] = {"FILES", "PREV", "NEXT", "FIRST", "LOG"};
static const char *const pageRight[5] = {"TOP", "", "", "", "END"};
static const char *const listLeft[5] = {"", "", "", "", "LOG"};
static const char *const listRight[5] = {"OPEN", "UP", "DOWN", "", ""};

static void showLog(void) {
  kn7LogShow(1);
  LCDC(0x70) = 0;
  release();
  for (;;) {
    s32 w;
    if (pressed(&w)) break;
    host->yield(30);
  }
  kn7LogShow(0);
  own();
}

/* one document, from its first page, until FILES or EXIT */
static void viewDoc(const PfFile *f) {
  snprintf(docName, sizeof docName, "%s", f->name);
  status("Opening...");
  printf("opening %s\n", f->name);
  host->diagSave();
  pageNo = -1, want = -1, job = -1, quitDoc = logReq = 0;
  pages = view_open(f->name);
  printf("%s: %d pages\n", f->name, pages);
  host->diagSave();
  if (pages <= 0) {
    printf("%s: %s\n", f->name, pages < 0 ? view_error() : "no pages");
    host->diagSave();
    status("This file cannot be opened: see the log (L5)");
    for (;;) {
      s32 w;
      if (pressed(&w) || w) return;
      host->yield(30);
    }
  }
  pc_open(f->name, f->bytes, pages);
  inkClear();
  labels(pageLeft, pageRight);
  clearLines(0, FB_H, 15);
  pageH = FB_H;
  scrollTo(0);
  go(0, 0);
  for (;;) {
    ui();
    while (hang_test) {}
    if (quitDoc) break;
    if (logReq) {
      logReq = 0;
      showLog();
      labels(pageLeft, pageRight);
      if (pageNo >= 0 && showPage(pageNo, top) < 0) want = pageNo, wantTop = top, pageNo = -1;
      continue;
    }
    if (want >= 0) {
      if ((pc_have(want) || pc_onCard(want)) && showPage(want, wantTop) == 0) continue;
      render(want);
      continue;
    }
    if (nowMs() - lastInput >= SAVE_IDLE_MS) {
      const int u = pc_unsaved();
      if (u >= 0) {
        const u32 t0 = nowMs();
        const int r = pc_save(u);
        printf("page %d %s the card, %lu ms\n", u + 1, r ? "not saved on" : "saved on", (unsigned long)(nowMs() - t0));
        host->diagSave();  /* (the log too, the reader idle: the page's lines onto the card) */
        continue;
      }
    }
    const int n = ahead();
    if (n >= 0) {
      render(n);
      continue;
    }
    host->yield(20);
  }
  pc_close();
  view_close();
  pageNo = -1, pageH = 0, want = -1;
  status(NULL);
}

static unsigned clockMs(void) { return (unsigned)host->ms(); }

/* MuPDF's yield (fz_kn7_yield): the firmware's tasks get the CPU at least every YIELD_MS while a page is drawn; the
 * keybed and the panel go on being served meanwhile */
#define YIELD_MS 20
static u32 yieldAt;
u32 yields;
/* and every HEARTBEAT_MS while MuPDF works, a line into the log and the log onto the card (PDFLOG.TXT): on the
 * instrument, where a page that never ends cannot say whether it is slow or stuck otherwise */
#define HEARTBEAT_MS 5000
static void yieldNow(void) {
  const u32 t = *(volatile u32 *)host->msCounter;
  if (t - yieldAt < YIELD_MS) return;
  host->yield(1);
  yieldAt = *(volatile u32 *)host->msCounter;
  ++yields;
  if (job >= 0) ui();  /* (the reader reads and scrolls the page shown meanwhile) */
  else latch();
  if (*view_stage && yieldAt - beatAt >= HEARTBEAT_MS) {
    HeapStats hs;
    kn7HeapStats(&hs);
    printf("  ... %s at line %d; yields %lu; heap %u KB, peak %u, failed %u%s\n", view_stage, view_y,
           (unsigned long)yields, hs.used >> 10, hs.peak >> 10, kn7HeapFailures(), hs.damaged ? "; HEAP DAMAGED" : "");
    host->diagSave();
    beatAt = *(volatile u32 *)host->msCounter;
  }
}

/* KN7APP's watchdog found the task stuck (PfHost::stuck): where the viewer and MuPDF's rasterizers were, into the log */
void kn7_gray_report(int (*out)(const char *, ...)); /* (FreeType's ftgrays.c, KN7PDF) */
void kn7_ft_report(int (*out)(const char *, ...));   /* (res_font.c, KN7PDF) */
static void stuck(void) {
  HeapStats hs;
  kn7HeapStats(&hs);
  printf("viewer: stuck in \"%s\" at line %d drawing page %d (page %d shown); yields %lu; heap %u KB, peak %u, "
         "failed %u\n",
         view_stage, view_y, job + 1, pageNo + 1, (unsigned long)yields, hs.used >> 10, hs.peak >> 10, kn7HeapFailures());
  kn7_ft_report(printf);
  kn7_gray_report(printf);
}

int viewer_main(void) {
  printf("PDF viewer: MuPDF 1.1 on the KN7000\n");
  HeapStats hs;
  kn7HeapStats(&hs);
  printf("heap: %u KB in %u regions\n", hs.total >> 10, hs.regions);
  takeKeys();
  memset(host->panelHit, 0, 0x40);  /* (presses from before the viewer: not its) */
  host->status("PDF VIEWER: HOLD EXIT TO QUIT");
  view_clock = clockMs;
  fz_kn7_yield = yieldNow;
  ((PfHost *)host)->stuck = stuck;  /* (the one field the program sets: pfhost.h) */
  view_failures = kn7HeapFailures;
  pc_init(DHEAP_LO, DHEAP_HI, DHEAP2_LO, DHEAP2_HI, FONT_BUDGET);
  extern void *(*fz_kn7_slow_malloc)(unsigned int size);  /* (MuPDF's res_font.c, KN7PDF: the font files) */
  extern void (*fz_kn7_slow_free)(void *p);
  extern void *(*fz_kn7_slow_try)(unsigned int size);
  fz_kn7_slow_malloc = pc_slow_malloc;
  fz_kn7_slow_free = pc_slow_free;
  fz_kn7_slow_try = pc_slow_try;
  extern int fz_kn7_file_read_max;
  fz_kn7_file_read_max = FILE_READ_MAX;
  rc_init((void *)(LCD_MEM + RC_AT), RC_BYTES);
  if (view_init(NULL, STORE_BYTES) < 0) {
    printf("%s\n", view_error());
    return 1;
  }
  printf("MuPDF's context made\n");
  extern const char *fontfile_dir;
  fontfile_dir = "";  /* (the folder: KN7APP puts it in front of every name) */
  listFiles();
  printf("%d PDF files listed\n", nFiles);
  for (int i = 0; i < nFiles; ++i) printf("  %s %lu bytes\n", files[i].name, (unsigned long)files[i].bytes);
  host->diagSave();
  own();
  int sel = 0, first = 0;
  for (;;) {
    inkClear();
    labels(listLeft, listRight);
    drawList(sel, first);
    for (;;) {
      s32 w;
      const u32 p = pressed(&w);
      int move = (int)w + ((p >> K_R3) & 1) - ((p >> K_R2) & 1);
      if (move) {
        sel += move;
        if (sel < 0) sel = 0;
        if (sel >= nFiles) sel = nFiles - 1;
        if (sel < 0) sel = 0;
        if (sel < first) first = sel;
        if (sel >= first + LIST_ROWS) first = sel - LIST_ROWS + 1;
        drawList(sel, first);
      }
      if (p & (1u << K_L5)) {
        showLog();
        break;
      }
      if ((p & (1u << K_R1)) && nFiles) {
        viewDoc(&files[sel]);
        break;
      }
      if (!p && !w) host->yield(20);
    }
  }
  release();
  return 0;
}
