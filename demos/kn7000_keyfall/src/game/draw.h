/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * draw.h -- the LCD as the game uses it (draw.c): 640 x 240, 8 bits a pixel through the controller's look-up table
 * (the colours C_*), lines of 640 bytes in display memory from LCD_FRAME; over it the controller's ink layer (2 bits a
 * dot: colour 0, colour 1, or see-through) with the soft keys' labels. */
#ifndef KF_DRAW_H
#define KF_DRAW_H
#include "kf.h"

enum {
  C_BLACK, C_HUD, C_LANE_W, C_LANE_B, C_SEP, C_BEAT, C_BAR, C_OCT, C_HITLINE, C_MARGIN,
  C_NOTE0_W, C_NOTE0_B, C_NOTE1_W, C_NOTE1_B, C_HIT0, C_HIT1, C_MISSED, C_DEMO_W, C_DEMO_B,
  C_KEY_W, C_KEY_B, C_KEY_HELD_W, C_KEY_HELD_B, C_KEY_HIT, C_KEY_WRONG, C_KEY_SEP, C_KEY_NEXT_W, C_KEY_NEXT_B,
  C_WHITE, C_GREY, C_YELLOW, C_GREEN, C_RED, C_CYAN, C_ORANGE, C_DIM,
  C_MENU, C_SEL, C_PROGRESS, C_FRAME,
  C_COUNT
};

void lcd_own(void);       /* the controller set up for the game (the firmware's settings kept) */
void lcd_release(void);   /* and back as the firmware had it */
void fill8(int x, int y, int w, int h, u8 c);
void text8(const char *s, int x, int y, u8 fg, u8 bg);           /* 8 x 13 */
void text8s(const char *s, int x, int y, u8 fg, u8 bg, int scale);  /* each dot scale x scale */
int text_w(const char *s);
void ink_clear(void);
void ink_labels(const char *const left[5], const char *const right[5]);
void ink_line(const char *s);  /* a line at the bottom, in the middle (0: none) */

#define LABEL_W 56

#endif
