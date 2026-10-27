/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * paula.c -- the Amiga's Paula, as pt2-clone's replayer drives it (pt2_paula.h: DMACON, AUDxLC, AUDxLEN, AUDxPER,
 * AUDxVOL), played by the KN7000's effects DSP. The DSP runs Another World's eight-voice mixer (KN7APP.BIN dsp.c,
 * technics-anotherworld kn7000/tools/dsp_mixer.py), whose voice plays 8-bit samples from its SDRAM: from a base word,
 * a 16.16 step a frame (44,100 frames a second), to an end, then back by a loop length or silent. That is Paula's DMA
 * with its two pairs of registers folded into one command:
 *   * a voice started (DMACON set) plays what AUDxLC/AUDxLEN held then (A); the replayer then writes the loop into them
 *     (B), which Paula takes when A ends. When B ends where A ends (every sample the replayer starts: its length is the
 *     loop's end), the DSP is told A's start, A's end and B's length as the loop. A B of one word is ProTracker's "no
 *     loop" (the sample's first word, which the loader clears): the DSP voice ends with A;
 *   * any other B (a new sample set without a note, or a loop changed while it plays) the DSP cannot take in advance:
 *     this file follows each voice's position on the CPU, frames times step, as pt2_paula.c does sample by sample, and
 *     when A has ended starts the DSP voice afresh on B, looping it (late by up to a tick);
 *   * AUDxPER and AUDxVOL change the DSP voice's step and volume without a restart.
 * The DSP's voice starts on a whole word (four samples): a start that is not (a loop on an odd word) begins up to three
 * samples early. Paula's own sound (its filters, the A500's low-pass, the LED filter, the stereo of channels 0-3 left,
 * right, right, left) is not modelled: the DSP's mix is mono and unfiltered. */
#include <string.h>

#include "kn7pt.h"
#include "pt2_structs.h"
#include "pt2_paula.h"

#define PAULA_CLK 3546895u   /* PAULA_PAL_CLK: 28375160 / 8 */

typedef struct pvoice_t
{
	bool dma;                            /* DMACON's bit */
	const int8_t *storedLoc;             /* AUDxLC, AUDxLEN as last written */
	uint16_t storedLen;
	uint16_t period;                     /* AUDxPER, AUDxVOL */
	uint8_t vol;
	bool started;                        /* DMA turned on since the last flush */
	const int8_t *loc;                   /* what the voice plays now: from, bytes; how far it has got (16.16 bytes) */
	uint32_t len, pos;
	/* what the DSP voice was told: playing, its loop (start and bytes, or 0), its step and volume */
	bool dspOn;
	const int8_t *dspLoopLoc;
	uint32_t dspLoopLen, dspStep, dspVol;
} pvoice_t;

static pvoice_t v[PAULA_VOICES];
static int8_t nullSample[4];  /* the replayer's "no data": silence */
static uint32_t bankBase;
static bool bankReady;

int8_t *paulaGetNullSamplePtr(void)
{
	return nullSample;
}

void paulaSetBank(uint32_t base, bool ready)
{
	bankBase = base;
	bankReady = ready;
}

void paulaReset(void)
{
	for (int ch = 0; ch < PAULA_VOICES; ch++)
		if (v[ch].dspOn && host->dspVoices)
			host->dspSet(PAULA_DSP_FIRST + ch, NULL, 0);

	memset(v, 0, sizeof (v));
	if (host->dspVoices)
		host->dspFlush();
}

static uint32_t stepOf(uint16_t period)
{
	uint32_t p = period;
	if (p == 0)
		p = 65536; // on the Amiga, period 0 is period 65536
	else if (p < 113)
		p = 113;

	return (uint32_t)(((uint64_t)PAULA_CLK << 16) / ((uint64_t)DSP_RATE * p));
}

static uint32_t segmentBytes(uint16_t words)
{
	return words ? words * 2u : 0x20000u; // AUDxLEN 0 is 65536 words
}

/* true when [loc, loc + len) lies in the samples the DSP holds */
static bool inBank(const int8_t *loc, uint32_t len)
{
	if (!bankReady || song == NULL || song->sampleData == NULL)
		return false;

	return loc >= song->sampleData && loc + len <= song->sampleData + song->sampleBytes;
}

static void dspStop(int ch)
{
	pvoice_t *p = &v[ch];
	if (p->dspOn && host->dspVoices)
		host->dspSet(PAULA_DSP_FIRST + ch, NULL, 0);

	p->dspOn = false;
}

/* The DSP voice plays [loc, loc + len) from its start, then loops the `loopLen` bytes that end where it ends (0: no loop). */
static void dspStart(int ch, const int8_t *loc, uint32_t len, uint32_t loopLen)
{
	pvoice_t *p = &v[ch];
	if (!inBank(loc, len) || len == 0)
	{
		dspStop(ch);
		return;
	}

	const uint32_t at = (uint32_t)(loc - song->sampleData);
	const uint32_t early = at & 3; // (the DSP's voice starts on a word)
	PfVoice cmd;
	cmd.base = bankBase + (at >> 2);
	cmd.position = 0;
	cmd.step = stepOf(p->period);
	cmd.end = early + len;
	cmd.loop = loopLen;
	cmd.volume = p->vol;
	if (host->dspVoices)
		host->dspSet(PAULA_DSP_FIRST + ch, &cmd, 1);

	p->dspOn = true;
	p->dspLoopLoc = loopLen ? loc + len - loopLen : NULL;
	p->dspLoopLen = loopLen;
	p->dspStep = cmd.step;
	p->dspVol = cmd.volume;
}

static void startDMA(int ch)
{
	pvoice_t *p = &v[ch];
	p->loc = p->storedLoc ? p->storedLoc : nullSample;
	p->len = segmentBytes(p->storedLen);
	p->pos = 0;
	p->dma = true;
	p->started = true;
}

static void stopDMA(int ch)
{
	v[ch].dma = false;
	v[ch].started = false;
}

void paulaWriteByte(uint32_t address, uint8_t data8)
{
	(void)address; // (CIA-A's LED filter: not modelled)
	(void)data8;
}

void paulaWriteWord(uint32_t address, uint16_t data16)
{
	if (address == 0xDFF096) // DMACON
	{
		for (int ch = 0; ch < PAULA_VOICES; ch++)
		{
			if (!(data16 & (1 << ch)))
				continue;

			if (data16 & 0x8000)
				startDMA(ch);
			else
				stopDMA(ch);
		}

		return;
	}

	if (address < 0xDFF0A0 || address > 0xDFF0D9)
		return;

	pvoice_t *p = &v[(address - 0xDFF0A0) >> 4];
	switch (address & 0xF)
	{
		case 0x4: p->storedLen = data16; break;  // AUDxLEN
		case 0x6: p->period = data16; break;     // AUDxPER
		case 0x8:                                // AUDxVOL
		{
			const uint8_t vol = data16 & 127;
			p->vol = vol > 64 ? 64 : vol;
		}
		break;

		default: break;
	}
}

void paulaWritePtr(uint32_t address, const int8_t *ptr)
{
	if (address < 0xDFF0A0 || address > 0xDFF0D0 || (address & 0xF) != 0)
		return;

	v[(address - 0xDFF0A0) >> 4].storedLoc = ptr ? ptr : nullSample;
}

void paulaAdvance(uint32_t frames)
{
	for (int ch = 0; ch < PAULA_VOICES; ch++)
	{
		pvoice_t *p = &v[ch];
		if (!p->dma || p->len == 0)
			continue;

		p->pos += stepOf(p->period) * frames;
		while ((p->pos >> 16) >= p->len)
		{
			// the end of what it played: Paula takes AUDxLC and AUDxLEN again
			p->pos -= p->len << 16;
			p->loc = p->storedLoc;
			p->len = segmentBytes(p->storedLen);
			if (p->len <= 2)
			{
				// ProTracker's "no loop": its one word is silence (the loader cleared it)
				if (p->dspOn && p->dspLoopLen)
					dspStop(ch);
				p->pos &= 0xFFFF;
				break;
			}

			if (p->loc != p->dspLoopLoc || p->len != p->dspLoopLen)
			{
				// a loop the DSP was not told of: start it there, looping
				dspStart(ch, p->loc, p->len, p->len);
			}
		}
	}
}

void paulaFlush(void)
{
	for (int ch = 0; ch < PAULA_VOICES; ch++)
	{
		pvoice_t *p = &v[ch];
		if (!p->dma)
		{
			dspStop(ch);
			continue;
		}

		if (p->started)
		{
			p->started = false;

			// what it plays first (A), and the loop the replayer has set for when A ends (B)
			const uint32_t lenB = segmentBytes(p->storedLen);
			uint32_t loop = 0;
			if (lenB > 2 && p->storedLoc + lenB == p->loc + p->len)
				loop = lenB;

			dspStart(ch, p->loc, p->len, loop);
			if (!loop)
				p->dspLoopLoc = NULL; // (paulaAdvance starts B, if it is a loop, when A ends)
			continue;
		}

		if (!p->dspOn)
			continue;

		const uint32_t step = stepOf(p->period);
		if (step != p->dspStep || p->vol != p->dspVol)
		{
			PfVoice cmd;
			memset(&cmd, 0, sizeof (cmd));
			cmd.step = step;
			cmd.volume = p->vol;
			if (host->dspVoices)
				host->dspSet(PAULA_DSP_FIRST + ch, &cmd, 2);

			p->dspStep = step;
			p->dspVol = p->vol;
		}
	}

	if (host->dspVoices)
		host->dspFlush();
}

bool paulaVoice(int ch, const int8_t **data, uint32_t *length, uint32_t *pos, uint32_t *volume)
{
	const pvoice_t *p = &v[ch];
	if (!p->dma || p->len <= 2 || p->loc == nullSample)
		return false;

	*data = p->loc;
	*length = p->len;
	*pos = p->pos;
	*volume = p->vol;
	return true;
}
