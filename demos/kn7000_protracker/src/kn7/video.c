/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * video.c -- the KN7000's LCD (640 x 240, an S1D13806) at 8 bits a pixel, as the Pinball port's screen.cpp sets it up:
 * each pixel the index of a colour in the controller's look-up table (4 bits of red, green and blue: the Amiga's own
 * 12-bit colours), a frame of our own in display memory past the firmware's (which is left alone), shown by the start
 * address. ProTracker's 320 x 240 screen (pt2-clone's video.frameBuffer, in RAM) is in the middle; the 160 columns either
 * side carry the soft keys' labels (ui.c). videoPresent writes the 16-bit words of the screen that changed since it last
 * wrote them (the display memory is slow: 0.44-0.69 us a halfword, Pinball notes).
 * The look-up table: entries 0-14 ProTracker's palette (pt2_palette.c), the rest as kn7Colour hands them out. */
#include <stdlib.h>
#include <string.h>

#include "kn7pt.h"
#include "pt2_structs.h"

#define FB_BASE 0x9CE00000u        /* display memory, uncached */
#define FRAME 0x50000u              /* our frame: past the firmware's 640 x 240 x 2 bytes */
#define PITCH LCD_W                 /* bytes a line */
#define VNDP_SPIN 2000              /* reads of REG[03Ah] waiting for the vertical non-display period */

static volatile uint8_t *lcdc(uint32_t r) { return (volatile uint8_t *)(0x9CC00000u + r); }

static pixel_t *shown;              /* the screen as written to display memory */
static pixel_t *side[2], *sideShown[2];  /* the side columns (sides.c draws them), and as written */
static uint16_t lut[256];           /* the look-up table: 0x0RGB */
static int lutUsed;
static bool owned;
static uint8_t saved40;

static uint16_t rgb12(uint32_t rgb24)
{
	const uint32_t r = (((rgb24 >> 16) & 0xFF) * 15 + 127) / 255;
	const uint32_t g = (((rgb24 >> 8) & 0xFF) * 15 + 127) / 255;
	const uint32_t b = ((rgb24 & 0xFF) * 15 + 127) / 255;
	return (uint16_t)((r << 8) | (g << 4) | b);
}

static void lutWrite(int i)
{
	*lcdc(0x1E2) = (uint8_t)i;
	*lcdc(0x1E4) = (uint8_t)(((lut[i] >> 8) & 15) << 4);
	*lcdc(0x1E4) = (uint8_t)(((lut[i] >> 4) & 15) << 4);
	*lcdc(0x1E4) = (uint8_t)((lut[i] & 15) << 4);
}

pixel_t kn7Colour(uint32_t rgb24)
{
	const uint16_t c = rgb12(rgb24);
	for (int i = 0; i < lutUsed; i++)
		if (lut[i] == c)
			return (pixel_t)i;

	if (lutUsed >= 256)
		return 0;

	lut[lutUsed] = c;
	if (owned)
		lutWrite(lutUsed);

	return (pixel_t)lutUsed++;
}

void videoInit(void)
{
	video.frameBuffer = (pixel_t *)calloc(SCREEN_W * SCREEN_H, 1);
	shown = (pixel_t *)malloc(SCREEN_W * SCREEN_H);
	for (int k = 0; k < 2; k++)
	{
		side[k] = (pixel_t *)calloc(SIDE_W * LCD_H, 1);
		sideShown[k] = (pixel_t *)malloc(SIDE_W * LCD_H);
	}
	lutUsed = PALETTE_NUM;
	for (int i = 0; i < PALETTE_NUM; i++)
		lut[i] = rgb12(video.paletteRGB[i]);
}

/* REG[042h]-[044h] = w, never across the start of a frame (screen.cpp writeStart) */
static void writeStart(uint32_t w)
{
	for (int spin = 0;;)
	{
		while ((*lcdc(0x3A) & 0x80) && spin < VNDP_SPIN)
			++spin;

		uint32_t psw;
		__asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
		if (!(*lcdc(0x3A) & 0x80) || spin >= VNDP_SPIN)
		{
			*(volatile uint16_t *)lcdc(0x42) = (uint16_t)w;
			*lcdc(0x44) = (uint8_t)(w >> 16);
			__asm__ volatile("mov %0,psw\n\tnop" ::"d"(psw) : "memory");
			return;
		}

		__asm__ volatile("mov %0,psw\n\tnop" ::"d"(psw) : "memory");
	}
}

void videoOwn(void)
{
	if (!owned)
		saved40 = *lcdc(0x40);

	*lcdc(0x70) = 0; // the ink layer off (the firmware keeps it so)

	// our frame black, the screen all to be written
	volatile uint16_t *p = (volatile uint16_t *)(FB_BASE + FRAME);
	for (uint32_t i = 0; i < PITCH * LCD_H / 2; i++)
		p[i] = 0;
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
		shown[i] = (pixel_t)~video.frameBuffer[i];
	for (int k = 0; k < 2; k++)
		for (int i = 0; i < SIDE_W * LCD_H; i++)
			sideShown[k][i] = (pixel_t)~side[k][i];

	*lcdc(0x1E0) = 0;
	for (int i = 0; i < 256; i++)
		lutWrite(i);

	*lcdc(0x46) = (uint8_t)(PITCH / 2);
	*lcdc(0x47) = (uint8_t)((PITCH / 2) >> 8);
	*lcdc(0x40) = (uint8_t)((saved40 & ~7) | 3); // 8 bits a pixel, SwivelView kept
	// the LCD is turned 180 degrees (SwivelView): the start address is the frame's last word (screen.cpp showLines)
	writeStart((FRAME + (uint32_t)(LCD_H - 1) * PITCH) / 2 + PITCH / 2 - 1);
	owned = true;
}

pixel_t *videoSide(int k) { return side[k]; }

/* the 16-bit words of a w x LCD_H buffer that changed, to display memory from column x0 */
static void present(const pixel_t *now8, pixel_t *was8, int w, int x0, void (*service)(void))
{
	const uint32_t *now = (const uint32_t *)now8;
	uint32_t *was = (uint32_t *)was8;
	for (int y = 0; y < LCD_H; y++)
	{
		if (service && (y & 7) == 7)
			service();

		volatile uint16_t *dst = (volatile uint16_t *)(FB_BASE + FRAME + (uint32_t)y * PITCH + (uint32_t)x0);
		const uint32_t *n = now + y * (w / 4);
		uint32_t *w32 = was + y * (w / 4);
		for (int x = 0; x < w / 4; x++)
		{
			const uint32_t a = n[x], b = w32[x];
			if (a == b)
				continue;

			if ((a ^ b) & 0xFFFF)
				dst[2 * x] = (uint16_t)a;
			if ((a ^ b) >> 16)
				dst[2 * x + 1] = (uint16_t)(a >> 16);
			w32[x] = a;
		}
	}
}

void videoPresent(void (*service)(void))
{
	present(video.frameBuffer, shown, SCREEN_W, PT_LEFT, service);
	present(side[0], sideShown[0], SIDE_W, 0, service);
	present(side[1], sideShown[1], SIDE_W, PT_LEFT + SCREEN_W, service);
}
