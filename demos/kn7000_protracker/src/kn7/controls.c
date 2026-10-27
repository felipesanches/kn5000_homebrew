/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * controls.c -- the KN7000's panel and keybed as ProTracker's keys (PLAN-EDITING.md, section 1, says why each control
 * does what it does; the user manual, manual/, says it for the player). DISPLAY HOLD is SHIFT: held, it gives a control
 * its second function. Three pages of soft keys (main, Disk Op., options); over them, HELP and the questions
 * (SAVE? CLEAR?), which take the controls while they are up. */
#include <stddef.h>
#include <string.h>

#include "kn7pt.h"
#include "panel.h"
#include "edit.h"
#include "pt2_structs.h"
#include "pt2_replayer.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_config.h"

enum { PAGE_MAIN, PAGE_DISK, PAGE_OPTIONS };
enum { ASK_NONE, ASK_SAVE, ASK_SAVE_NEW, ASK_CLEAR, ASK_LOAD };

void restartClock(void);  /* main.c */

static int page = PAGE_MAIN, asking = ASK_NONE, askLoad;
static bool help, macroView, setArmed;
static int helpPage;
static uint32_t setAt;
static int varRow[4] = { 0, 16, 32, 48 };   /* VARIATION 1-4: ProTracker's F6-F9 */
static int32_t wheelWas;
static bool pedalWas;
static int modWas = -1;

/* the soft keys' tags, by page: LCDL 1-5, then LCDR 1-5 */
static const char *const kTags[3][10] =
{
	{ "PLAY", "PATTRN", "STOP", "EDIT", "RECORD", "PREV", "NEXT", "DISKOP", "SAVE", "CLEAR" },
	{ "UP", "DOWN", "LOAD", "SAVE", "SV.NEW", "", "", "", "", "BACK" },
	{ "BLANK0", "DOTS", "#/b", "VELO", "SCOPE", "", "", "", "", "BACK" },
};

const char *controlsTag(int k)
{
	if (asking)
		return k == 4 ? "NO" : k == 9 ? "YES" : "";
	if (page == PAGE_OPTIONS && k < 5)
	{
		static char t[5][8];
		static const char *const n[5] = { "BLANK0", "DOTS", "#/b", "VELO", "SCOPE" };
		const bool on[5] = { config.blankZeroFlag, config.pattDots, config.accidental, editVelocity,
		                     ui.visualizerMode == VISUAL_QUADRASCOPE };
		strcpy(t[k], n[k]);
		strcat(t[k], on[k] ? "+" : "-");
		return t[k];
	}
	return kTags[page][k];
}

bool controlsHelp(int *pageOut) { *pageOut = helpPage; return help; }
bool controlsMacroView(void) { return macroView; }
int controlsPage(void) { return page; }

/* the question being asked, two lines; 0 when none */
bool controlsQuestion(const char **line1, const char **line2)
{
	static char l1[40];
	switch (asking)
	{
		case ASK_SAVE: strcpy(l1, "SAVE OVER "); strcat(l1, diskModuleName(diskModuleAt())); break;
		case ASK_SAVE_NEW: strcpy(l1, "SAVE AS A NEW MODULE"); break;
		case ASK_CLEAR: strcpy(l1, "CLEAR THE SONG"); break;
		case ASK_LOAD: strcpy(l1, "LOAD, LOSING THE EDITS"); break;
		default: return false;
	}
	strcat(l1, "?");
	*line1 = l1;
	*line2 = "START/STOP:YES EXIT:NO";
	return true;
}

static void ask(int what)
{
	asking = what;
	host->wheelLed(1);
}

static void answer(bool yes)
{
	const int what = asking;
	asking = ASK_NONE;
	host->wheelLed(0);
	if (!yes)
	{
		displayMsg("CANCELLED");
		return;
	}

	switch (what)
	{
		case ASK_SAVE: diskSave(false); break;
		case ASK_SAVE_NEW: diskSave(true); break;
		case ASK_CLEAR: songClear(); break;
		case ASK_LOAD: diskLoad(askLoad); break;
		default: break;
	}
}

/* ---- playing ---------------------------------------------------------------------------------------------------- */
static void playSong(void)
{
	restartClock();
	editor.playMode = PLAY_MODE_NORMAL;
	modPlay(DONT_SET_PATTERN, song->currPos, DONT_SET_ROW);
	editor.currMode = MODE_PLAY;
	statusAllRight();
}

static void playPatt(bool record)
{
	restartClock();
	editor.playMode = PLAY_MODE_PATTERN;
	modPlay(song->currPattern, DONT_SET_ORDER, DONT_SET_ROW);
	editor.currMode = record ? MODE_RECORD : MODE_PLAY;
	statusAllRight();
}

static void stop(void)
{
	modStop();
	editor.currMode = MODE_IDLE;
	paulaFlush();
	statusAllRight();
}

static void editToggle(void)
{
	// ProTracker's space: stop, and the edit mode on or off
	const bool on = editor.currMode != MODE_EDIT;
	modStop();
	paulaFlush();
	editor.currMode = on ? MODE_EDIT : MODE_IDLE;
	statusAllRight();
	ui.updatePatternData = true;
}

/* a module from the list: asked first when the one in hand has unsaved edits */
static void loadAndPlay(int index)
{
	if (song != NULL && song->modified)
	{
		askLoad = index;
		ask(ASK_LOAD);
		return;
	}
	if (diskLoad(index))
		playSong();
}

/* ---- the MUTE UP/DOWN pairs (column 1-16) ------------------------------------------------------------------------ */
static const int kLeftFields[2][4] = { { F_POS, F_PATT, F_LEN, F_BPM }, { F_EDIT_PATT, F_PATT, F_LEN, F_SPEED } };
static const int kRightFields[2][4] = { { F_SAMPLE, F_VOLUME, F_FINETUNE, F_SKIP }, { F_REPEAT, F_REPLEN, F_METRO_SPEED, F_METRO_CH } };

int controlsColumnField(int col)  /* columns 0-3 and 12-15: the field each changes now (with SHIFT or not) */
{
	const int s = panelShift() ? 1 : 0;
	return col < 4 ? kLeftFields[s][col] : kRightFields[s][col - 12];
}

static void column(int col, int d)
{
	if (col < 4 || col >= 12)
	{
		songField(controlsColumnField(col), d);
		return;
	}

	// columns 5-12 (0-based 4-11) lie under the four channels' tracks, two each
	const int ch = (col - 4) / 2;
	if ((col - 4) % 2 == 0)
		songMute(ch, d < 0);          // UP: the part on; DOWN: the part off
	else if (d > 0)
		editCursorChannel(ch);        // UP: the cursor there
	else
		songSolo(ch);                 // DOWN: solo
}

/* ---- each pass of the main loop -------------------------------------------------------------------------------- */
void controlsUpdate(void)
{
	panelUpdate();
	host->pollNotes();
	uint8_t notes[16], vels[16];
	const int n = (int)host->noteOns(notes, vels, 16);
	const int32_t wheel = *host->wheel, dw = wheel - wheelWas;
	wheelWas = wheel;
	const bool shift = panelShift();
	host->wheelLed(asking || shift);

	if (help)
	{
		if (panelPressed(C_PAGE_DOWN) || dw > 0)
			helpPage++;
		if (panelPressed(C_PAGE_UP) || dw < 0)
			helpPage--;
		helpPage = helpPage < 0 ? 0 : helpPage > 4 ? 4 : helpPage;  /* (sides.c HELP_PAGES) */
		if (panelPressed(C_HELP) || panelReleasedAfter(C_EXIT))
		{
			help = false;
			uiFullRedraw();
		}
		return;
	}

	if (asking)
	{
		if (panelPressed(C_START_STOP) || panelPressed(C_PM_SET) || panelPressed(C_LCDR5))
			answer(true);
		else if (panelReleasedAfter(C_EXIT) || panelPressed(C_LCDL5))
			answer(false);
		return;
	}

	// the keybed, the pedal, the modulation wheel
	if (n > 0)
		editNotes(notes, vels, n);
	const bool pedal = *host->sustain != 0;
	if (pedal && !pedalWas)
		editRest();
	pedalWas = pedal;
	const int mod = *host->modWheel;
	if (modWas >= 0 && mod != modWas)
		editModWheel(mod);
	modWas = mod;

	// the TEMPO/PROGRAM wheel: the rows; while the song plays (not editing), the tempo; with SHIFT, the value
	if (dw)
	{
		if (page == PAGE_DISK)
			diskKey(dw > 0 ? 1 : -1);
		else if (shift)
			editNudge(dw > 0 ? 1 : -1);
		else if (editor.songPlaying && editor.currMode == MODE_PLAY)
			songField(F_BPM, dw);
		else
			editRowMove(-dw);
	}

	// EXIT, briefly: ProTracker's Esc (the page back, the edit mode off); held, the platform ends the program
	const uint32_t exitHeld = panelReleasedAfter(C_EXIT);
	if (exitHeld && exitHeld < 1000)
	{
		if (page != PAGE_MAIN)
			page = PAGE_MAIN;
		else if (macroView)
			macroView = false;
		else if (editing())
			editToggle();
	}

	if (panelPressed(C_HELP))
	{
		help = true;
		helpPage = 0;
		return;
	}

	// the soft keys
	if (page == PAGE_MAIN)
	{
		if (panelPressed(C_LCDL1)) playSong();
		if (panelPressed(C_LCDL2)) playPatt(false);
		if (panelPressed(C_LCDL3)) stop();
		if (panelPressed(C_LCDL4)) editToggle();
		if (panelPressed(C_LCDL5)) playPatt(true);
		if (panelPressed(C_LCDR1)) loadAndPlay((diskModuleAt() + diskModuleCount() - 1) % diskModuleCount());
		if (panelPressed(C_LCDR2)) loadAndPlay((diskModuleAt() + 1) % diskModuleCount());
		if (panelPressed(C_LCDR3)) page = PAGE_DISK;
		if (panelPressed(C_LCDR4)) ask(ASK_SAVE);
		if (panelPressed(C_LCDR5)) ask(ASK_CLEAR);
	}
	else if (page == PAGE_DISK)
	{
		if (panelPressed(C_LCDL1)) diskKey(-1);
		if (panelPressed(C_LCDL2)) diskKey(+1);
		if (panelPressed(C_LCDL3)) { page = PAGE_MAIN; loadAndPlay(diskModuleAt()); }
		if (panelPressed(C_LCDL4)) ask(ASK_SAVE);
		if (panelPressed(C_LCDL5)) ask(ASK_SAVE_NEW);
		if (panelPressed(C_LCDR5)) page = PAGE_MAIN;
		if (panelPressed(C_PAGE_UP)) diskKey(-16);
		if (panelPressed(C_PAGE_DOWN)) diskKey(+16);
	}
	else
	{
		if (panelPressed(C_LCDL1)) { config.blankZeroFlag = !config.blankZeroFlag; ui.updatePatternData = true; }
		if (panelPressed(C_LCDL2)) { config.pattDots = !config.pattDots; ui.updatePatternData = true; }
		if (panelPressed(C_LCDL3)) { config.accidental = !config.accidental; ui.updatePatternData = true; }
		if (panelPressed(C_LCDL4)) editVelocity = !editVelocity;
		if (panelPressed(C_LCDL5) || panelPressed(C_OTHER_PARTS))
		{
			ui.visualizerMode = ui.visualizerMode == VISUAL_QUADRASCOPE ? VISUAL_SPECTRUM : VISUAL_QUADRASCOPE;
			uiFullRedraw();
		}
		if (panelPressed(C_LCDR5)) page = PAGE_MAIN;
	}

	if (panelPressed(C_DISK) || panelPressed(C_SD)) page = page == PAGE_DISK ? PAGE_MAIN : PAGE_DISK;
	if (panelPressed(C_CUSTOMIZE)) page = page == PAGE_OPTIONS ? PAGE_MAIN : PAGE_OPTIONS;
	if (page != PAGE_OPTIONS && panelPressed(C_OTHER_PARTS))
	{
		ui.visualizerMode = ui.visualizerMode == VISUAL_QUADRASCOPE ? VISUAL_SPECTRUM : VISUAL_QUADRASCOPE;
		uiFullRedraw();
	}

	// the transport: the sequencer is the song, the arranger's controls ProTracker's
	if (panelPressed(C_START_STOP)) { if (editor.songPlaying) stop(); else playSong(); }
	if (panelPressed(C_SEQ_PLAY)) playSong();
	if (panelPressed(C_EASY_REC)) { if (editor.songPlaying) playPatt(true); else editToggle(); }
	if (panelPressed(C_PADS_STOP)) stop();
	if (panelPressed(C_SYNCHRO)) playPatt(false);
	if (panelPressed(C_ONE_TOUCH)) editStepPlay();

	// rows
	const int vars[4] = { C_VAR1, C_VAR2, C_VAR3, C_VAR4 };
	for (int i = 0; i < 4; i++)
		if (panelPressed(vars[i]))
		{
			if (shift)
			{
				varRow[i] = song->currRow;
				displayMsg("POSITION SET");
			}
			else
				editRowSet(varRow[i]);
		}
	if (panelPressed(C_INTRO1)) { if (!editor.songPlaying) { modSetPos(0, 0); modSetPattern((uint8_t)song->header.patternTable[0]); } }
	if (panelPressed(C_INTRO2)) editRowSet(63);
	if (page != PAGE_DISK)
	{
		if (panelPressed(C_PAGE_UP)) editRowMove(song->currRow == 63 ? -15 : -16);
		if (panelPressed(C_PAGE_DOWN)) editRowMove(16);
	}
	if (panelPressed(C_FILL1)) editInsertRow(shift);
	if (panelPressed(C_FILL2)) editDeleteRow(shift);

	// the cursor
	if (panelPressed(C_PART_LEFT)) editCursorLeft();
	if (panelPressed(C_PART_RIGHT1)) editCursorRight();
	if (panelPressed(C_PART_RIGHT2)) editCursorNextChannel();
	if (panelPressed(C_SOLO)) songSolo(cursor.channel);

	// the song's positions and the edited pattern (the CONDUCTOR)
	if (panelPressed(C_COND_LEFT)) songField(F_EDIT_PATT, -1);
	if (panelPressed(C_COND_RIGHT1)) songField(F_EDIT_PATT, +1);
	if (panelPressed(C_COND_RIGHT2)) { if (shift) songDeletePosition(); else songInsertPosition(); }

	// transposing
	if (panelPressed(C_TRANSPOSE_UP)) editTranspose(shift ? 1 : 0, +1);
	if (panelPressed(C_TRANSPOSE_DOWN)) editTranspose(shift ? 1 : 0, -1);
	if (panelPressed(C_OCTAVE_UP)) editTranspose(shift ? 1 : 0, +12);
	if (panelPressed(C_OCTAVE_DOWN)) editTranspose(shift ? 1 : 0, -12);

	// the SOUND GROUP: the hex keypad (0-7 above, 8-F below); its ninth buttons delete
	for (int k = 0; k < 8; k++)
	{
		if (panelPressed(C_SG_PIANO + k)) editHex(k);
		if (panelPressed(C_SG_DRAWBAR + k)) editHex(8 + k);
	}
	if (panelPressed(C_SG_EXPLORER)) editDelete(shift);
	if (panelPressed(C_SG_EW)) editDelete(true);

	// the RHYTHM GROUP: samples 1-16 (SHIFT: 17-31), ProTracker's numeric keypad
	for (int k = 0; k < 16; k++)
		if (panelPressed(C_RG_8BEAT + k))
		{
			const int s = k + (shift ? 16 : 0);
			if (s < MOD_SAMPLES)
			{
				editor.currSample = (int8_t)s;
				updateCurrSample();
			}
		}

	// the PERFORMANCE PADS: cut, copy, paste the track (1-3) and the pattern (4-6); BANK: undo
	const int pads[6] = { C_PAD1, C_PAD2, C_PAD3, C_PAD4, C_PAD5, C_PAD6 };
	for (int k = 0; k < 6; k++)
		if (panelPressed(pads[k]))
			editBuffer(k % 3, k >= 3);
	if (panelPressed(C_PADS_BANK)) editUndo();

	// PANEL MEMORY: the effect macros; SET, then 1-8, stores
	const int pms[8] = { C_PM1, C_PM2, C_PM3, C_PM4, C_PM5, C_PM6, C_PM7, C_PM8 };
	if (panelPressed(C_PM_SET))
	{
		setArmed = true;
		setAt = host->ms();
		displayMsg("SET: PRESS 1-8");
	}
	if (setArmed && host->ms() - setAt > 4000)
		setArmed = false;
	for (int k = 0; k < 8; k++)
		if (panelPressed(pms[k]))
		{
			editMacro(k, setArmed);
			setArmed = false;
		}
	if (panelPressed(C_BANK_VIEW)) macroView = !macroView;

	// entry modes, the metronome, the volume, the tempo
	if (panelPressed(C_APC_ONOFF))
	{
		editChord = !editChord;
		displayMsg(editChord ? "CHORD ENTRY ON" : "CHORD ENTRY OFF");
	}
	if (panelPressed(C_APC_MODE) || panelPressed(C_MULTI))
	{
		editor.multiFlag = !editor.multiFlag;
		ui.updateTrackerFlags = true;
		displayMsg(editor.multiFlag ? "MULTI ON" : "MULTI OFF");
	}
	if (panelPressed(C_SA_ONOFF))
	{
		editor.metroFlag = !editor.metroFlag;
		if (editor.metroFlag && editor.metroChannel == 0)
			editor.metroChannel = (uint16_t)(cursor.channel + 1);
		if (editor.metroSpeed == 0)
			editor.metroSpeed = 4;
		ui.updateTrackerFlags = true;
		displayMsg(editor.metroFlag ? "METRONOME ON" : "METRONOME OFF");
	}
	if (panelPressed(C_SA_SET)) editor.metroChannel = (uint16_t)(cursor.channel + 1);
	if (panelPressed(C_FADE_IN)) songField(F_VOLUME, +1);
	if (panelPressed(C_FADE_OUT)) songField(F_VOLUME, -1);
	if (panelPressed(C_TAP_TEMPO)) songTapTempo();

	// the MUTE UP/DOWN pairs
	for (int col = 0; col < 16; col++)
	{
		if (panelPressed(C_MUTE_UP1 + col)) column(col, +1);
		if (panelPressed(C_MUTE_DOWN1 + col)) column(col, -1);
	}
}

void controlsInit(void)
{
	panelInit();
	wheelWas = *host->wheel;
	editor.metroSpeed = 4;
}
