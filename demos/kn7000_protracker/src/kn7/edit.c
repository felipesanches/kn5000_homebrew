/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * edit.c -- ProTracker's pattern editing on the KN7000 (PLAN-EDITING.md): what pt2-clone's keyboard handler does on the
 * Amiga's keys (pt2_keyboard.c, pt2_edit.c), as functions the panel calls (controls.c):
 *   * the cursor: 24 columns (note, sample hi/lo, command, parameter hi/lo, for each channel), the row;
 *   * a note: pt2-clone's jamAndPlaceSample (the current sample plays on the cursor's channel; in the edit mode the note
 *     and the sample go into the pattern and the cursor moves on by the edit skip); a chord on the keybed: its notes
 *     into the cursor's channel and the ones after it, on the same row; MULTI: each note to the next channel;
 *   * a hex digit: handleEditKeys for the non-note columns; delete: Del (the note and sample, or the command and its
 *     parameter), Shift+Del (all of the cell);
 *   * rows: insert (Shift+Return), delete (pulls the rows below up), in the track or, with all, in the pattern;
 *   * transpose a note, a track, a pattern by semitones; cut, copy, paste a track or a pattern (F3-F5); undo;
 *   * the effect macros (Ctrl+0-9 store, Alt+0-9 recall); a rest (the cursor on by the edit skip); step play.
 * Every change to a pattern keeps a copy of it first, for undo (pt2-clone's saveUndo). */
#include <stddef.h>
#include <string.h>

#include "kn7pt.h"
#include "edit.h"
#include "pt2_structs.h"
#include "pt2_tables.h"
#include "pt2_replayer.h"
#include "pt2_paula.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"

#define KEYBED_C1 36  /* MIDI note of the key that is ProTracker's C-1 */

void updateSpectrumAnalyzer(uint8_t vol, uint16_t period);  /* ui.c */

bool editChord, editVelocity;   /* AUTO PLAY CHORD ON/OFF; the velocity as Cxx (the options) */
static note_t trackBuffer[MOD_ROWS], patternBuffer[MOD_ROWS * PAULA_VOICES], undoBuffer[MOD_ROWS * PAULA_VOICES];
static int undoPattern = -1;

bool editing(void)
{
	return editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD;
}

/* ---- the cursor (pt2_keyboard.c movePatCur*) ---------------------------------------------------------------------- */
static void cursorSet(int pos)
{
	cursor.pos = (uint8_t)((pos + 24) % 24);
	cursor.channel = cursor.pos / 6;
	cursor.mode = cursor.pos % 6;
	ui.updatePatternData = true;
}

void editCursorRight(void) { cursorSet(cursor.pos + 1); }
void editCursorLeft(void) { cursorSet(cursor.pos - 1); }
void editCursorNextChannel(void) { cursorSet(((cursor.pos / 6 + 1) % 4) * 6); }
void editCursorChannel(int ch) { cursorSet(ch * 6); }

void editRowMove(int d)
{
	if (editor.songPlaying && editor.currMode != MODE_EDIT)
		return; // (the replayer has the row)

	modSetPos(DONT_SET_ORDER, (song->currRow + d) & 63);
}

void editRowSet(int row)
{
	if (editor.songPlaying)
		return;

	modSetPos(DONT_SET_ORDER, row & 63);
}

/* ---- the pattern being edited ------------------------------------------------------------------------------- */
static note_t *pattern(void)
{
	return kn7PatternForEdit(song, song->currPattern);
}

static void saveUndo(void)
{
	note_t *p = pattern();
	if (p == NULL)
		return;

	memcpy(undoBuffer, p, sizeof (undoBuffer));
	undoPattern = song->currPattern;
}

static void changed(void)
{
	song->modified = true;
	ui.updatePatternData = true;
}

static void advance(void)
{
	if (editor.currMode != MODE_RECORD)
		modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);
}

void editUndo(void)
{
	if (undoPattern < 0)
	{
		displayErrorMsg("NOTHING TO UNDO !");
		return;
	}

	note_t *p = kn7PatternForEdit(song, undoPattern);
	if (p == NULL)
		return;

	for (int i = 0; i < MOD_ROWS * PAULA_VOICES; i++)
	{
		const note_t t = undoBuffer[i];
		undoBuffer[i] = p[i];
		p[i] = t;
	}

	modSetPattern((uint8_t)undoPattern);
	changed();
	displayMsg("UNDONE");
}

/* ---- notes --------------------------------------------------------------------------------------------------- */
static int noteOf(int midiNote)
{
	int noteVal = midiNote - KEYBED_C1;
	while (noteVal < 0)
		noteVal += 12;
	while (noteVal > 35)
		noteVal -= 12;
	return noteVal;
}

/* pt2-clone's jamAndPlaceSample: play the current sample at noteVal on channel chNum, and in the edit mode write it */
static void placeNote(int noteVal, int chNum, int velocity, bool move)
{
	moduleChannel_t *ch = &song->channels[chNum];
	moduleSample_t *s = &song->samples[editor.currSample];

	const int16_t tempPeriod = periodTable[((s->fineTune & 0xF) * 37) + noteVal];
	const uint16_t cleanPeriod = periodTable[noteVal];
	editor.currPlayNote = noteVal;

	ch->n_samplenum = editor.currSample;
	ch->n_volume = s->volume;
	if (editVelocity && velocity > 0)
		ch->n_volume = (int8_t)((s->volume * velocity) / 127);
	ch->n_period = tempPeriod;
	ch->n_start = &song->sampleData[s->offset];
	ch->n_length = (uint16_t)((s->loopStart > 0) ? (s->loopStart + s->loopLength) >> 1 : s->length >> 1);
	ch->n_loopstart = &song->sampleData[s->offset + s->loopStart];
	ch->n_replen = (uint16_t)(s->loopLength >> 1);
	if (ch->n_length == 0)
		ch->n_length = 1;

	const uint32_t voiceAddr = 0xDFF0A0 + (chNum * 16);
	paulaWriteWord(voiceAddr + 8, ch->n_volume);
	paulaWriteWord(voiceAddr + 6, ch->n_period);
	paulaWritePtr(voiceAddr + 0, ch->n_start);
	paulaWriteWord(voiceAddr + 4, ch->n_length);
	if (!editor.muted[chNum])
		paulaWriteWord(0xDFF096, 0x8000 | ch->n_dmabit); // voice DMA on
	else
		paulaWriteWord(0xDFF096, ch->n_dmabit); // voice DMA off
	paulaWritePtr(voiceAddr + 0, ch->n_loopstart);
	paulaWriteWord(voiceAddr + 4, ch->n_replen);
	paulaFlush();

	if (!editor.songPlaying)
		editor.vuMeterVolumes[chNum] = vuMeterHeights[ch->n_volume > 64 ? 64 : ch->n_volume];

	if (editing())
	{
		if (move)
			saveUndo();
		note_t *p = pattern();
		if (p != NULL)
		{
			note_t *note = &p[(song->currRow * PAULA_VOICES) + chNum];
			note->sample = editor.sampleZero ? 0 : (editor.currSample + 1);
			note->period = cleanPeriod;
			if (editVelocity && velocity > 0)
			{
				note->command = 0xC;
				note->param = (uint8_t)((s->volume * velocity) / 127);
			}
			changed();
			if (move)
				advance();
		}
	}

	updateSpectrumAnalyzer(s->volume, tempPeriod); // PT quirk: even if the channel is muted
}

/* The keybed's note-ons since the last pass: one note on the cursor's channel (MULTI: and the cursor to the next
 * channel); a chord (several at once, with AUTO PLAY CHORD on) on the cursor's channel and the next ones, one row. */
void editNotes(const uint8_t *notes, const uint8_t *vels, int n)
{
	if (song == NULL || n <= 0)
		return;

	if (editChord && n > 1)
	{
		if (editing())
			saveUndo();
		for (int i = 0; i < n && i < PAULA_VOICES; i++)
			placeNote(noteOf(notes[i]), (cursor.channel + i) & 3, vels[i], false);
		if (editing())
			advance();
		return;
	}

	for (int i = 0; i < n; i++)
	{
		placeNote(noteOf(notes[i]), cursor.channel & 3, vels[i], true);
		if (editor.multiFlag)
			editCursorNextChannel();
	}
}

/* ---- hex digits and deletes (pt2_edit.c handleEditKeys) --------------------------------------------------------- */
void editHex(int key)
{
	if (!editing())
	{
		displayErrorMsg("NOT IN EDIT MODE");
		return;
	}

	if (cursor.mode == CURSOR_NOTE)
		return; // (the keybed enters notes)

	saveUndo();
	note_t *p = pattern();
	if (p == NULL)
		return;

	note_t *note = &p[(song->currRow * PAULA_VOICES) + cursor.channel];
	switch (cursor.mode)
	{
		case CURSOR_SAMPLE1:
			if (key >= 2)
				return; // (31 samples: the high digit is 0 or 1)
			note->sample = (uint8_t)((note->sample & 0x0F) | (key << 4));
			break;
		case CURSOR_SAMPLE2: note->sample = (uint8_t)((note->sample & 0xF0) | key); break;
		case CURSOR_CMD: note->command = (uint8_t)key; break;
		case CURSOR_PARAM1: note->param = (uint8_t)((note->param & 0x0F) | (key << 4)); break;
		case CURSOR_PARAM2: note->param = (uint8_t)((note->param & 0xF0) | key); break;
		default: return;
	}

	changed();
	advance();
}

/* Del: on the note, the note and its sample; elsewhere, the command and its parameter. Shift+Del (all): the cell. */
void editDelete(bool all)
{
	if (!editing())
		return;

	saveUndo();
	note_t *p = pattern();
	if (p == NULL)
		return;

	note_t *note = &p[(song->currRow * PAULA_VOICES) + cursor.channel];
	if (all || cursor.mode == CURSOR_NOTE)
	{
		note->period = 0;
		note->sample = 0;
	}
	if (all || cursor.mode != CURSOR_NOTE)
	{
		note->command = 0;
		note->param = 0;
	}

	changed();
	advance();
}

/* ---- rows ---------------------------------------------------------------------------------------------------- */
/* An empty row at the cursor, the rest of the track (all: of every track) one row down; the last row is lost. */
void editInsertRow(bool all)
{
	if (!editing())
		return;

	saveUndo();
	note_t *p = pattern();
	if (p == NULL)
		return;

	for (int ch = 0; ch < PAULA_VOICES; ch++)
	{
		if (!all && ch != cursor.channel)
			continue;
		for (int r = 62; r >= song->currRow; r--)
			p[((r + 1) * PAULA_VOICES) + ch] = p[(r * PAULA_VOICES) + ch];
		memset(&p[(song->currRow * PAULA_VOICES) + ch], 0, sizeof (note_t));
	}

	changed();
}

/* The row at the cursor removed, the rest of the track (all: of every track) one row up, an empty row at the end. */
void editDeleteRow(bool all)
{
	if (!editing())
		return;

	saveUndo();
	note_t *p = pattern();
	if (p == NULL)
		return;

	for (int ch = 0; ch < PAULA_VOICES; ch++)
	{
		if (!all && ch != cursor.channel)
			continue;
		for (int r = song->currRow; r < 63; r++)
			p[(r * PAULA_VOICES) + ch] = p[((r + 1) * PAULA_VOICES) + ch];
		memset(&p[(63 * PAULA_VOICES) + ch], 0, sizeof (note_t));
	}

	changed();
}

/* ---- transposing (pt2_edit.c trackNoteUp and the like, every sample's notes) ----------------------------------- */
static void transposeNote(note_t *note, int semitones)
{
	if (note->period == 0)
		return;

	for (int i = 0; i < 36; i++)
	{
		if (periodTable[i] == note->period)
		{
			const int j = i + semitones;
			if (j >= 0 && j < 36)
				note->period = periodTable[j];
			return;
		}
	}
}

/* scope: 0 the note at the cursor, 1 the cursor's track, 2 the pattern */
void editTranspose(int scope, int semitones)
{
	if (!editing())
	{
		displayErrorMsg("NOT IN EDIT MODE");
		return;
	}

	saveUndo();
	note_t *p = pattern();
	if (p == NULL)
		return;

	if (scope == 0)
	{
		transposeNote(&p[(song->currRow * PAULA_VOICES) + cursor.channel], semitones);
	}
	else
	{
		for (int r = 0; r < MOD_ROWS; r++)
			for (int ch = 0; ch < PAULA_VOICES; ch++)
				if (scope == 2 || ch == cursor.channel)
					transposeNote(&p[(r * PAULA_VOICES) + ch], semitones);
	}

	changed();
}

/* ---- cut, copy, paste (F3-F5 with Shift: the track, with Alt: the pattern) ------------------------------------- */
void editBuffer(int op, bool wholePattern)  /* op: 0 cut, 1 copy, 2 paste */
{
	note_t *p = pattern();
	if (p == NULL)
		return;

	if (op != 1)
	{
		if (!editing())
		{
			displayErrorMsg("NOT IN EDIT MODE");
			return;
		}
		saveUndo();
	}

	if (wholePattern)
	{
		if (op == 2)
			memcpy(p, patternBuffer, sizeof (patternBuffer));
		else
			memcpy(patternBuffer, p, sizeof (patternBuffer));
		if (op == 0)
			memset(p, 0, sizeof (patternBuffer));
	}
	else
	{
		for (int r = 0; r < MOD_ROWS; r++)
		{
			note_t *n = &p[(r * PAULA_VOICES) + cursor.channel];
			if (op == 2)
			{
				*n = trackBuffer[r];
			}
			else
			{
				trackBuffer[r] = *n;
				if (op == 0)
					memset(n, 0, sizeof (note_t));
			}
		}
	}

	if (op != 1)
		changed();
	static const char *const done[2][3] = { { "TRACK CUT", "TRACK COPIED", "TRACK PASTED" },
	                                        { "PATTERN CUT", "PATTERN COPIED", "PATTERN PASTED" } };
	displayMsg(done[wholePattern][op]);
}

/* ---- effect macros (Ctrl+n stores the command and parameter under the cursor, Alt+n puts it there) -------------- */
void editMacro(int slot, bool store)
{
	note_t *p = pattern();
	if (p == NULL || slot < 0 || slot > 9)
		return;

	note_t *note = &p[(song->currRow * PAULA_VOICES) + cursor.channel];
	if (store)
	{
		editor.effectMacros[slot] = (uint16_t)((note->command << 8) | note->param);
		displayMsg("COMMAND STORED!");
		return;
	}

	if (!editing())
	{
		displayErrorMsg("NOT IN EDIT MODE");
		return;
	}

	saveUndo();
	note->command = editor.effectMacros[slot] >> 8;
	note->param = editor.effectMacros[slot] & 0xFF;
	changed();
	advance();
}

/* ---- a rest, step play, the modulation wheel -------------------------------------------------------------------- */
void editRest(void)
{
	if (editing())
		modSetPos(DONT_SET_ORDER, (song->currRow + (editor.editMoveAdd ? editor.editMoveAdd : 1)) & 63);
}

/* pt2_keyboard.c's Return: the row at the cursor played, the cursor on to the next */
void editStepPlay(void)
{
	editor.stepPlayEnabled = true;
	editor.stepPlayBackwards = false;
	editor.stepPlayLastMode = editor.currMode;
	doStopIt(true);
	playPattern(song->currRow);
}

/* The parameter under the cursor from the modulation wheel (0-127 to 00-FE), while the cursor is on a parameter. */
void editModWheel(int value)
{
	if (!editing() || (cursor.mode != CURSOR_PARAM1 && cursor.mode != CURSOR_PARAM2))
		return;

	note_t *p = pattern();
	if (p == NULL)
		return;

	note_t *note = &p[(song->currRow * PAULA_VOICES) + cursor.channel];
	const uint8_t v = (uint8_t)(value * 2);
	if (note->param != v)
	{
		note->param = v;
		changed();
	}
}

/* SHIFT and the wheel: the value under the cursor a step up or down (the note a semitone, a digit by one). */
void editNudge(int d)
{
	if (!editing())
		return;

	saveUndo();
	note_t *p = pattern();
	if (p == NULL)
		return;

	note_t *note = &p[(song->currRow * PAULA_VOICES) + cursor.channel];
	switch (cursor.mode)
	{
		case CURSOR_NOTE: transposeNote(note, d); break;
		case CURSOR_SAMPLE1:
		case CURSOR_SAMPLE2:
		{
			int s = note->sample + (cursor.mode == CURSOR_SAMPLE1 ? 16 * d : d);
			note->sample = (uint8_t)(s < 0 ? 0 : s > 31 ? 31 : s);
		}
		break;
		case CURSOR_CMD: note->command = (uint8_t)((note->command + d) & 0xF); break;
		case CURSOR_PARAM1: note->param = (uint8_t)(note->param + 16 * d); break;
		case CURSOR_PARAM2: note->param = (uint8_t)(note->param + d); break;
		default: break;
	}

	changed();
}
