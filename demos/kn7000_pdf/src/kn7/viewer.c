/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * viewer.c -- the viewer on the KN7000's LCD: the folder's PDFs listed, one opened, its page drawn as wide as the LCD
 * (view.c, MuPDF) into display memory and scrolled by the TEMPO/PROGRAM wheel. The rest of the panel and the keybed stay
 * the instrument's: KN7APP takes from the firmware only the switches this file names (PfHost::panelTake).
 *   * the page: 8 bits a pixel, 256 grays in the controller's look-up table (4 bits each), lines of 640 bytes in display
 *     memory from LCD_FRAME, as tall as the page is (at most PAGE_ROWS); the controller shows 240 of them from its start
 *     address, so the wheel scrolls without a pixel moved (Doom's i_video.c, Pinball's screen.cpp);
 *   * over it, the controller's ink layer (2 bits a dot: colour 0, colour 1, or the page): the soft keys' labels
 *     beside their keys, and a line of status at the bottom while there is something to say (Pinball's screen.cpp);
 *   * the controls: the wheel scrolls (past a page's end, to the next page's top, and back); PAGE UP and PAGE DOWN
 *     the previous and the next page; the soft keys as labelled; EXIT, briefly, back to the list (in the list, held,
 *     KN7APP ends the program: entry.c). */
#include <stdio.h>
#include <string.h>

#include "kn7pdf.h"
#include "view.h"

extern void (*fz_kn7_yield)(void);  /* (MuPDF's, KN7PDF) */
static u32 beatAt;  /* the log's last heartbeat (yieldNow) */
volatile u32 hang_test;  /* (tests/mame/hang.lua sets it: the viewer loops without yielding, for KN7APP's watchdog) */

#define LCDC(r) (*(volatile u8 *)(0x9CC00000u + (r)))
#define PITCH FB_W
#define PAGE_ROWS ((LCD_KEEP - LCD_FRAME) / PITCH)  /* the tallest page display memory holds: 1,004 lines */
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

/* lines [y, y + h) of the page memory set to gray g */
static void clearLines(int y, int h, u8 g) {
  const u16 two = (u16)(g | (g << 8));
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

static void lutLoad(void) {
  LCDC(0x1E0) = 0;
  for (int i = 0; i < 256; ++i) {
    LCDC(0x1E2) = (u8)i;
    LCDC(0x1E4) = (u8)(i & 0xF0);
    LCDC(0x1E4) = (u8)(i & 0xF0);
    LCDC(0x1E4) = (u8)(i & 0xF0);
  }
}

/* the controller ours: 8 bits a pixel, gray, lines of PITCH bytes, the ink layer on (Pinball's Screen::own, inkOn) */
static void own(void) {
  saved40 = LCDC(0x40), saved46 = LCDC(0x46), saved47 = LCDC(0x47);
  LCDC(0x70) = 0;
  clearLines(0, FB_H, 255);
  inkClear();
  lutLoad();
  LCDC(0x46) = (u8)(PITCH / 2);
  LCDC(0x47) = (u8)((PITCH / 2) >> 8);
  LCDC(0x40) = (u8)((saved40 & ~7) | 3);
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

/* the keys pressed since the last call (a bit each), and the wheel's detents */
static u32 pressed(s32 *wheel) {
  host->panelScan();
  u32 p = 0;
  for (int k = 0; k < KEYS; ++k) {
    const int d = down(k);
    if (d && !keyWas[k]) p |= 1u << k;
    keyWas[k] = d;
  }
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

static void listFiles(void) {
  static PfFile all[MAX_FILES];
  const s32 n = host->list(all, MAX_FILES);
  nFiles = 0;
  for (s32 i = 0; i < n; ++i)
    if (isPdf(all[i].name)) files[nFiles++] = all[i];
}

/* 8 x 13 text into the page memory at (x, y): gray fg on bg */
static void pageText(const char *s, int x, int y, u8 fg, u8 bg) {
  for (; *s && x <= FB_W - 8; ++s, x += 8) {
    const unsigned c = (unsigned char)*s;
    const u8 *g = host->font + 13 * ((c >= 0x20 && c < 0x7F ? c : '?') - 0x20);
    for (int r = 0; r < 13; ++r) {
      volatile u8 *p = line(y + r) + x;
      for (int b = 0; b < 8; ++b) p[b] = (g[r] & (0x80 >> b)) ? fg : bg;
    }
  }
}

#define LIST_ROWS 14
static void drawList(int sel, int first) {
  clearLines(0, FB_H, 0);
  show(0);
  pageText("PDF VIEWER (MuPDF 1.1)", LABEL_W + 8, 4, 255, 0);
  char t[64];
  snprintf(t, sizeof t, "%d PDF file%s in the folder", nFiles, nFiles == 1 ? "" : "s");
  pageText(t, LABEL_W + 8, 20, 160, 0);
  for (int i = 0; i < LIST_ROWS && first + i < nFiles; ++i) {
    const PfFile *f = &files[first + i];
    snprintf(t, sizeof t, " %-12s %7lu KB ", f->name, (unsigned long)((f->bytes + 1023) / 1024));
    const int on = first + i == sel;
    pageText(t, LABEL_W + 8, 40 + 14 * i, on ? 0 : 255, on ? 255 : 0);
  }
  if (!nFiles) pageText("Put PDF files in the card's PDF folder.", LABEL_W + 8, 60, 255, 0);
}

/* ---- the page ----------------------------------------------------------------------------------------------- */
static int pageNo, pages, pageH, top;
static char docName[16];

static void band(void *user, int y, int h, int w, const unsigned char *gray, int stride) {
  (void)user;
  for (int r = 0; r < h; ++r) {
    volatile u16 *dst = (volatile u16 *)line(y + r);
    const unsigned char *s = gray + r * stride;
    for (int x = 0; x < w / 2; ++x) dst[x] = (u16)(s[4 * x] | (s[4 * x + 2] << 8));  /* (gray and alpha: gray in 0) */
  }
}

/* page n drawn into the page memory; 0, or -1 (the log says why) */
static int drawPage(int n) {
  char t[64];
  snprintf(t, sizeof t, "%s: drawing page %d of %d...", docName, n + 1, pages);
  status(t);
  ViewStats st = {0};
  const u32 t0 = host->ms();
  printf("drawing page %d\n", n + 1);
  host->diagSave();
  beatAt = host->ms();
  const int h = view_draw(n, FB_W, PAGE_ROWS, BAND_ROWS, band, NULL, &st);
  if (h < 0) {
    printf("page %d: %s\n", n + 1, view_error());
    host->diagSave();
    status("This page cannot be drawn: see the log (L5)");
    return -1;
  }
  if (h < PAGE_ROWS) clearLines(h, (h < FB_H ? FB_H : h + 1) - h, 255);
  pageNo = n;
  pageH = h < FB_H ? FB_H : h;
  HeapStats hs;
  kn7HeapStats(&hs);
  printf("page %d: %d lines, %lu ms (load %u, list %u, bands %u: %u%s)%s; heap %u KB of %u, peak %u\n", n + 1, h,
         (unsigned long)(host->ms() - t0), st.load_ms, st.list_ms, st.draw_ms, st.bands, st.direct ? ", no list" : "",
         st.incomplete ? " INCOMPLETE" : "", hs.used >> 10, hs.total >> 10, hs.peak >> 10);
  snprintf(t, sizeof t, "%s  page %d of %d%s", docName, n + 1, pages,
           st.incomplete ? "  (memory ran short: it may lack something)" : "");
  status(t);
  host->diagSave();
  return 0;
}

static void scrollTo(int y) {
  if (y > pageH - FB_H) y = pageH - FB_H;
  if (y < 0) y = 0;
  top = y;
  show(top);
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
  inkClear();
  labels(pageLeft, pageRight);
  int want = 0, wantTop = 0;
  for (;;) {
    if (want != pageNo || pageH == 0) {
      top = 0;
      show(0);
      if (drawPage(want) == 0) scrollTo(wantTop < 0 ? pageH : wantTop);
      want = pageNo;
    }
    s32 w;
    const u32 p = pressed(&w);
    while (hang_test) {}
    if (p & (1u << K_EXIT | 1u << K_L1)) break;
    if (p & (1u << K_L5)) {
      showLog();
      labels(pageLeft, pageRight);
      show(top);
    }
    if (p & (1u << K_PAGE_DOWN | 1u << K_L3)) want = pageNo + 1 < pages ? pageNo + 1 : pageNo, wantTop = 0;
    if (p & (1u << K_PAGE_UP | 1u << K_L2)) want = pageNo > 0 ? pageNo - 1 : 0, wantTop = 0;
    if (p & (1u << K_L4)) want = 0, wantTop = 0;
    if (p & (1u << K_R1)) scrollTo(0);
    if (p & (1u << K_R5)) scrollTo(pageH);
    if (w > 0) {  /* (clockwise: down the page; past its end, the next page's top) */
      if (top >= pageH - FB_H && pageNo + 1 < pages) want = pageNo + 1, wantTop = 0;
      else scrollTo(top + w * WHEEL_ROWS);
    } else if (w < 0) {
      if (top == 0 && pageNo > 0) want = pageNo - 1, wantTop = -1;
      else scrollTo(top + w * WHEEL_ROWS);
    }
    if (!p && !w) host->yield(20);
  }
  view_close();
  pageNo = 0, pageH = 0;
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
  if (*view_stage && yieldAt - beatAt >= HEARTBEAT_MS) {
    HeapStats hs;
    kn7HeapStats(&hs);
    printf("  ... %s at line %d; yields %lu; heap %u KB, peak %u, failed %u\n", view_stage, view_y,
           (unsigned long)yields, hs.used >> 10, hs.peak >> 10, kn7HeapFailures());
    host->diagSave();
    beatAt = *(volatile u32 *)host->msCounter;
  }
}

int viewer_main(void) {
  printf("PDF viewer: MuPDF 1.1 on the KN7000\n");
  HeapStats hs;
  kn7HeapStats(&hs);
  printf("heap: %u KB in %u regions\n", hs.total >> 10, hs.regions);
  takeKeys();
  host->status("PDF VIEWER: HOLD EXIT TO QUIT");
  view_clock = clockMs;
  fz_kn7_yield = yieldNow;
  view_failures = kn7HeapFailures;
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
