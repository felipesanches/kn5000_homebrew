/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * panel.c -- panel.h: the switches as the platform keeps them from the firmware (PfHost::panel, KN7APP.BIN entry.c),
 * each with its edges; the controls that move something (the cursor, a value) repeat while held, as a computer's
 * keys do: after REPEAT_DELAY_MS, every REPEAT_MS. */
#include "kn7pt.h"
#include "panel.h"

#define REPEAT_DELAY_MS 400
#define REPEAT_MS 70

typedef struct control_t
{
	uint8_t seg, bit;   /* byte of the bitmap, bit */
	bool repeats;
	const char *name;
} control_t;

#define CPL(n) (0x30 + (n))
#define CPC(n) (0x30 + (n))
#define CPR(n) (n)

static const control_t kControls[C_COUNT] =
{
	[C_LCDL1] = { CPL(0), 0x02, false, "LCDL 1" }, [C_LCDL2] = { CPL(0), 0x08, false, "LCDL 2" },
	[C_LCDL3] = { CPL(0), 0x20, false, "LCDL 3" }, [C_LCDL4] = { CPL(0), 0x01, false, "LCDL 4" },
	[C_LCDL5] = { CPL(0), 0x04, false, "LCDL 5" },
	[C_LCDR1] = { CPR(5), 0x10, false, "LCDR 1" }, [C_LCDR2] = { CPR(5), 0x20, false, "LCDR 2" },
	[C_LCDR3] = { CPR(7), 0x01, false, "LCDR 3" }, [C_LCDR4] = { CPR(6), 0x01, false, "LCDR 4" },
	[C_LCDR5] = { CPR(5), 0x01, false, "LCDR 5" },
	/* MUTE UP n, MUTE DOWN n: CPC_SEG5 SW4-7, SEG8-10 SW0-7, SEG11 SW0-3, UP the even switch, DOWN the next */
	[C_MUTE_UP1 + 0] = { CPC(5), 0x10, true, "MUTE UP 1" }, [C_MUTE_DOWN1 + 0] = { CPC(5), 0x20, true, "MUTE DOWN 1" },
	[C_MUTE_UP1 + 1] = { CPC(5), 0x40, true, "MUTE UP 2" }, [C_MUTE_DOWN1 + 1] = { CPC(5), 0x80, true, "MUTE DOWN 2" },
	[C_MUTE_UP1 + 2] = { CPC(8), 0x01, true, "MUTE UP 3" }, [C_MUTE_DOWN1 + 2] = { CPC(8), 0x02, true, "MUTE DOWN 3" },
	[C_MUTE_UP1 + 3] = { CPC(8), 0x04, true, "MUTE UP 4" }, [C_MUTE_DOWN1 + 3] = { CPC(8), 0x08, true, "MUTE DOWN 4" },
	[C_MUTE_UP1 + 4] = { CPC(8), 0x10, true, "MUTE UP 5" }, [C_MUTE_DOWN1 + 4] = { CPC(8), 0x20, true, "MUTE DOWN 5" },
	[C_MUTE_UP1 + 5] = { CPC(8), 0x40, true, "MUTE UP 6" }, [C_MUTE_DOWN1 + 5] = { CPC(8), 0x80, true, "MUTE DOWN 6" },
	[C_MUTE_UP1 + 6] = { CPC(9), 0x01, true, "MUTE UP 7" }, [C_MUTE_DOWN1 + 6] = { CPC(9), 0x02, true, "MUTE DOWN 7" },
	[C_MUTE_UP1 + 7] = { CPC(9), 0x04, true, "MUTE UP 8" }, [C_MUTE_DOWN1 + 7] = { CPC(9), 0x08, true, "MUTE DOWN 8" },
	[C_MUTE_UP1 + 8] = { CPC(9), 0x10, true, "MUTE UP 9" }, [C_MUTE_DOWN1 + 8] = { CPC(9), 0x20, true, "MUTE DOWN 9" },
	[C_MUTE_UP1 + 9] = { CPC(9), 0x40, true, "MUTE UP 10" }, [C_MUTE_DOWN1 + 9] = { CPC(9), 0x80, true, "MUTE DOWN 10" },
	[C_MUTE_UP1 + 10] = { CPC(10), 0x01, true, "MUTE UP 11" }, [C_MUTE_DOWN1 + 10] = { CPC(10), 0x02, true, "MUTE DOWN 11" },
	[C_MUTE_UP1 + 11] = { CPC(10), 0x04, true, "MUTE UP 12" }, [C_MUTE_DOWN1 + 11] = { CPC(10), 0x08, true, "MUTE DOWN 12" },
	[C_MUTE_UP1 + 12] = { CPC(10), 0x10, true, "MUTE UP 13" }, [C_MUTE_DOWN1 + 12] = { CPC(10), 0x20, true, "MUTE DOWN 13" },
	[C_MUTE_UP1 + 13] = { CPC(10), 0x40, true, "MUTE UP 14" }, [C_MUTE_DOWN1 + 13] = { CPC(10), 0x80, true, "MUTE DOWN 14" },
	[C_MUTE_UP1 + 14] = { CPC(11), 0x01, true, "MUTE UP 15" }, [C_MUTE_DOWN1 + 14] = { CPC(11), 0x02, true, "MUTE DOWN 15" },
	[C_MUTE_UP1 + 15] = { CPC(11), 0x04, true, "MUTE UP 16" }, [C_MUTE_DOWN1 + 15] = { CPC(11), 0x08, true, "MUTE DOWN 16" },
	[C_PAGE_UP] = { CPC(11), 0x10, true, "PAGE UP" }, [C_PAGE_DOWN] = { CPC(11), 0x20, true, "PAGE DOWN" },
	[C_DISPLAY_HOLD] = { CPC(11), 0x40, false, "DISPLAY HOLD" }, [C_EXIT] = { CPC(11), 0x80, false, "EXIT" },
	[C_OTHER_PARTS] = { CPC(5), 0x01, false, "OTHER PARTS/TG" }, [C_HELP] = { CPC(5), 0x02, false, "HELP" },
	[C_TRANSPOSE_DOWN] = { CPR(4), 0x01, true, "TRANSPOSE -" }, [C_TRANSPOSE_UP] = { CPR(3), 0x01, true, "TRANSPOSE +" },
	[C_OCTAVE_DOWN] = { CPR(7), 0x02, true, "R1/R2 OCTAVE -" }, [C_OCTAVE_UP] = { CPR(6), 0x02, true, "R1/R2 OCTAVE +" },
	[C_PART_LEFT] = { CPR(2), 0x01, true, "PART SELECT LEFT" }, [C_PART_RIGHT2] = { CPR(2), 0x02, true, "PART SELECT RIGHT 2" },
	[C_PART_RIGHT1] = { CPR(1), 0x02, true, "PART SELECT RIGHT 1" },
	[C_COND_LEFT] = { CPR(5), 0x02, true, "CONDUCTOR LEFT" }, [C_COND_RIGHT2] = { CPR(4), 0x02, false, "CONDUCTOR RIGHT 2" },
	[C_COND_RIGHT1] = { CPR(3), 0x02, true, "CONDUCTOR RIGHT 1" },
	[C_TECHNI_CHORD] = { CPR(1), 0x01, false, "TECHNI-CHORD" }, [C_SOLO] = { CPR(0), 0x01, false, "SOLO" },
	[C_SG_PIANO] = { CPR(4), 0x10, false, "PIANO" }, [C_SG_GUITAR] = { CPR(3), 0x10, false, "GUITAR" },
	[C_SG_MALLET] = { CPR(2), 0x10, false, "MALLET & ORCH PERC" }, [C_SG_WORLD] = { CPR(1), 0x10, false, "WORLD" },
	[C_SG_STRINGS] = { CPR(0), 0x10, false, "STRINGS & VOCAL" }, [C_SG_BRASS] = { CPR(9), 0x08, false, "BRASS" },
	[C_SG_SAX] = { CPR(8), 0x08, false, "SAX & WOODWIND" }, [C_SG_ORGAN_ACC] = { CPR(7), 0x08, false, "ORGAN & ACCORDION" },
	[C_SG_EXPLORER] = { CPR(6), 0x08, true, "SOUND EXPLORER" },
	[C_SG_DRAWBAR] = { CPR(4), 0x20, false, "DIGITAL DRAWBAR" }, [C_SG_ORGAN_TABS] = { CPR(3), 0x20, false, "ORGAN TABS" },
	[C_SG_ACC_REG] = { CPR(2), 0x20, false, "ACCORDION REGISTER" }, [C_SG_PAD] = { CPR(1), 0x20, false, "PAD" },
	[C_SG_SYNTH] = { CPR(0), 0x20, false, "SYNTH" }, [C_SG_BASS] = { CPR(9), 0x04, false, "BASS" },
	[C_SG_DRUMS] = { CPR(8), 0x04, false, "DRUM KITS" }, [C_SG_MEMORY] = { CPR(7), 0x04, false, "MEMORY" },
	[C_SG_EW] = { CPR(6), 0x04, true, "EW EXPANSION" },
	[C_SUSTAIN] = { CPR(5), 0x08, false, "SUSTAIN" }, [C_DIGITAL_EFFECT] = { CPR(4), 0x08, false, "DIGITAL EFFECT" },
	[C_SOUND_DSP] = { CPR(3), 0x08, false, "SOUND DSP" }, [C_EFFECT_VARIATION] = { CPR(2), 0x08, false, "VARIATION (effect)" },
	[C_CHORUS] = { CPR(5), 0x04, false, "CHORUS" }, [C_MULTI] = { CPR(4), 0x04, false, "MULTI" },
	[C_REVERB] = { CPR(3), 0x04, false, "REVERB" }, [C_MIC] = { CPR(2), 0x04, false, "MIC" },
	[C_SEQ_PLAY] = { CPR(1), 0x08, false, "SEQUENCER PLAY" }, [C_EASY_REC] = { CPR(0), 0x08, false, "EASY REC" },
	[C_DISK] = { CPR(1), 0x04, false, "DISK" }, [C_PROGRAM_MENUS] = { CPR(0), 0x04, false, "PROGRAM MENUS" },
	[C_SD] = { CPR(1), 0x80, false, "SD" },
	[C_PM1] = { CPR(0), 0x02, false, "PANEL MEMORY 1" }, [C_PM2] = { CPR(6), 0x40, false, "PANEL MEMORY 2" },
	[C_PM3] = { CPR(5), 0x40, false, "PANEL MEMORY 3" }, [C_PM4] = { CPR(4), 0x40, false, "PANEL MEMORY 4" },
	[C_PM5] = { CPR(3), 0x40, false, "PANEL MEMORY 5" }, [C_PM6] = { CPR(5), 0x80, false, "PANEL MEMORY 6" },
	[C_PM7] = { CPR(6), 0x80, false, "PANEL MEMORY 7" }, [C_PM8] = { CPR(7), 0x80, false, "PANEL MEMORY 8" },
	[C_PM_SET] = { CPR(7), 0x40, false, "SET" }, [C_BANK_VIEW] = { CPR(4), 0x80, false, "BANK VIEW" },
	[C_NEXT_BANK] = { CPR(3), 0x80, false, "NEXT BANK" },
	[C_CUSTOM_PANEL] = { CPR(1), 0x40, false, "CUSTOM PANEL" }, [C_CUSTOMIZE] = { CPR(0), 0x40, false, "CUSTOMIZE" },
	[C_FAVORITES] = { CPR(2), 0x40, false, "FAVORITES" },
	[C_RG_8BEAT] = { CPL(2), 0x80, false, "8 & 16 BEAT" }, [C_RG_ROCK] = { CPL(2), 0x20, false, "ROCK & POP" },
	[C_RG_BALLAD] = { CPL(2), 0x08, false, "BALLAD" }, [C_RG_JAZZ] = { CPL(2), 0x02, false, "JAZZ & SWING" },
	[C_RG_BALLROOM] = { CPL(1), 0x80, false, "BALLROOM" }, [C_RG_MOVIE] = { CPL(1), 0x20, false, "MOVIE & SHOW" },
	[C_RG_ENTERTAINER] = { CPL(1), 0x08, false, "ENTERTAINER" }, [C_RG_ORGANIST] = { CPL(1), 0x02, false, "ORGANIST" },
	[C_RG_60S] = { CPL(2), 0x40, false, "60s & 70s" }, [C_RG_DANCE] = { CPL(2), 0x10, false, "MODERN DANCE" },
	[C_RG_SOUL] = { CPL(2), 0x04, false, "SOUL & R&B" }, [C_RG_COUNTRY] = { CPL(2), 0x01, false, "COUNTRY & WESTERN" },
	[C_RG_MARCH] = { CPL(1), 0x40, false, "MARCH & WALTZ" }, [C_RG_LATIN] = { CPL(1), 0x10, false, "LATIN & WORLD" },
	[C_RG_CUSTOM] = { CPL(1), 0x04, false, "CUSTOM" }, [C_RG_MEMORY] = { CPL(1), 0x01, false, "MEMORY (rhythm)" },
	[C_APC_MODE] = { CPL(7), 0x02, false, "AUTO PLAY CHORD MODE" }, [C_APC_ONOFF] = { CPL(7), 0x08, false, "AUTO PLAY CHORD OFF/ON" },
	[C_SA_SET] = { CPL(7), 0x04, false, "SOUND ARRANGER SET" }, [C_SA_ONOFF] = { CPL(7), 0x10, false, "SOUND ARRANGER OFF/ON" },
	[C_MUSIC_STYLIST] = { CPL(7), 0x01, false, "MUSIC STYLIST" }, [C_MSA] = { CPL(4), 0x08, false, "MUSIC STYLE ARRANGER" },
	[C_ONE_TOUCH] = { CPL(4), 0x02, true, "ONE TOUCH PLAY" }, [C_SPLIT_POINT] = { CPL(3), 0x80, false, "SPLIT POINT" },
	[C_FADE_IN] = { CPL(3), 0x20, true, "FADE IN" }, [C_FADE_OUT] = { CPL(3), 0x08, true, "FADE OUT" },
	[C_TAP_TEMPO] = { CPL(3), 0x02, false, "TAP TEMPO" }, [C_SYNCHRO] = { CPL(0), 0x80, false, "SYNCHRO & BREAK" },
	[C_VAR1] = { CPL(4), 0x10, false, "VARIATION 1" }, [C_VAR2] = { CPL(4), 0x04, false, "VARIATION 2" },
	[C_VAR3] = { CPL(4), 0x01, false, "VARIATION 3" }, [C_VAR4] = { CPL(3), 0x40, false, "VARIATION 4" },
	[C_FILL1] = { CPL(3), 0x10, true, "FILL IN 1" }, [C_FILL2] = { CPL(3), 0x04, true, "FILL IN 2" },
	[C_INTRO1] = { CPL(3), 0x01, false, "INTRO & ENDING 1" }, [C_INTRO2] = { CPL(0), 0x40, false, "INTRO & ENDING 2" },
	[C_START_STOP] = { CPL(0), 0x10, false, "START/STOP" },
	[C_PAD1] = { CPL(6), 0x10, false, "PAD 1" }, [C_PAD2] = { CPL(4), 0x80, false, "PAD 2" },
	[C_PAD3] = { CPL(4), 0x20, false, "PAD 3" }, [C_PAD4] = { CPL(6), 0x04, false, "PAD 4" },
	[C_PAD5] = { CPL(6), 0x01, false, "PAD 5" }, [C_PAD6] = { CPL(4), 0x40, false, "PAD 6" },
	[C_PADS_AUTO] = { CPL(6), 0x20, false, "PADS AUTO SETTING" }, [C_PADS_BANK] = { CPL(6), 0x08, false, "PADS BANK" },
	[C_PADS_STOP] = { CPL(6), 0x02, false, "PADS STOP" }, [C_DEMO] = { CPL(6), 0x40, false, "DEMO" },
};

static bool down[C_COUNT], pressed[C_COUNT], released[C_COUNT];
static uint32_t downAt[C_COUNT], nextRepeat[C_COUNT];

void panelInit(void)
{
	for (int c = 0; c < C_COUNT; c++)
		down[c] = host->panel ? (host->panel[kControls[c].seg] & kControls[c].bit) != 0 : false;
}

void panelUpdate(void)
{
	const uint32_t now = host->ms();
	for (int c = 0; c < C_COUNT; c++)
	{
		const bool is = host->panel ? (host->panel[kControls[c].seg] & kControls[c].bit) != 0 : false;
		pressed[c] = released[c] = false;
		if (is && !down[c])
		{
			pressed[c] = true;
			downAt[c] = now;
			nextRepeat[c] = now + REPEAT_DELAY_MS;
		}
		else if (is && kControls[c].repeats && (int32_t)(now - nextRepeat[c]) >= 0)
		{
			pressed[c] = true;
			nextRepeat[c] = now + REPEAT_MS;
		}
		else if (!is && down[c])
		{
			released[c] = true;
		}
		down[c] = is;
	}
}

bool panelPressed(int c) { return pressed[c]; }
bool panelReleased(int c) { return released[c]; }
bool panelDown(int c) { return down[c]; }
uint32_t panelHeldMs(int c) { return down[c] ? host->ms() - downAt[c] : 0; }
bool panelShift(void) { return down[C_DISPLAY_HOLD]; }
const char *panelName(int c) { return kControls[c].name; }
