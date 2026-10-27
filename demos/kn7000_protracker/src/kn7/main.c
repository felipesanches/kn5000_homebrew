/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * main.c -- PT.BIN: ProTracker (pt2-clone, src/pt2/) on the KN7000, run by KN7APP.BIN (src/app/) on its program task.
 *   * memory: what KN7APP hands it (PfHost::regions) and the rest of its own area; two blocks carved first, the
 *     module's file and then its samples coded for the DSP (scratch), and its samples (kn7SampleArena); the rest is the
 *     heap newlib's malloc grows into, a region after another (the Pinball port's memory.cpp, in C);
 *   * the modules: the files MODS.TXT names, one a line, in the program's folder (or the six of Pinball Fantasies when
 *     there is no MODS.TXT); loaded whole, parsed by pt2-clone's loader, their samples sent to the DSP (bank.c);
 *   * time: the replayer ticks by the kernel's millisecond counter and timer 4 (2,001 counts a millisecond), 50 times
 *     a second at 125 BPM as the CIA does it; Paula's changes go to the DSP after each tick (paula.c). The screen is
 *     drawn between ticks, and its writes to the LCD stop every few rows for the ticks that are due;
 *   * the controls (the soft keys' labels beside the screen, ui.c):
 *       LCDL 1-5   PLAY (song), PATTERN (play it), STOP, EDIT, the scopes or the spectrum analyzer
 *       LCDR 1-5   the module before, after (in MODS.TXT; it plays at once), the sample before, after, every channel
 *                  muted or none
 *       START/STOP play the song / stop (ProTracker's right Amiga key and space)
 *       PAGE UP/DOWN  the song position (while playing: the replayer goes there) or, in the edit mode, the row
 *       the TEMPO/PROGRAM wheel  the row (stopped), or the sample (playing)
 *       MUTE UP 1-4  channel 1-4 muted or not; MUTE DOWN 1-4: the cursor to channel 1-4
 *       the keybed the current sample on the cursor's channel (edit.c); in the edit mode, into the pattern
 *       EXIT held  the end (KN7APP.BIN) */
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "kn7pt.h"
#include "pt2_structs.h"
#include "pt2_tables.h"
#include "pt2_replayer.h"
#include "pt2_module_loader.h"
#include "pt2_bmp.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_visuals_sync.h"

void sinkVisualizerBars(void);                               /* ui.c */
void updateSpectrumAnalyzer(uint8_t vol, uint16_t period);  /* ui.c */
void editNote(int midiNote);                                 /* edit.c */
void setDefaultPalette(void);                                /* pt2_palette.c */

extern char __bss_lo[], __bss_hi[], __init_lo[], __init_hi[], __game_end[];
int pt_entry(const PfHost *h);

/* ---- the header KN7APP.BIN checks (pfhost.h PfGameHeader); mkgame.py fills in the size and the CRC-32 ---- */
__attribute__((section(".header"), used)) const PfGameHeader pt_header = {
	PFGAME_MAGIC, PFHOST_ABI, 0, PT_PROGRAM, (u32)__bss_lo, (u32)__bss_hi, (u32)__init_lo, (u32)__init_hi,
	(u32)__game_end, pt_entry, 0, {0, 0, 0, 0, 0}};

const PfHost *host;

/* ---- memory --------------------------------------------------------------------------------------------------- */
#define MAX_REGIONS 8
static PfRegion region[MAX_REGIONS];
static int regions, heapAt;
static uint32_t brk;
static uint8_t *scratch, *sampleArena;
static uint32_t scratchBytes, sampleArenaBytes;

static void memoryInit(void)
{
	regions = 0;
	const uint32_t own = ((uint32_t)__game_end + 15) & ~15u;
	if (own < GAME_END)
		region[regions++] = (PfRegion){ own, GAME_END };
	for (const PfRegion *r = host->regions; r->hi && regions < MAX_REGIONS; ++r)
		region[regions++] = *r;
}

/* the whole of the region with the most room: its first byte, *bytes its size */
static uint8_t *carveLargest(uint32_t *bytes)
{
	int best = -1;
	for (int i = 0; i < regions; i++)
		if (best < 0 || region[i].hi - region[i].lo > region[best].hi - region[best].lo)
			best = i;
	if (best < 0)
		return NULL;

	const uint32_t at = (region[best].lo + 15) & ~15u;
	*bytes = region[best].hi - at;
	region[best].lo = region[best].hi;
	return (uint8_t *)at;
}

uint8_t *kn7Scratch(uint32_t *bytes)
{
	*bytes = scratchBytes;
	return scratch;
}

int8_t *kn7SampleArena(uint32_t bytes)
{
	if (bytes > sampleArenaBytes)
		return NULL;

	memset(sampleArena, 0, bytes);
	return (int8_t *)sampleArena;
}

/* newlib's malloc grows the heap through here: in the region it is in, or the next one with room */
void *_sbrk(ptrdiff_t n)
{
	const uint32_t bytes = (uint32_t)n;
	while (heapAt < regions)
	{
		if (brk >= region[heapAt].lo && brk + bytes <= region[heapAt].hi)
		{
			const uint32_t at = brk;
			brk += bytes;
			return (void *)at;
		}

		if (++heapAt < regions)
			brk = region[heapAt].lo;
	}

	errno = ENOMEM;
	return (void *)-1;
}

static void fatal(const char *what)
{
	host->lcdRestore();
	host->fill(0, 100, FB_W, 40, 0);
	host->text("PROTRACKER STOPPED:", 8, 104, 0xF800, 0);
	host->text(what, 8, 120, 0xFFFF, 0);
	host->status("PROTRACKER STOPPED: HOLD EXIT TO QUIT");
	for (;;)
		host->yield(100); // until the player holds EXIT (the task ends in the yield)
}

int _write(int f, const void *p, int n) { (void)f; (void)p; return n; }
int _read(int f, void *p, int n) { (void)f; (void)p; (void)n; return 0; }
int _close(int f) { (void)f; return 0; }
int _lseek(int f, int o, int w) { (void)f; (void)o; (void)w; return 0; }
int _fstat(int f, struct stat *s) { (void)f; s->st_mode = S_IFCHR; return 0; }
int _isatty(int f) { (void)f; return 1; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; return -1; }
void _exit(int c) { (void)c; fatal("exit"); for (;;) {} }
void abort(void) { fatal("abort"); for (;;) {} }

/* ---- the fine clock: the kernel's millisecond counter and timer 4's down-counter, 2,001 counts a millisecond (the
 * Pinball port's clock.h) ------------------------------------------------------------------------------------- */
#define TICKS_PER_MS 2001u

static uint32_t stamp(void)
{
	const volatile uint32_t *ms = (const volatile uint32_t *)host->msCounter;
	const volatile uint16_t *bc = (const volatile uint16_t *)0x340010A0u; // TM4BC
	uint32_t a, b;
	uint16_t c;
	int tries = 4;
	do
	{
		a = *ms;
		c = *bc;
		b = *ms;
	} while (a != b && --tries);
	if (c > 2000)
		c = 2000;
	return a * TICKS_PER_MS + (2000u - c);
}

/* ---- the modules ------------------------------------------------------------------------------------------------ */
#define MAX_MODULES 64
static char names[MAX_MODULES][13];
static int moduleCount, moduleAt;
static const char *const pinballModules[] = { "INTRO.MOD", "MOD2.MOD", "TABLE1.MOD", "TABLE2.MOD", "TABLE3.MOD", "TABLE4.MOD" };

static void readModuleList(void)
{
	moduleCount = 0;
	PfPiece piece = { scratch, scratchBytes < 4096 ? scratchBytes : 4096 };
	const s32 got = host->load("MODS.TXT", &piece, 1);
	if (got > 0)
	{
		const char *p = (const char *)scratch, *end = p + got;
		while (p < end && moduleCount < MAX_MODULES)
		{
			while (p < end && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t'))
				p++;
			if (p >= end)
				break;

			int n = 0;
			const bool comment = *p == '#';
			while (p < end && *p != '\r' && *p != '\n')
			{
				if (!comment && n < 12 && *p != ' ' && *p != '\t')
					names[moduleCount][n++] = *p;
				p++;
			}

			if (!comment && n > 0)
				names[moduleCount++][n] = '\0';
		}
	}

	if (moduleCount == 0)
		for (unsigned i = 0; i < sizeof (pinballModules) / sizeof (pinballModules[0]); i++)
			strcpy(names[moduleCount++], pinballModules[i]);
}

static void showProgress(const char *what)
{
	uiMessage(what);
	uiUpdate();
	videoPresent(NULL);
}

/* An empty module (what ProTracker starts with), when a file cannot be loaded. */
static void emptyModule(void)
{
	module_t *m = createEmptyMod();
	if (m == NULL || !kn7AllocSamples(m) || !kn7AllocPatterns(m, 1))
		fatal("out of memory");

	m->header.initialTempo = 125;
	song = m;
	setupLoadedMod();
	song->loaded = true;
}

static bool loadModule(int index)
{
	if (song != NULL)
	{
		modStop();
		editor.currMode = MODE_IDLE;
		editor.playMode = PLAY_MODE_NORMAL;
		paulaFlush();
		song->loaded = false;
		modFree();
	}

	paulaSetBank(host->dspBase, false);
	moduleAt = index;
	const char *name = names[index];
	PfPiece piece = { scratch, scratchBytes };
	const s32 got = host->load(name, &piece, 1);
	module_t *m = got > 0 ? modLoadFromBuffer(scratch, (uint32_t)got) : NULL;
	if (m == NULL)
	{
		const char *why = got > 0 ? ui.statusMessage : "CAN'T FIND FILE !";
		char keep[18];
		strncpy(keep, why, sizeof (keep) - 1);
		keep[sizeof (keep) - 1] = '\0';
		emptyModule();
		uiFullRedraw();
		displayErrorMsg(keep);
		return false;
	}

	song = m;
	setupLoadedMod();
	song->loaded = true;
	uiFullRedraw();
	showProgress("SENDING SAMPLES..");
	if (bankSend() == 1)
		statusAllRight();
	else
		displayErrorMsg(host->dspVoices ? "DSP NOT READY !" : "NO DSP: SILENT !");
	return true;
}

/* ---- the replayer's clock ------------------------------------------------------------------------------------- */
static uint32_t nextTick;
static bool ticking;

static uint32_t bpmOf(void)
{
	const int32_t bpm = song->currBPM;
	return bpm >= 32 && bpm <= 255 ? (uint32_t)bpm : 125;
}

/* After a tick: the VU meters and the spectrum analyzer, from what the replayer noted (pt2-clone's visuals sync). */
static void syncVisuals(void)
{
	moduleChannel_t *ch = song->channels;
	for (int i = 0; i < PAULA_VOICES; i++, ch++)
	{
		if (ch->syncFlags & UPDATE_SPECTRUM_ANALYZER)
			updateSpectrumAnalyzer(ch->syncAnalyzerVolume, ch->syncAnalyzerPeriod);
		if ((ch->syncFlags & UPDATE_VUMETER) && ch->syncVuVolume <= 64)
			editor.vuMeterVolumes[i] = vuMeterHeights[ch->syncVuVolume];
		ch->syncFlags = 0;
	}
}

/* How late the ticks ran (the clock's counts past when each was due): under 1, 2, 5, 10, 20, 50 ms and more; the worst.
 * tests/mame/pt_test.lua reads them. */
uint32_t ptLate[7], ptLateMax;

/* Every tick that is due: the replayer, then Paula's changes to the DSP. Called between pieces of drawing. */
static void service(void)
{
	if (song == NULL || !editor.songPlaying)
	{
		ticking = false;
		return;
	}

	const uint32_t now = stamp();
	if (!ticking)
	{
		ticking = true;
		nextTick = now;
	}

	int n = 0;
	while ((int32_t)(now - nextTick) >= 0 && editor.songPlaying)
	{
		const uint32_t late = now - nextTick;
		static const uint32_t kLateMs[6] = { 1, 2, 5, 10, 20, 50 };
		int b = 0;
		while (b < 6 && late >= kLateMs[b] * TICKS_PER_MS)
			b++;
		ptLate[b]++;
		if (late > ptLateMax)
			ptLateMax = late;

		const uint32_t bpm = bpmOf();
		paulaAdvance((DSP_RATE * 5 / 2 + bpm / 2) / bpm); // the DSP frames of a tick: 2.5 / BPM seconds
		tickReplayer();
		syncVisuals();
		paulaFlush();
		nextTick += (TICKS_PER_MS * 2500u + bpmOf() / 2) / bpmOf();
		if (++n >= 8 && (int32_t)(now - nextTick) > (int32_t)(TICKS_PER_MS * 100))
		{
			nextTick = now; // far behind (the card, a long wait): from now on
			break;
		}
	}

	if (!editor.songPlaying)
		paulaFlush(); // (F00 stopped it: the voices go silent)
}

/* ---- the controls ------------------------------------------------------------------------------------------- */
#define MUTE_UP(c) PB(kMuteSeg[c], kMuteBit[c])
#define MUTE_DOWN(c) PB(kMuteSeg[c], kMuteBit[c] << 1)
static const uint8_t kMuteSeg[4] = { PANEL_CPL + 5, PANEL_CPL + 5, PANEL_CPL + 8, PANEL_CPL + 8 };  /* (KN7FM's MuteTab) */
static const uint8_t kMuteBit[4] = { 0x10, 0x40, 0x01, 0x04 };

static uint8_t panelWas[0x40];
static uint16_t notesWas[128];
static int32_t wheelWas;

static bool pressed(uint32_t b)
{
	const uint32_t seg = b >> 8, bit = b & 0xFF;
	return (host->panel[seg] & bit) && !(panelWas[seg] & bit);
}

static void playSong(void)
{
	editor.playMode = PLAY_MODE_NORMAL;
	modPlay(DONT_SET_PATTERN, song->currPos, DONT_SET_ROW);
	editor.currMode = MODE_PLAY;
	statusAllRight();
}

static void playPatternNow(void)
{
	editor.playMode = PLAY_MODE_PATTERN;
	modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
	editor.currMode = MODE_PLAY;
	statusAllRight();
}

static void stop(void)
{
	modStop();
	editor.currMode = MODE_IDLE;
	paulaFlush();
	statusAllRight();
}

static void setSample(int s)
{
	editor.currSample = (int8_t)((s + MOD_SAMPLES) % MOD_SAMPLES);
	updateCurrSample();
}

static void moveRow(int d)
{
	modSetPos(DONT_SET_ORDER, (song->currRow + d) & 63);
}

static void movePos(int d)
{
	int pos = song->currPos + d;
	if (pos < 0)
		pos = 0;
	if (pos > song->header.songLength - 1)
		pos = song->header.songLength - 1;

	if (editor.songPlaying && editor.playMode == PLAY_MODE_NORMAL)
	{
		modPlay(DONT_SET_PATTERN, (int16_t)pos, 0); // the replayer goes there
		return;
	}

	modSetPos((int16_t)pos, DONT_SET_ROW);
	if (editor.currMode != MODE_PLAY)
		modSetPattern((uint8_t)song->header.patternTable[pos]);
}

static void controls(void)
{
	const volatile uint8_t *panel = host->panel;
	if (panel == NULL)
		return;

	if (pressed(BTN_LCDL1))
		playSong();
	if (pressed(BTN_LCDL2))
		playPatternNow();
	if (pressed(BTN_LCDL3))
		stop();
	if (pressed(BTN_LCDL4))
	{
		// ProTracker's space: stop, and the edit mode on or off
		const bool edit = editor.currMode != MODE_EDIT;
		modStop();
		paulaFlush();
		editor.currMode = edit ? MODE_EDIT : MODE_IDLE;
		statusAllRight();
		ui.updatePatternData = true;
	}
	if (pressed(BTN_LCDL5))
	{
		ui.visualizerMode = ui.visualizerMode == VISUAL_QUADRASCOPE ? VISUAL_SPECTRUM : VISUAL_QUADRASCOPE;
		uiFullRedraw();
	}

	if (pressed(BTN_LCDR1) && moduleCount)
	{
		if (loadModule((moduleAt + moduleCount - 1) % moduleCount))
			playSong();
	}
	if (pressed(BTN_LCDR2) && moduleCount)
	{
		if (loadModule((moduleAt + 1) % moduleCount))
			playSong();
	}
	if (pressed(BTN_LCDR3))
		setSample(editor.currSample - 1);
	if (pressed(BTN_LCDR4))
		setSample(editor.currSample + 1);
	if (pressed(BTN_LCDR5))
	{
		const bool any = editor.muted[0] || editor.muted[1] || editor.muted[2] || editor.muted[3];
		for (int c = 0; c < PAULA_VOICES; c++)
		{
			editor.muted[c] = !any;
			if (!any)
				paulaWriteWord(0xDFF096, 1 << c); // (its voice off now, as the mute button does)
		}
		renderMuteButtons();
		paulaFlush();
	}

	if (pressed(BTN_START_STOP))
	{
		if (editor.currMode == MODE_PLAY)
			stop();
		else
			playSong();
	}

	if (pressed(BTN_PAGE_UP) || pressed(BTN_PAGE_DOWN))
	{
		const int d = pressed(BTN_PAGE_UP) ? -1 : 1;
		if (editor.currMode == MODE_EDIT && !editor.songPlaying)
			moveRow(d);
		else
			movePos(d);
	}

	for (int c = 0; c < PAULA_VOICES; c++)
	{
		if (pressed(MUTE_UP(c)))
		{
			editor.muted[c] = !editor.muted[c];
			if (editor.muted[c])
			{
				paulaWriteWord(0xDFF096, 1 << c);
				paulaFlush();
			}
			renderMuteButtons();
		}

		if (pressed(MUTE_DOWN(c)))
		{
			cursor.channel = (uint8_t)c;
			cursor.pos = (uint8_t)(c * 6);
		}
	}

	for (unsigned i = 0; i < sizeof (panelWas); i++)
		panelWas[i] = panel[i];

	// the wheel: rows while stopped, samples while playing
	if (host->wheel)
	{
		const int32_t w = *host->wheel, d = w - wheelWas;
		wheelWas = w;
		if (d)
		{
			if (editor.songPlaying)
				setSample(editor.currSample + d);
			else
				moveRow(-d);
		}
	}

	// the keybed: a key that went down is a note key
	host->pollNotes();
	for (int n = 0; n < 128; n++)
	{
		const uint16_t now = host->notes[n];
		if (now && !notesWas[n])
			editNote(n);
		notesWas[n] = now;
	}
}

/* ---- the program ------------------------------------------------------------------------------------------------ */
int pt_entry(const PfHost *h)
{
	host = h;
	memoryInit();
	scratch = carveLargest(&scratchBytes);          // the module's file, then its samples coded for the DSP
	sampleArena = carveLargest(&sampleArenaBytes);  // its samples
	heapAt = 0;
	brk = regions ? region[0].lo : 0;
	if (scratch == NULL || sampleArena == NULL)
		fatal("no memory for the module");

	setDefaultPalette();
	videoInit();
	if (video.frameBuffer == NULL || !unpackBMPs())
		fatal("out of memory (the screen)");

	editor.initialTempo = 125;
	editor.initialSpeed = 6;
	editor.editMoveAdd = 1;
	editor.currMode = MODE_IDLE;
	uiInit();
	host->wheelLed(0);
	host->keybedSound(0); // the keybed plays ProTracker's samples, not the instrument's sounds
	paulaReset();

	readModuleList();
	emptyModule();
	videoOwn();
	uiSides();
	setStatusMessage("ALL RIGHT", DO_CARRY);
	if (loadModule(0))
		playSong();

	host->status("PROTRACKER: HOLD EXIT TO QUIT");
	uint32_t vblank = stamp();
	for (;;)
	{
		service();
		controls();
		service();

		// 50 times a second as on a PAL Amiga: the bars sink, the screen is drawn
		const uint32_t now = stamp();
		if (now - vblank >= TICKS_PER_MS * 20)
		{
			vblank += TICKS_PER_MS * 20;
			if (now - vblank >= TICKS_PER_MS * 200)
				vblank = now;
			sinkVisualizerBars();
			uiUpdate();
			videoPresent(service);
		}

		host->yield(1);
	}
}
