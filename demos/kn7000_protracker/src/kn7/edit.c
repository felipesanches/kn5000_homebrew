/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * edit.c -- a key of the keybed, as ProTracker's note keys: pt2-clone's jamAndPlaceSample (pt2_edit.c). The current
 * sample plays on the cursor's channel at that note; in the edit mode the note and the sample number go into the
 * pattern at the cursor, and the cursor moves on by the edit skip. The keybed's C2 is ProTracker's C-1: three octaves
 * from there, the keys above and below folded into them by octaves (ProTracker has C-1 to B-3). */
#include <stddef.h>

#include "kn7pt.h"
#include "pt2_structs.h"
#include "pt2_tables.h"
#include "pt2_replayer.h"
#include "pt2_paula.h"
#include "pt2_visuals.h"

#define KEYBED_C1 36  /* MIDI note of the key that is ProTracker's C-1 */

void updateSpectrumAnalyzer(uint8_t vol, uint16_t period);  /* ui.c */

void editNote(int midiNote)
{
	if (song == NULL)
		return;

	int noteVal = midiNote - KEYBED_C1;
	while (noteVal < 0)
		noteVal += 12;
	while (noteVal > 35)
		noteVal -= 12;

	const uint8_t chNum = cursor.channel & 3;
	moduleChannel_t *ch = &song->channels[chNum];
	moduleSample_t *s = &song->samples[editor.currSample];

	const int16_t tempPeriod = periodTable[((s->fineTune & 0xF) * 37) + noteVal];
	const uint16_t cleanPeriod = periodTable[noteVal];
	editor.currPlayNote = noteVal;

	// play the current sample
	ch->n_samplenum = editor.currSample;
	ch->n_volume = s->volume;
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

	// these take effect after the current DMA cycle is done
	paulaWritePtr(voiceAddr + 0, ch->n_loopstart);
	paulaWriteWord(voiceAddr + 4, ch->n_replen);
	paulaFlush();

	if (!editor.songPlaying)
		editor.vuMeterVolumes[chNum] = vuMeterHeights[s->volume > 64 ? 64 : s->volume];

	// insert note and sample number
	if (editor.currMode == MODE_EDIT || editor.currMode == MODE_RECORD)
	{
		note_t *patt = kn7PatternForEdit(song, song->currPattern);
		if (patt != NULL)
		{
			note_t *note = &patt[(song->currRow * PAULA_VOICES) + chNum];
			note->sample = editor.sampleZero ? 0 : (editor.currSample + 1);
			note->period = cleanPeriod;
			if (editor.currMode != MODE_RECORD)
				modSetPos(DONT_SET_ORDER, (song->currRow + editor.editMoveAdd) & 63);
			ui.updatePatternData = true;
			song->modified = true;
		}
	}

	// PT quirk: spectrum analyzer is still handled here even if channel is muted
	updateSpectrumAnalyzer(s->volume, tempPeriod);
}
