/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * kn7pt.h -- what the KN7000 side of the ProTracker port (src/kn7/) shares: the platform (KN7APP.BIN, through the
 * PfHost it hands PT.BIN), the LCD, Paula on the effects DSP, the sample bank, the screen. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "pfhost.h"

extern const PfHost* host;  /* main.c */

/* ---- paula.c: Paula's four voices on the effects DSP's mixer --------------------------------------------- */
#define PAULA_DSP_FIRST 4      /* the DSP voices the four channels play on: 4-7 */
#define DSP_RATE 44100         /* the DSP's frames a second */
void paulaReset(void);
/* The DSP frames since the last tick have passed: each voice's position moves on, and a voice that has come to the end
 * of what it was playing goes on with what AUDxLC and AUDxLEN hold now, as Paula does (before the next tick). */
void paulaAdvance(uint32_t frames);
/* After a tick: what the replayer changed, to the DSP (PfHost::dspSet, then dspFlush). */
void paulaFlush(void);
/* The samples (song->sampleData, song->sampleBytes) are in the DSP's SDRAM from its word `base`: the voices may play. 0:
 * not there (the voices stay silent). */
void paulaSetBank(uint32_t base, bool ready);
/* For the scopes and the VU meters: what voice ch plays and where it is (bytes, 16.16), or false when it is silent. */
bool paulaVoice(int ch, const int8_t** data, uint32_t* length, uint32_t* pos, uint32_t* volume);

/* ---- main.c ---------------------------------------------------------------------------------------------------- */
uint8_t *kn7Scratch(uint32_t *bytes);  /* the block the module's file is read into, free once it is parsed */

/* ---- format.c: numbers into text, without newlib's stdio (it would add 45 KB to PT.BIN) -------------------- */
char *fmtDec(char *out, int v, int width, bool zeros, bool sign);  /* -> the end of what it wrote ('\0' there) */
char *fmtHex(char *out, uint32_t v, int digits);
char *fmtStr(char *out, const char *s, int width);                 /* s, cut or padded with spaces to width (0: as is) */

/* ---- bank.c: the samples coded for the DSP's decoder, sent through PfHost::dspSend ------------------------- */
/* 1 when song's samples are in the DSP's SDRAM, 2 when not (no sound). */
int bankSend(void);

/* ---- video.c: the LCD at 8 bits a pixel ----------------------------------------------------------------- */
#define LCD_W 640
#define LCD_H 240
#define PT_LEFT 160            /* ProTracker's 320 x 240 screen, in the middle of the LCD */
typedef uint8_t pixel_t;
pixel_t kn7Colour(uint32_t rgb24);  /* the look-up table's entry for a colour (made the first time) */
void videoInit(void);               /* the frame buffer (video.frameBuffer) and the colours */
void videoOwn(void);                /* the LCD controller at 8 bits a pixel, showing our frame, all of it drawn again */
/* Writes to the LCD the pixels of the frame buffer that changed since the last call; `service` is called every 8 rows
 * (the music goes on). */
void videoPresent(void (*service)(void), bool sides);  /* sides: the side columns too */
#define SIDE_W 160             /* the columns left and right of ProTracker's screen */
pixel_t* videoSide(int k);          /* their buffers (0 left, 1 right), SIDE_W x LCD_H, written by videoPresent */

/* ---- ui.c: ProTracker's main screen ------------------------------------------------------------------------ */
extern void (*uiService)(void);  /* called between the screen's parts (main.c: the ticks that are due) */
void uiInit(void);
void uiFullRedraw(void);     /* the tracker frame and everything on it, at the next uiUpdate */
void uiUpdate(void);         /* what changed, into the frame buffer (pt2-clone's renderFrame for the main screen) */
bool uiDrawSides(bool now);  /* sides.c: the columns beside the screen (or HELP over the whole LCD), ten times a
                                second (now: at once); true when it drew */
void uiMessage(const char* s);  /* the status line (pt2-clone's setStatusMessage) */
