/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * i_video.c -- Chocolate Doom's i_video.c for the KN7000's LCD (640 x 240, an S1D13806), set up as ProTracker's
 * video.c does it: 8 bits a pixel, each the index of a colour in the controller's look-up table (4 bits of red, green
 * and blue), a frame of our own in display memory past the firmware's (which is left alone), shown by the start address.
 * Doom's palette is the look-up table, so a frame goes to the LCD as it is: its 320 x 200 pixels in the middle, its 200
 * lines shown as 240 (every fifth twice), as Chocolate Doom corrects the aspect ratio. A line goes to display memory only
 * when it changed since it last went (the memory is slow: 0.44-0.69 us a halfword, Pinball notes). */
#include <stdio.h>
#include <string.h>

#include "kn7doom.h"
#include "doomtype.h"
#include "i_video.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#define FB_BASE LCD_MEM      /* display memory, uncached */
#define FRAME LCD_FRAME      /* our frame: past the firmware's 640 x 240 x 2 bytes (kn7.h) */
#define PITCH FB_W           /* bytes a line */
#define LCD_H FB_H
#define X0 ((FB_W - SCREENWIDTH) / 2)
#define VNDP_SPIN 2000       /* reads of REG[03Ah] waiting for the vertical non-display period */

static volatile u8 *lcdc(u32 r) { return (volatile u8 *)(0x9CC00000u + r); }

/* Chocolate Doom's settings (m_config.c binds them): kept, most unused here */
int usemouse = 0, png_screenshots = 0, fullscreen = 1, aspect_ratio_correct = 1, integer_scaling = 0;
int smooth_pixel_scaling = 0, vga_porch_flash = 0, force_software_renderer = 1, usegamma = 0;
int video_display = 0, window_width = FB_W, window_height = FB_H, fullscreen_width = 0, fullscreen_height = 0;
int screen_width = SCREENWIDTH, screen_height = SCREENHEIGHT;
char *video_driver = "", *window_position = "";
boolean screenvisible = true, screensaver_mode = false;
unsigned int joywait = 0;
pixel_t *I_VideoBuffer;

static pixel_t screen[SCREENWIDTH * SCREENHEIGHT];
static u32 rowSum[SCREENHEIGHT];  /* what each line was when it last went to display memory */
static boolean owned, allDirty;
static u8 saved40;
static byte palette[768];

static void lutWrite(int i, byte r, byte g, byte b)
{
    *lcdc(0x1E2) = (u8)i;
    *lcdc(0x1E4) = r & 0xF0;
    *lcdc(0x1E4) = g & 0xF0;
    *lcdc(0x1E4) = b & 0xF0;
}

static void lutLoad(void)
{
    *lcdc(0x1E0) = 0;
    for (int i = 0; i < 256; ++i)
        lutWrite(i, gammatable[usegamma][palette[3 * i]], gammatable[usegamma][palette[3 * i + 1]],
                 gammatable[usegamma][palette[3 * i + 2]]);
}

/* REG[042h]-[044h] = w, never across the start of a frame (ProTracker's video.c writeStart) */
static void writeStart(u32 w)
{
    for (int spin = 0;;)
    {
        while ((*lcdc(0x3A) & 0x80) && spin < VNDP_SPIN)
            ++spin;
        u32 psw;
        __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
        if (!(*lcdc(0x3A) & 0x80) || spin >= VNDP_SPIN)
        {
            *(volatile u16 *)lcdc(0x42) = (u16)w;
            *lcdc(0x44) = (u8)(w >> 16);
            __asm__ volatile("mov %0,psw\n\tnop" ::"d"(psw) : "memory");
            return;
        }
        __asm__ volatile("mov %0,psw\n\tnop" ::"d"(psw) : "memory");
    }
}

extern const char *const kn7SoftLabels[2][5];  /* i_input.c */

/* 8 bits a pixel into display memory at (x, y): colour index c, w x h */
static void box(int x, int y, int w, int h, byte c)
{
    for (int yy = y; yy < y + h; ++yy)
    {
        volatile u8 *dst = (volatile u8 *)(FB_BASE + FRAME + (u32)yy * PITCH + (u32)x);
        for (int xx = 0; xx < w; ++xx)
            dst[xx] = c;
    }
}

/* text in KN7APP's 8 x 13 font (PfHost::font: 95 glyphs from ' ', 13 rows, the leftmost dot in bit 7) */
static void text(const char *s, int x, int y, byte fg, byte bg)
{
    for (; *s; ++s, x += 8)
    {
        const u8 *g = host->font + 13 * (*s >= ' ' && *s < 0x7F ? *s - ' ' : 0);
        for (int r = 0; r < 13; ++r)
        {
            volatile u8 *dst = (volatile u8 *)(FB_BASE + FRAME + (u32)(y + r) * PITCH + (u32)x);
            for (int b = 0; b < 8; ++b)
                dst[b] = (g[r] << b) & 0x80 ? fg : bg;
        }
    }
}

/* the soft keys' labels beside them, in the columns either side of the game (i_input.c) */
static void labels(void)
{
    const byte fg = (byte)I_GetPaletteIndex(255, 255, 255), bg = (byte)I_GetPaletteIndex(80, 80, 80);
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < 5; ++i)
        {
            const char *t = kn7SoftLabels[k][i];
            const int w = 8 * (int)strlen(t) + 8, y = 48 * i + 16;
            const int x = k == 0 ? 4 : FB_W - 4 - w;
            box(x, y, w, 17, bg);
            text(t, x + 4, y + 2, fg, bg);
        }
}

void I_InitGraphics(void)
{
    I_VideoBuffer = screen;
    memset(screen, 0, sizeof screen);
    kn7LogShow(0);
    saved40 = *lcdc(0x40);
    *lcdc(0x70) = 0;  /* the ink layer off (the firmware keeps it so) */
    volatile u16 *p = (volatile u16 *)(FB_BASE + FRAME);
    for (u32 i = 0; i < PITCH * LCD_H / 2; ++i)
        p[i] = 0;
    I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));  /* (as Chocolate Doom's I_InitGraphics) */
    lutLoad();
    labels();
    *lcdc(0x46) = (u8)(PITCH / 2);
    *lcdc(0x47) = (u8)((PITCH / 2) >> 8);
    *lcdc(0x40) = (u8)((saved40 & ~7) | 3);  /* 8 bits a pixel, SwivelView kept */
    /* the LCD is turned 180 degrees (SwivelView): the start address is the frame's last word (ProTracker's video.c) */
    writeStart((FRAME + (u32)(LCD_H - 1) * PITCH) / 2 + PITCH / 2 - 1);
    owned = true;
    allDirty = true;
}

void kn7VideoRelease(void)
{
    if (!owned)
        return;
    *lcdc(0x40) = saved40;
    host->lcdRestore();
    owned = false;
}

void I_ShutdownGraphics(void) { kn7VideoRelease(); }

void I_SetPalette(byte *doompalette)
{
    memcpy(palette, doompalette, sizeof palette);
    if (owned)
        lutLoad();
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, bestDiff = 1 << 30;
    for (int i = 0; i < 256; ++i)
    {
        const int dr = palette[3 * i] - r, dg = palette[3 * i + 1] - g, db = palette[3 * i + 2] - b;
        const int diff = dr * dr + dg * dg + db * db;
        if (diff < bestDiff)
        {
            best = i;
            bestDiff = diff;
            if (diff == 0)
                break;
        }
    }
    return best;
}

static u32 lineSum(const u32 *w)
{
    u32 s = 0;
    for (int x = 0; x < SCREENWIDTH / 4; ++x)
        s = (s << 5 | s >> 27) ^ w[x];
    return s;
}

/* Frames drawn, and once a second their rate under the labels (left column): what the player can report. */
u32 kn7Frames;
static u32 fpsAt, fpsFrames;

static void fpsShow(void)
{
    const u32 now = host->ms();
    if (now - fpsAt < 1000)
        return;
    char t[16];
    const u32 tenths = (kn7Frames - fpsFrames) * 10000 / (now - fpsAt);
    snprintf(t, sizeof t, "%2lu.%lu FPS", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
    text(t, 4, FB_H - 14, (byte)I_GetPaletteIndex(160, 160, 160), (byte)I_GetPaletteIndex(0, 0, 0));
    fpsAt = now;
    fpsFrames = kn7Frames;
}

void I_FinishUpdate(void)
{
    if (!owned)
        return;
    ++kn7Frames;
    fpsShow();
    for (int y = 0; y < SCREENHEIGHT; ++y)
    {
        const u32 *src = (const u32 *)(screen + y * SCREENWIDTH);
        const u32 sum = lineSum(src);
        if (sum == rowSum[y] && !allDirty)
            continue;
        rowSum[y] = sum;
        /* LCD line ly shows source line ly * 5 / 6 (240 for 200), so source line y is on LCD lines ceil(6y / 5) up to
         * ceil(6(y + 1) / 5) */
        for (int ly = (6 * y + 4) / 5; ly < (6 * y + 10) / 5; ++ly)
        {
            volatile u16 *dst = (volatile u16 *)(FB_BASE + FRAME + (u32)ly * PITCH + X0);
            const u16 *s16 = (const u16 *)src;
            for (int x = 0; x < SCREENWIDTH / 2; ++x)
                dst[x] = s16[x];
        }
    }
    allDirty = false;
    host->yield(1);  /* the firmware's tasks, once a frame */
}

void I_UpdateNoBlit(void) {}
void I_ReadScreen(pixel_t *scr) { memcpy(scr, screen, sizeof screen); }
void I_BeginRead(void) {}
void I_SetWindowTitle(const char *title) { (void)title; }
void I_CheckIsScreensaver(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void)func; }
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_BindVideoVariables(void) {}
void I_InitWindowTitle(void) {}
void I_RegisterWindowIcon(const unsigned int *icon, int width, int height) { (void)icon; (void)width; (void)height; }
void I_InitWindowIcon(void) {}
void I_GraphicsCheckCommandLine(void) {}
void I_GetWindowPosition(int *x, int *y, int w, int h) { (void)w; (void)h; *x = *y = 0; }
void I_StartFrame(void) {}
void I_StartTic(void) { kn7InputPoll(); }
