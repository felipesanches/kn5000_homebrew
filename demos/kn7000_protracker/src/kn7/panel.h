/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * panel.h -- the KN7000's panel switches, by name (panel.c). Each is a byte of the switch bitmap (PfHost::panel: the
 * CPR board's segments at 0-9, the CPL and CPC boards' at 0x30 + segment) and a bit, as the MAME driver's input ports
 * kn7000_cpanel.cpp name them; their places on the panel are the owner's manual's (QQTG0671 pp. 10-11). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum
{
	/* beside the LCD */
	C_LCDL1, C_LCDL2, C_LCDL3, C_LCDL4, C_LCDL5, C_LCDR1, C_LCDR2, C_LCDR3, C_LCDR4, C_LCDR5,
	/* under it: the 16 MUTE UP/DOWN pairs (C_MUTE_UP1 + n, C_MUTE_DOWN1 + n) */
	C_MUTE_UP1, C_MUTE_DOWN1 = C_MUTE_UP1 + 16,
	C_PAGE_UP = C_MUTE_DOWN1 + 16, C_PAGE_DOWN, C_DISPLAY_HOLD, C_EXIT,
	C_OTHER_PARTS, C_HELP,
	/* right of it */
	C_TRANSPOSE_DOWN, C_TRANSPOSE_UP, C_OCTAVE_DOWN, C_OCTAVE_UP,
	C_PART_LEFT, C_PART_RIGHT2, C_PART_RIGHT1, C_COND_LEFT, C_COND_RIGHT2, C_COND_RIGHT1,
	C_TECHNI_CHORD, C_SOLO,
	/* the SOUND GROUP: its top row, then its bottom row, left to right */
	C_SG_PIANO, C_SG_GUITAR, C_SG_MALLET, C_SG_WORLD, C_SG_STRINGS, C_SG_BRASS, C_SG_SAX, C_SG_ORGAN_ACC, C_SG_EXPLORER,
	C_SG_DRAWBAR, C_SG_ORGAN_TABS, C_SG_ACC_REG, C_SG_PAD, C_SG_SYNTH, C_SG_BASS, C_SG_DRUMS, C_SG_MEMORY, C_SG_EW,
	/* PART EFFECT, GLOBAL EFFECT */
	C_SUSTAIN, C_DIGITAL_EFFECT, C_SOUND_DSP, C_EFFECT_VARIATION, C_CHORUS, C_MULTI, C_REVERB, C_MIC,
	/* SEQUENCER, DISK, SD */
	C_SEQ_PLAY, C_EASY_REC, C_DISK, C_PROGRAM_MENUS, C_SD,
	/* PANEL MEMORY */
	C_PM1, C_PM2, C_PM3, C_PM4, C_PM5, C_PM6, C_PM7, C_PM8, C_PM_SET, C_BANK_VIEW, C_NEXT_BANK,
	C_CUSTOM_PANEL, C_CUSTOMIZE, C_FAVORITES,
	/* left of the LCD: the RHYTHM GROUP, top row then bottom row */
	C_RG_8BEAT, C_RG_ROCK, C_RG_BALLAD, C_RG_JAZZ, C_RG_BALLROOM, C_RG_MOVIE, C_RG_ENTERTAINER, C_RG_ORGANIST,
	C_RG_60S, C_RG_DANCE, C_RG_SOUL, C_RG_COUNTRY, C_RG_MARCH, C_RG_LATIN, C_RG_CUSTOM, C_RG_MEMORY,
	C_APC_MODE, C_APC_ONOFF, C_SA_SET, C_SA_ONOFF, C_MUSIC_STYLIST, C_MSA, C_ONE_TOUCH, C_SPLIT_POINT,
	C_FADE_IN, C_FADE_OUT, C_TAP_TEMPO, C_SYNCHRO,
	C_VAR1, C_VAR2, C_VAR3, C_VAR4, C_FILL1, C_FILL2, C_INTRO1, C_INTRO2, C_START_STOP,
	C_PAD1, C_PAD2, C_PAD3, C_PAD4, C_PAD5, C_PAD6, C_PADS_AUTO, C_PADS_BANK, C_PADS_STOP, C_DEMO,
	C_COUNT
};

void panelInit(void);
void panelUpdate(void);           /* once a pass of the main loop: edges and repeats from PfHost::panel */
bool panelPressed(int c);         /* went down since the last update (or repeats, for a control that repeats) */
bool panelReleased(int c);        /* came up since the last update */
bool panelDown(int c);            /* is held */
uint32_t panelHeldMs(int c);      /* how long it has been held, 0 when it is not */
bool panelShift(void);            /* DISPLAY HOLD held: the second function of a control */
const char *panelName(int c);     /* its name on the panel */
