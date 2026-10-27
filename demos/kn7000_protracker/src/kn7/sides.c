/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * sides.c -- the LCD's 160 columns either side of ProTracker's screen (PLAN-EDITING.md, section 2), in ProTracker's own
 * font (8 x 5) and colours:
 *   * the soft keys' tags, short, at the outer edge beside each key;
 *   * left: the song's positions (Pos Ed.), the current one boxed; or the effect macros (BANK VIEW);
 *   * right: the 31 samples, the current one boxed; or the Disk Op.'s modules;
 *   * at the bottom of each, the four MUTE UP/DOWN pairs under it: each column's field and value, right above its pair;
 *   * a question (SAVE? CLEAR?) over the top of both;
 *   * HELP: the whole LCD, a page at a time. */
#include <string.h>

#include "kn7pt.h"
#include "edit.h"
#include "pt2_structs.h"
#include "pt2_bmp.h"
#include "pt2_header.h"

const char *controlsTag(int k);
bool controlsQuestion(const char **line1, const char **line2);
bool controlsHelp(int *page);
bool controlsMacroView(void);
int controlsColumnField(int col);
int controlsPage(void);

#define LINE 7                  /* a text line: 5 rows of glyph, 2 between */
#define LIST_TOP 2
#define LIST_BOTTOM 206         /* the lists end above the columns' parameters */
#define PARAM_TOP 208
#define TAG_W 50                /* the soft keys' tags */

static pixel_t cBack, cPanel, cPanelHi, cPanelLo, cText, cList, cMark, cWhite, cAccent;

static void colours(void)
{
	cBack = (pixel_t)video.palette[PAL_BACKGRD];
	cPanel = (pixel_t)video.palette[PAL_GENBKG];
	cPanelHi = (pixel_t)video.palette[PAL_BORDER];
	cPanelLo = (pixel_t)video.palette[PAL_GENBKG2];
	cText = (pixel_t)video.palette[PAL_GENTXT];
	cList = (pixel_t)video.palette[PAL_PATTXT];
	cMark = (pixel_t)video.palette[PAL_PATCURSOR];
	cWhite = kn7Colour(0xFFFFFF);
	cAccent = (pixel_t)video.palette[PAL_QADSCP];
}

/* ---- drawing into a buffer of width w ---------------------------------------------------------------------- */
typedef struct canvas_t { pixel_t *p; int w; } canvas_t;

static void fill(canvas_t c, int x, int y, int w, int h, pixel_t col)
{
	for (int yy = y; yy < y + h && yy < LCD_H; yy++)
		if (yy >= 0)
			memset(&c.p[yy * c.w + (x < 0 ? 0 : x)], col, (x + w > c.w ? c.w : x + w) - (x < 0 ? 0 : x));
}

static void glyph(canvas_t c, int x, int y, char ch, pixel_t col)
{
	if (ch == ' ' || ch == '\0' || x < 0 || x + 8 > c.w || y < 0 || y + 6 > LCD_H)
		return;

	const int h = (ch == 5 || ch == 6) ? FONT_CHAR_H + 1 : FONT_CHAR_H;
	const uint8_t *src = &fontBMP[(ch & 0x7F) << 3];
	for (int yy = 0; yy < h; yy++, src += 127 * FONT_CHAR_W)
		for (int xx = 0; xx < FONT_CHAR_W; xx++)
			if (src[xx])
				c.p[(y + yy) * c.w + x + xx] = col;
}

static void text(canvas_t c, int x, int y, const char *s, pixel_t col)
{
	for (; *s; s++, x += FONT_CHAR_W)
		glyph(c, x, y, *s >= 'a' && *s <= 'z' ? *s - 32 : *s, col);
}

/* a raised box as ProTracker draws its buttons (drawFramework1) */
static void box(canvas_t c, int x, int y, int w, int h, pixel_t face)
{
	fill(c, x, y, w, h, face);
	fill(c, x, y, w, 1, cPanelHi);
	fill(c, x, y, 1, h, cPanelHi);
	fill(c, x, y + h - 1, w, 1, cPanelLo);
	fill(c, x + w - 1, y, 1, h, cPanelLo);
}

/* ---- the parts ---------------------------------------------------------------------------------------------- */
static void tags(canvas_t c, int first, int x)
{
	for (int i = 0; i < 5; i++)
	{
		const char *t = controlsTag(first + i);
		if (!t[0])
			continue;
		const int y = 48 * i + 18;
		box(c, x, y, TAG_W, 11, cPanel);
		text(c, x + (TAG_W - 8 * (int)strlen(t)) / 2, y + 3, t, cText);
	}
}

static void params(canvas_t c, int firstCol)
{
	for (int k = 0; k < 4; k++)
	{
		const int field = controlsColumnField(firstCol + k);
		const int v = songFieldValue(field);
		char val[8];
		if (field == F_REPEAT || field == F_REPLEN)
			fmtHex(val, (uint32_t)v, 4);              // (in bytes, as ProTracker shows them: hex)
		else if (field == F_SAMPLE)
			fmtHex(val, (uint32_t)v, 2);
		else if (field == F_FINETUNE)
			fmtDec(val, v, 0, false, v != 0);
		else if (field == F_PATT || field == F_EDIT_PATT || field == F_POS)
			fmtDec(val, v, 2, true, false);
		else
			fmtDec(val, v, 0, false, false);
		const int x = 40 * k;
		box(c, x, PARAM_TOP, 40, 32, cPanel);
		const char *name = songFieldName(field);
		text(c, x + (40 - 8 * (int)strlen(name)) / 2, PARAM_TOP + 6, name, cText);
		text(c, x + (40 - 8 * (int)strlen(val)) / 2, PARAM_TOP + 18, val, cText);
	}
}

static void positions(canvas_t c, int x, int w)
{
	const int lines = (LIST_BOTTOM - LIST_TOP) / LINE;
	int first = song->currPos - lines / 2;
	if (first > song->header.songLength - lines)
		first = song->header.songLength - lines;
	if (first < 0)
		first = 0;

	for (int i = 0; i < lines; i++)
	{
		const int pos = first + i;
		if (pos >= song->header.songLength)
			break;
		const int y = LIST_TOP + i * LINE;
		char line[16];
		fmtDec(fmtStr(fmtDec(line, pos, 3, true, false), "  ", 0), song->header.patternTable[pos], 2, true, false);
		if (pos == song->currPos)
		{
			fill(c, x, y - 1, w, LINE, cPanel);
			text(c, x + 4, y, line, cText);
		}
		else
		{
			text(c, x + 4, y, line, cList);
		}
	}
}

static void macros(canvas_t c, int x, int w)
{
	text(c, x + 4, LIST_TOP, "MACROS", cAccent);
	for (int i = 0; i < 8; i++)
	{
		char line[16];
		fmtHex(fmtStr(fmtDec(line, i + 1, 0, false, false), "  ", 0), editor.effectMacros[i], 3);
		text(c, x + 4, LIST_TOP + (i + 2) * LINE, line, cList);
	}
	(void)w;
}

static void samples(canvas_t c, int x, int w)
{
	const int lines = (LIST_BOTTOM - LIST_TOP) / LINE;
	int first = editor.currSample - lines / 2;
	if (first > MOD_SAMPLES - lines)
		first = MOD_SAMPLES - lines;
	if (first < 0)
		first = 0;

	for (int i = 0; i < lines && first + i < MOD_SAMPLES; i++)
	{
		const int s = first + i;
		const int y = LIST_TOP + i * LINE;
		char line[16];
		fmtStr(fmtStr(fmtHex(line, (uint32_t)(s + 1), 2), " ", 0), song->samples[s].text, 10);
		if (s == editor.currSample)
		{
			fill(c, x, y - 1, w, LINE, cPanel);
			text(c, x + 2, y, line, cText);
		}
		else
		{
			text(c, x + 2, y, line, song->samples[s].length > 2 ? cList : cPanelLo);
		}
	}
}

static void modules(canvas_t c, int x, int w)
{
	text(c, x + 2, LIST_TOP, "DISK OP.", cAccent);
	const int lines = (LIST_BOTTOM - LIST_TOP) / LINE - 2;
	int first = diskModuleAt() - lines / 2;
	if (first > diskModuleCount() - lines)
		first = diskModuleCount() - lines;
	if (first < 0)
		first = 0;

	for (int i = 0; i < lines && first + i < diskModuleCount(); i++)
	{
		const int y = LIST_TOP + (i + 2) * LINE;
		const int m = first + i;
		if (m == diskModuleAt())
		{
			fill(c, x, y - 1, w, LINE, cPanel);
			text(c, x + 2, y, diskModuleName(m), cText);
		}
		else
		{
			text(c, x + 2, y, diskModuleName(m), cList);
		}
	}
}

static void question(canvas_t l, canvas_t r)
{
	const char *q1, *q2;
	if (!controlsQuestion(&q1, &q2))
		return;

	// across the inner parts of both columns: the question left, the answer right
	box(l, TAG_W + 2, 90, SIDE_W - TAG_W - 2, 40, cMark);
	box(r, 0, 90, SIDE_W - TAG_W - 2, 40, cMark);
	char a[14], b[14];
	fmtStr(a, q1, 13);
	fmtStr(b, strlen(q1) > 13 ? q1 + 13 : "", 13);
	text(l, TAG_W + 6, 100, a, cWhite);
	text(l, TAG_W + 6, 112, b, cWhite);
	text(r, 4, 100, "START: YES", cWhite);
	text(r, 4, 112, "EXIT:  NO", cWhite);
	(void)q2;
}

/* ---- HELP ----------------------------------------------------------------------------------------------------- */
/* the help pages: lines of text across the LCD's 80 columns of 8 dots ('|' starts a heading) */
#define HELP_PAGES 5
static const char *const kHelp[HELP_PAGES][26] =
{
	{
		"|PROTRACKER - HELP 1/5: PLAYING            PAGE UP/DOWN: PAGES, EXIT: BACK",
		"",
		"START/STOP ....... PLAY THE SONG, OR STOP",
		"SEQUENCER PLAY ... PLAY THE SONG",
		"SYNCHRO & BREAK .. PLAY THE PATTERN, OVER AND OVER",
		"EASY REC ......... EDIT MODE ON/OFF; WHILE PLAYING: RECORD",
		"                   (THE PATTERN PLAYS AND THE KEYBED WRITES INTO IT)",
		"ONE TOUCH PLAY ... PLAY ONE ROW AND MOVE ON",
		"PADS STOP ........ STOP",
		"LCDL 1-5 ......... PLAY, PATTERN, STOP, EDIT, RECORD",
		"TEMPO/PROGRAM .... WHILE THE SONG PLAYS: THE TEMPO (BPM)",
		"TAP TEMPO ........ THE TEMPO FROM YOUR TAPS, A TAP A BEAT",
		"",
		"|SHIFT AND EXIT",
		"DISPLAY HOLD IS SHIFT: HOLD IT FOR A CONTROL'S SECOND FUNCTION",
		"(THE TEMPO/PROGRAM WHEEL'S LED BLINKS WHILE IT IS HELD).",
		"EXIT, BRIEFLY: BACK (A PAGE, THE EDIT MODE). HELD 1.5 S: THE END.",
		NULL
	},
	{
		"|HELP 2/5: MOVING",
		"",
		"TEMPO/PROGRAM .... THE ROW, ONE A DETENT",
		"SHIFT + WHEEL .... THE VALUE UNDER THE CURSOR, A STEP",
		"PAGE UP/DOWN ..... 16 ROWS UP OR DOWN",
		"VARIATION 1-4 .... ROWS 0, 16, 32, 48 (SHIFT: STORE THIS ROW THERE)",
		"INTRO & ENDING 1 . THE SONG'S START",
		"INTRO & ENDING 2 . ROW 63",
		"PART SELECT ...... LEFT: THE CURSOR LEFT",
		"                   RIGHT 1: THE CURSOR RIGHT",
		"                   RIGHT 2: THE NEXT CHANNEL",
		"MUTE UP 6/8/10/12  THE CURSOR TO CHANNEL 1/2/3/4 (UNDER ITS TRACK)",
		"SUSTAIN PEDAL .... A REST: THE CURSOR ON BY THE EDIT SKIP",
		NULL
	},
	{
		"|HELP 3/5: ENTERING",
		"",
		"THE KEYBED ....... NOTES: C2 IS PROTRACKER'S C-1, THREE OCTAVES",
		"                   (KEYS ABOVE AND BELOW FOLD INTO THEM)",
		"AUTO PLAY CHORD    OFF/ON: CHORD ENTRY, A CHORD ACROSS THE CHANNELS",
		"                   MODE: MULTI, EACH NOTE TO THE NEXT CHANNEL",
		"MODULATION WHEEL . THE EFFECT PARAMETER UNDER THE CURSOR",
		"",
		"|HEX DIGITS: THE SOUND GROUP",
		" PIANO GUITAR MALLET WORLD STRINGS BRASS SAX ORGAN&ACC | EXPLORER",
		"   0     1      2      3     4      5    6      7     |  DELETE",
		" DRAWBAR TABS ACC.REG PAD SYNTH BASS DRUMS MEMORY      | EW EXP.",
		"   8      9     A      B    C     D     E     F        |  CELL",
		"",
		"|SAMPLES: THE RHYTHM GROUP",
		"TOP ROW: 1-8. BOTTOM ROW: 9-16. WITH SHIFT: 17-31.",
		"FADE IN/OUT ...... THE CURRENT SAMPLE'S VOLUME",
		NULL
	},
	{
		"|HELP 4/5: EDITING",
		"",
		"FILL IN 1 ........ INSERT A ROW IN THE TRACK (SHIFT: THE PATTERN)",
		"FILL IN 2 ........ DELETE A ROW IN THE TRACK (SHIFT: THE PATTERN)",
		"TRANSPOSE -/+ .... THE NOTE A SEMITONE (SHIFT: THE TRACK)",
		"R1/R2 OCTAVE -/+ . THE NOTE AN OCTAVE (SHIFT: THE TRACK)",
		"PADS 1, 2, 3 ..... CUT, COPY, PASTE THE TRACK",
		"PADS 4, 5, 6 ..... CUT, COPY, PASTE THE PATTERN",
		"PADS BANK ........ UNDO",
		"PANEL MEMORY 1-8 . THE EFFECT MACRO INTO THE CELL",
		"SET, THEN 1-8 .... STORE THE CELL'S EFFECT AS THAT MACRO",
		"BANK VIEW ........ SHOW THE MACROS",
		"SOLO ............. THE CURSOR'S CHANNEL ALONE (AGAIN: ALL)",
		"",
		"|THE MUTE UP/DOWN PAIRS",
		"1-4 ....... POS, PATT, LEN, BPM (SHIFT: EDITED PATTERN, PATT, LEN, SPEED)",
		"5,7,9,11 .. CHANNEL 1-4: UP HEARD, DOWN MUTED",
		"6,8,10,12 . CHANNEL 1-4: UP THE CURSOR THERE, DOWN SOLO",
		"13-16 ..... SAMPLE, VOLUME, FINETUNE, EDIT SKIP",
		"            (SHIFT: REPEAT, REPLEN, METRONOME SPEED, METRONOME CHANNEL)",
		NULL
	},
	{
		"|HELP 5/5: THE SONG, MODULES, OPTIONS",
		"",
		"CONDUCTOR LEFT/RIGHT 1  THE EDITED PATTERN, DOWN/UP",
		"CONDUCTOR RIGHT 2 ...... INSERT A POSITION (SHIFT: DELETE IT)",
		"LCDR 1, 2 .............. THE MODULE BEFORE, AFTER (MODS.TXT)",
		"LCDR 3, DISK, SD ....... DISK OP.: THE WHEEL CHOOSES; LOAD, SAVE,",
		"                         SAVE AS NEW (PTNEWnn.MOD)",
		"LCDR 4 ................. SAVE OVER THE MODULE'S FILE",
		"LCDR 5 ................. CLEAR THE SONG (THE SAMPLES STAY)",
		"QUESTIONS .............. START/STOP OR SET: YES. EXIT: NO.",
		"SOUND ARRANGER ......... OFF/ON: METRONOME (SAMPLE 1F)",
		"                         SET: ON THE CURSOR'S CHANNEL",
		"OTHER PARTS/TG ......... QUADRASCOPE OR SPECTRUM ANALYZER",
		"CUSTOMIZE .............. OPTIONS: BLANK ZEROES, DOTS, SHARPS/FLATS,",
		"                         VELOCITY AS Cxx, SCOPE",
		"",
		"|LEFT ALONE",
		"THE VOLUME SLIDERS AND CONTRAST STAY THE INSTRUMENT'S. THE EFFECT BUTTONS,",
		"PROGRAM MENUS, DEMO, MUSIC STYLIST, MUSIC STYLE ARRANGER, SPLIT POINT,",
		"TECHNI-CHORD, CUSTOM PANEL, FAVORITES AND NEXT BANK DO NOTHING HERE.",
		NULL
	},
};

/* text across the LCD: the left column, ProTracker's screen and the right column, by x */
static void wide(canvas_t l, canvas_t m, canvas_t r, int x, int y, const char *s, pixel_t col)
{
	for (; *s; s++, x += 8)
	{
		const char ch = *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
		if (x < SIDE_W)
			glyph(l, x, y, ch, col);
		else if (x < SIDE_W + SCREEN_W)
			glyph(m, x - SIDE_W, y, ch, col);
		else
			glyph(r, x - SIDE_W - SCREEN_W, y, ch, col);
	}
}

static void helpPage(canvas_t l, canvas_t m, canvas_t r, int page)
{
	fill(l, 0, 0, SIDE_W, LCD_H, cBack);
	fill(m, 0, 0, SCREEN_W, LCD_H, cBack);
	fill(r, 0, 0, SIDE_W, LCD_H, cBack);
	for (int i = 0; kHelp[page][i] != NULL; i++)
	{
		const char *s = kHelp[page][i];
		const bool heading = s[0] == '|';
		wide(l, m, r, 4, 4 + i * 9, heading ? s + 1 : s, heading ? cAccent : cWhite);
	}
}

void uiDrawSides(void)
{
	static bool init;
	if (!init)
	{
		colours();
		init = true;
	}

	canvas_t l = { videoSide(0), SIDE_W }, r = { videoSide(1), SIDE_W }, m = { video.frameBuffer, SCREEN_W };
	int page;
	if (controlsHelp(&page))
	{
		helpPage(l, m, r, page);
		return;
	}

	fill(l, 0, 0, SIDE_W, LCD_H, cBack);
	fill(r, 0, 0, SIDE_W, LCD_H, cBack);
	tags(l, 0, 0);
	tags(r, 5, SIDE_W - TAG_W);
	if (song != NULL)
	{
		if (controlsMacroView())
			macros(l, TAG_W + 2, SIDE_W - TAG_W - 2);
		else
			positions(l, TAG_W + 2, SIDE_W - TAG_W - 2);
		if (diskShown() || controlsPage() == 1)
			modules(r, 0, SIDE_W - TAG_W - 2);
		else
			samples(r, 0, SIDE_W - TAG_W - 2);
		params(l, 0);
		params(r, 12);
	}
	question(l, r);
}
