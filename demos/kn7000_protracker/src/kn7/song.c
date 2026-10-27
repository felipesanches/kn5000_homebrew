/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * song.c -- the fields of ProTracker's left panel that its up/down arrows change (POS, PATTERN, LENGTH, the BPM, the
 * sample's number, VOLUME, FINETUNE, REPEAT, REPLEN), and the edit skip, the metronome, the edited pattern and the
 * replayer's speed, a step at a time (the MUTE UP/DOWN pairs under the side columns, controls.c); the position
 * editor's insert and delete; CLEAR (the song); TAP TEMPO; mute and solo. */
#include <stddef.h>
#include <string.h>

#include "kn7pt.h"
#include "edit.h"
#include "pt2_structs.h"
#include "pt2_replayer.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"
#include "pt2_paula.h"

static const char *const kNames[F_COUNT] = { "POS", "PATT", "LEN", "BPM", "SMP", "VOL", "FTU", "SKP", "REP", "RPL",
                                             "MSP", "MCH", "SPD", "EDP" };

const char *songFieldName(int field) { return kNames[field]; }

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int songFieldValue(int field)
{
	moduleSample_t *s = &song->samples[editor.currSample];
	switch (field)
	{
		case F_POS: return song->currPos;
		case F_PATT: return song->header.patternTable[song->currPos];
		case F_LEN: return song->header.songLength;
		case F_BPM: return song->currBPM;
		case F_SAMPLE: return editor.currSample + 1;
		case F_VOLUME: return s->volume;
		case F_FINETUNE: return s->fineTune < 8 ? s->fineTune : s->fineTune - 16;
		case F_SKIP: return editor.editMoveAdd;
		case F_REPEAT: return s->loopStart;
		case F_REPLEN: return s->loopLength;
		case F_METRO_SPEED: return editor.metroSpeed;
		case F_METRO_CH: return editor.metroChannel;
		case F_SPEED: return song->currSpeed;
		case F_EDIT_PATT: return song->currPattern;
		default: return 0;
	}
}

void songField(int field, int d)
{
	moduleSample_t *s = &song->samples[editor.currSample];
	switch (field)
	{
		case F_POS:
		{
			const int pos = clamp(song->currPos + d, 0, song->header.songLength - 1);
			if (editor.songPlaying && editor.playMode == PLAY_MODE_NORMAL)
			{
				modPlay(DONT_SET_PATTERN, (int16_t)pos, 0); // the replayer goes there
				break;
			}
			modSetPos((int16_t)pos, DONT_SET_ROW);
			modSetPattern((uint8_t)song->header.patternTable[pos]);
		}
		break;

		case F_PATT:
			song->header.patternTable[song->currPos] = (uint16_t)clamp(song->header.patternTable[song->currPos] + d, 0, MAX_PATTERNS - 1);
			if (!editor.songPlaying)
				modSetPattern((uint8_t)song->header.patternTable[song->currPos]);
			ui.updateSongPattern = ui.updateSongSize = true;
			song->modified = true;
			break;

		case F_LEN:
			song->header.songLength = (uint16_t)clamp(song->header.songLength + d, 1, 128);
			ui.updateSongLength = ui.updateSongSize = true;
			song->modified = true;
			break;

		case F_BPM: modSetTempo(clamp(song->currBPM + d, 32, 255), true); break;
		case F_SPEED: modSetSpeed(clamp(song->currSpeed + d, 1, 31)); break;

		case F_SAMPLE:
			editor.currSample = (int8_t)((editor.currSample + d + MOD_SAMPLES) % MOD_SAMPLES);
			updateCurrSample();
			break;

		case F_VOLUME:
			s->volume = (int8_t)clamp(s->volume + d, 0, 64);
			ui.updateCurrSampleVolume = true;
			song->modified = true;
			break;

		case F_FINETUNE:
		{
			const int ft = clamp(songFieldValue(F_FINETUNE) + d, -8, 7);
			s->fineTune = (uint8_t)(ft & 0xF);
			ui.updateCurrSampleFineTune = true;
			song->modified = true;
		}
		break;

		case F_SKIP:
			editor.editMoveAdd = (uint8_t)clamp(editor.editMoveAdd + d, 0, 9);
			ui.updateTrackerFlags = true;
			break;

		case F_REPEAT:
		case F_REPLEN:
		{
			// in words, as ProTracker keeps them; the loop stays inside the sample
			int start = s->loopStart, len = s->loopLength;
			if (field == F_REPEAT)
				start = clamp(start + 2 * d, 0, s->length - len > 0 ? s->length - len : 0);
			else
				len = clamp(len + 2 * d, 2, s->length - start > 2 ? s->length - start : 2);
			s->loopStart = start;
			s->loopLength = len;
			ui.updateCurrSampleRepeat = ui.updateCurrSampleReplen = true;
			updatePaulaLoops();
			song->modified = true;
		}
		break;

		case F_METRO_SPEED: editor.metroSpeed = (uint16_t)clamp(editor.metroSpeed + d, 1, 64); break;
		case F_METRO_CH:
			editor.metroChannel = (uint16_t)clamp(editor.metroChannel + d, 0, 4);
			editor.metroFlag = editor.metroChannel > 0;
			ui.updateTrackerFlags = true;
			break;

		case F_EDIT_PATT:
			if (editor.songPlaying)
				break;
			if (d > 0)
				incPatt();
			else
				decPatt();
			break;

		default: break;
	}
}

/* Pos Ed.'s INSERT: the current position's pattern again after it; DELETE: the current position removed. */
void songInsertPosition(void)
{
	if (song->header.songLength >= 128)
		return;

	for (int i = 127; i > song->currPos; i--)
		song->header.patternTable[i] = song->header.patternTable[i - 1];
	song->header.songLength++;
	ui.updateSongLength = ui.updateSongPattern = ui.updateSongSize = true;
	song->modified = true;
	displayMsg("POSITION INSERTED");
}

void songDeletePosition(void)
{
	if (song->header.songLength <= 1)
		return;

	for (int i = song->currPos; i < 127; i++)
		song->header.patternTable[i] = song->header.patternTable[i + 1];
	song->header.patternTable[127] = 0;
	song->header.songLength--;
	if (song->currPos >= song->header.songLength)
		modSetPos((int16_t)(song->header.songLength - 1), DONT_SET_ROW);
	ui.updateSongLength = ui.updateSongPattern = ui.updateSongSize = true;
	song->modified = true;
	displayMsg("POSITION DELETED");
}

/* CLEAR (song): the patterns and the positions, the samples kept */
void songClear(void)
{
	modStop();
	editor.currMode = MODE_IDLE;
	paulaFlush();
	clearSong();
	song->modified = true;
	displayMsg("SONG CLEARED");
}

/* TAP TEMPO: the BPM from the last taps, a tap a beat (4 rows of the current speed: 4 x speed ticks of 2.5/BPM s) */
void songTapTempo(void)
{
	static uint32_t last, sum;
	static int taps;
	const uint32_t now = host->ms();
	const uint32_t gap = now - last;
	last = now;
	if (gap > 2000 || gap < 100)
	{
		taps = 0;
		sum = 0;
		displayMsg("TAP AGAIN");
		return;
	}

	sum += gap;
	taps++;
	const uint32_t avg = sum / (uint32_t)taps;
	const int bpm = clamp((int)((10000u * (uint32_t)song->currSpeed + avg / 2) / avg), 32, 255);
	modSetTempo(bpm, true);
	if (taps >= 8)
	{
		taps = 0;
		sum = 0;
	}
}

void songMute(int ch, bool mute)
{
	editor.muted[ch] = mute;
	if (mute)
	{
		paulaWriteWord(0xDFF096, 1 << ch); // its voice off now
		paulaFlush();
	}
	renderMuteButtons();
}

/* SOLO: only this channel heard; again: all of them */
void songSolo(int ch)
{
	bool solo = true;
	for (int c = 0; c < PAULA_VOICES; c++)
		solo = solo && editor.muted[c] == (c != ch);

	for (int c = 0; c < PAULA_VOICES; c++)
		songMute(c, solo ? false : c != ch);
}
