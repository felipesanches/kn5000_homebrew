/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * lcd.c -- text and filled rectangles on the LCD (640 x 240 RGB565 at FB), for the menu and the messages. The
 * LCD is behind a 16-bit bus with wait states: a 16-bit store takes about 0.44 us on the instrument, a 32-bit
 * one 0.89 us (technics-anotherworld README-CNV.md, "Speed"), so these write each pixel once. */
#include "app.h"

extern const unsigned char font8x13[95][13];

static volatile u16* px(int x, int y) { return (volatile u16*)(FB + (u32)y * FB_STRIDE + (u32)x * 2); }

void lcd_fill(int x, int y, int w, int h, u16 colour) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > FB_W) w = FB_W - x;
  if (y + h > FB_H) h = FB_H - y;
  if (w <= 0 || h <= 0) return;
  const u32 two = (u32)colour | ((u32)colour << 16);
  for (int r = 0; r < h; ++r) {
    volatile u16* p = px(x, y + r);
    int n = w;
    if (((u32)p & 2) && n) *p++ = colour, --n;
    volatile u32* q = (volatile u32*)p;
    for (; n >= 2; n -= 2) *q++ = two;
    if (n) *(volatile u16*)q = colour;
  }
}

void lcd_text(const char* s, int x, int y, u16 fg, u16 bg) {
  for (; *s; ++s, x += 8) {
    if (x > FB_W - 8) return;
    const unsigned c = (unsigned char)*s;
    const unsigned char* g = font8x13[(c >= 0x20 && c < 0x7F ? c : '?') - 0x20];
    for (int r = 0; r < 13 && y + r < FB_H; ++r) {
      volatile u16* p = px(x, y + r);
      for (int b = 0; b < 8; ++b) p[b] = (g[r] & (0x80 >> b)) ? fg : bg;
    }
  }
}

/* The LCD controller's display mode (S1D13506 REG[040h]: the table program shows 8 bits a pixel), start address and
 * line offset (REG[042h]-[044h], [046h]-[047h]) and its ink layer (REG[070h]-[07Ch]) as the firmware set them: the
 * table program moves the start to scroll and shows the dot display in the ink layer (src/game/screen.cpp), and these
 * are put back whenever a table is left, the program ends, or the
 * crash screen comes (the ink layer's mode, REG[070h], last). */
#define LCDC_REG(n) (*(volatile u8*)(0x9CC00000u + (n)))
static const u8 lcdc_regs[] = {0x40, 0x42, 0x43, 0x44, 0x46, 0x47, 0x71, 0x72, 0x73, 0x74, 0x75,
                                0x76, 0x77, 0x78, 0x7A, 0x7B, 0x7C, 0x70};
#define LCDC_N (sizeof lcdc_regs)
static u8 lcdc_saved[LCDC_N];
static int lcdc_have;

void lcdc_save(void) {
  for (u32 i = 0; i < LCDC_N; ++i) lcdc_saved[i] = LCDC_REG(lcdc_regs[i]);
  lcdc_have = 1;
}

void lcdc_restore(void) {
  if (!lcdc_have) return;
  for (u32 i = 0; i < LCDC_N; ++i) LCDC_REG(lcdc_regs[i]) = lcdc_saved[i];
}
