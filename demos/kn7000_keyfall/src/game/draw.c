/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * draw.c -- the LCD (draw.h), as the PDF viewer sets it up (kn7000_pdf src/kn7/viewer.c: own, the start address, the
 * ink layer; Pinball's screen.cpp before it): the controller (an S1D13806, REG[040h] the mode) at 8 bits a pixel, its
 * look-up table the game's colours (4 bits a component), the frame at LCD_FRAME in display memory, shown from its start
 * address (the LCD is turned 180 degrees: the start address is the last word shown). Display memory is behind a 16-bit
 * bus with wait states (a 16-bit store about 0.44 us on the instrument), so the game draws only what changes. */
#include "draw.h"

#define LCDC(r) (*(volatile u8 *)(0x9CC00000u + (r)))
#define PITCH FB_W
#define INK LCD_INK
#define INK_PITCH (FB_W / 4)
#define VNDP_SPIN 2000

extern const PfHost *host;

/* the colours, 0xRGB (4 bits each) */
static const u16 palette[C_COUNT] = {
    [C_BLACK] = 0x000,      [C_HUD] = 0x113,        [C_LANE_W] = 0x111,     [C_LANE_B] = 0x000,
    [C_SEP] = 0x000,        [C_BEAT] = 0x333,       [C_BAR] = 0x556,        [C_OCT] = 0x223,
    [C_HITLINE] = 0x99A,    [C_MARGIN] = 0x000,     [C_NOTE0_W] = 0x3C4,    [C_NOTE0_B] = 0x293,
    [C_NOTE1_W] = 0x58F,    [C_NOTE1_B] = 0x36C,    [C_HIT0] = 0xCFC,       [C_HIT1] = 0xCDF,
    [C_MISSED] = 0x544,     [C_DEMO_W] = 0xDB4,     [C_DEMO_B] = 0xA82,     [C_KEY_W] = 0xEEE,
    [C_KEY_B] = 0x111,      [C_KEY_HELD_W] = 0x8CF, [C_KEY_HELD_B] = 0x47B, [C_KEY_HIT] = 0x5E5,
    [C_KEY_WRONG] = 0xE44,  [C_KEY_SEP] = 0x666,    [C_KEY_NEXT_W] = 0xBEB, [C_KEY_NEXT_B] = 0x262,
    [C_WHITE] = 0xFFF,      [C_GREY] = 0x999,       [C_YELLOW] = 0xFE4,     [C_GREEN] = 0x5F5,
    [C_RED] = 0xF55,        [C_CYAN] = 0x5EF,       [C_ORANGE] = 0xF93,     [C_DIM] = 0x666,
    [C_MENU] = 0x112,       [C_SEL] = 0x36A,        [C_PROGRESS] = 0x47A,   [C_FRAME] = 0x335,
};

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

static volatile u8 *line(int y) { return (volatile u8 *)(LCD_MEM + LCD_FRAME + (u32)y * PITCH); }

void fill8(int x, int y, int w, int h, u8 c) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > FB_W) w = FB_W - x;
  if (y + h > FB_H) h = FB_H - y;
  if (w <= 0 || h <= 0) return;
  const u16 two = (u16)(c | c << 8);
  for (int r = 0; r < h; ++r) {
    volatile u8 *p = line(y + r) + x;
    int n = w;
    if (((u32)p & 1) && n) *p++ = c, --n;
    volatile u16 *q = (volatile u16 *)p;
    for (; n >= 2; n -= 2) *q++ = two;
    if (n) *(volatile u8 *)q = c;
  }
}

int text_w(const char *s) { return 8 * kf_strlen(s); }

void text8s(const char *s, int x, int y, u8 fg, u8 bg, int scale) {
  for (; *s; ++s, x += 8 * scale) {
    if (x > FB_W - 8 * scale) return;
    const unsigned c = (unsigned char)*s;
    const u8 *g = host->font + 13 * ((c >= 0x20 && c < 0x7F ? c : '?') - 0x20);
    for (int r = 0; r < 13; ++r) {
      if (y + r * scale >= FB_H) break;
      if (scale == 1) {
        volatile u8 *p = line(y + r) + x;
        for (int b = 0; b < 8; ++b) p[b] = (g[r] & (0x80 >> b)) ? fg : bg;
      } else {
        for (int b = 0; b < 8; ++b) fill8(x + b * scale, y + r * scale, scale, scale, (g[r] & (0x80 >> b)) ? fg : bg);
      }
    }
  }
}

void text8(const char *s, int x, int y, u8 fg, u8 bg) { text8s(s, x, y, fg, bg, 1); }

/* ---- the ink layer: the soft keys' labels (the viewer's) ---- */
static void inkDot(int x, int y, int code) {
  if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) return;
  const u32 px = (u32)(FB_W - 1 - x), py = (u32)(FB_H - 1 - y);
  volatile u8 *b = (volatile u8 *)(LCD_MEM + INK + py * INK_PITCH + px / 4);
  const int shift = 6 - 2 * (int)(px & 3);
  *b = (u8)((*b & ~(3 << shift)) | (code << shift));
}

static void inkBox(int x, int y, int w, int h, int code) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx) inkDot(xx, yy, code);
}

static void inkText(const char *s, int x, int y) {
  for (; *s; ++s, x += 8) {
    const unsigned c = (unsigned char)*s;
    const u8 *g = host->font + 13 * ((c >= 0x20 && c < 0x7F ? c : '?') - 0x20);
    for (int r = 0; r < 13; ++r)
      for (int b = 0; b < 8; ++b) inkDot(x + b, y + r, (g[r] & (0x80 >> b)) ? 1 : 0);
  }
}

void ink_clear(void) {
  volatile u16 *p = (volatile u16 *)(LCD_MEM + INK);
  for (u32 i = 0; i < INK_PITCH * FB_H / 2; ++i) p[i] = 0xAAAA;
}

/* five a side, 48 lines apart, beside their keys */
void ink_labels(const char *const left[5], const char *const right[5]) {
  inkBox(0, 0, LABEL_W, FB_H, 2);
  inkBox(FB_W - LABEL_W, 0, LABEL_W, FB_H, 2);
  for (int i = 0; i < 5; ++i) {
    const int y = 48 * i + 16;
    if (left && left[i] && *left[i]) {
      const int w = text_w(left[i]) + 8;
      inkBox(0, y, w, 17, 0);
      inkText(left[i], 4, y + 2);
    }
    if (right && right[i] && *right[i]) {
      const int w = text_w(right[i]) + 8;
      inkBox(FB_W - w, y, w, 17, 0);
      inkText(right[i], FB_W - w + 4, y + 2);
    }
  }
}

static int lineW;
void ink_line(const char *s) {
  if (lineW) inkBox((FB_W - lineW) / 2, FB_H - 17, lineW, 17, 2);
  lineW = 0;
  if (!s || !*s) return;
  lineW = text_w(s) + 8;
  if (lineW > FB_W - 2 * LABEL_W) lineW = FB_W - 2 * LABEL_W;
  inkBox((FB_W - lineW) / 2, FB_H - 17, lineW, 17, 0);
  inkText(s, (FB_W - lineW) / 2 + 4, FB_H - 15);
}

static void lutLoad(void) {
  LCDC(0x1E0) = 0;
  for (int i = 0; i < 256; ++i) {
    const u16 c = i < C_COUNT ? palette[i] : 0;
    LCDC(0x1E2) = (u8)i;
    LCDC(0x1E4) = (u8)((c >> 4) & 0xF0);
    LCDC(0x1E4) = (u8)(c & 0xF0);
    LCDC(0x1E4) = (u8)((c << 4) & 0xF0);
  }
}

void lcd_own(void) {
  if (owned) return;
  saved40 = LCDC(0x40), saved46 = LCDC(0x46), saved47 = LCDC(0x47);
  LCDC(0x70) = 0;
  fill8(0, 0, FB_W, FB_H, C_BLACK);
  ink_clear();
  lutLoad();
  LCDC(0x46) = (u8)(PITCH / 2);
  LCDC(0x47) = (u8)((PITCH / 2) >> 8);
  LCDC(0x40) = (u8)((saved40 & ~7) | 3);
  writeStart((LCD_FRAME + (u32)(FB_H - 1) * PITCH) / 2 + PITCH / 2 - 1);
  /* the labels: colour 0 a dark blue, colour 1 white (5, 6, 5 bits of blue, green, red) */
  LCDC(0x76) = 10, LCDC(0x77) = 8, LCDC(0x78) = 3;
  LCDC(0x7A) = 31, LCDC(0x7B) = 63, LCDC(0x7C) = 31;
  LCDC(0x71) = (u8)((0x140000u - INK) / 8192);  /* (8 KB pages from the top of the 1,280 KB) */
  for (u32 r = 0x72; r <= 0x75; ++r) LCDC(r) = 0;
  LCDC(0x70) = 2;
  owned = 1;
}

void lcd_release(void) {
  if (!owned) return;
  LCDC(0x70) = 0;
  LCDC(0x46) = saved46, LCDC(0x47) = saved47;
  LCDC(0x40) = saved40;
  host->lcdRestore();
  owned = 0;
}
