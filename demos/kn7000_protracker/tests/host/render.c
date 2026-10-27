/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * render.c MODULE OUT.WAV SECONDS -- what the KN7000 port should sound like, worked out on the PC: the port's own
 * replayer and Paula (src/pt2/, src/kn7/paula.c, built for the PC), ticked 2.5/BPM s a tick as main.c ticks it, and
 * their voice commands played by a model of the effects DSP's mixer (technics-anotherworld kn7000/tools/dsp_mixer.py,
 * unit 0's commands and mix, step for step: per frame, each voice whose POS < END adds its sample (the byte at POS
 * from BASE, sign-extended) times VOL; then POS.FRAC += INC, and past END, POS -= LOOP or the voice stays silent;
 * MIX = sum x GAIN, written here as MIX / 256, 16 bits). The commands of a tick take effect at the frame the tick ends,
 * as one flush. Writes a mono 44,100 Hz WAV. tests/host/README.md says what it is compared with. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kn7pt.h"
#include "pt2_structs.h"
#include "pt2_replayer.h"
#include "pt2_module_loader.h"

#define BANK 0x70000u  /* dsp_mixer.BANK */
#define GAIN 128       /* dsp_mixer.GAIN */

typedef struct mvoice_t { uint32_t pos, frac, base, inc, end, loop, vol; } mvoice_t;
static mvoice_t voice[8];
static uint32_t cmd[8][5], pend;

static void hostSet(u32 v, const PfVoice *p, u32 how)
{
	v &= 7;
	if (how == 0)
		pend = (pend & ~(0x101u << v)) | (0x10000u << v);
	else if (how == 1)
	{
		cmd[v][0] = p->base, cmd[v][1] = p->step, cmd[v][2] = p->end, cmd[v][3] = p->loop, cmd[v][4] = p->volume;
		pend = (pend & ~(0x10101u << v)) | (1u << v);
	}
	else
	{
		cmd[v][1] = p->step, cmd[v][4] = p->volume;
		pend |= 0x100u << v;
	}
}

static void hostFlush(void)
{
	// (the mixer's unit 0 takes MSGR6 at its next frame: here, at once)
	for (int v = 0; v < 8; v++)
	{
		mvoice_t *m = &voice[v];
		if (pend & (0x10000u << v))
			m->end = 0;
		if (pend & (1u << v))
		{
			m->base = cmd[v][0], m->inc = cmd[v][1], m->end = cmd[v][2], m->loop = cmd[v][3], m->vol = cmd[v][4];
			m->pos = m->frac = 0;
		}
		if (pend & (0x100u << v))
			m->inc = cmd[v][1], m->vol = cmd[v][4];
	}
	pend = 0;
}

static int16_t mixFrame(void)
{
	int32_t sum = 0;
	for (int v = 0; v < 8; v++)
	{
		mvoice_t *m = &voice[v];
		if (m->pos >= m->end)
			continue;

		const uint32_t at = (m->base - BANK) * 4 + m->pos;
		const int8_t s = at < song->sampleBytes ? song->sampleData[at] : 0;
		sum += s * (int32_t)m->vol;
		m->frac += m->inc;
		m->pos += m->frac >> 16;
		m->frac &= 0xFFFF;
		if (m->pos >= m->end && m->loop)
			m->pos -= m->loop;
	}

	int32_t out = (sum * GAIN) / 256;
	if (out > 32767) out = 32767;
	if (out < -32768) out = -32768;
	return (int16_t)out;
}

static PfHost hostRec;
const PfHost *host = &hostRec;
static int8_t *arena;
static uint32_t arenaBytes = 0x100000;

int8_t *kn7SampleArena(uint32_t bytes)
{
	if (bytes > arenaBytes)
		return NULL;
	memset(arena, 0, bytes);
	return arena;
}

uint8_t *kn7Scratch(uint32_t *bytes) { *bytes = 0; return NULL; }

/* ui.c's, which the replayer and the loader call */
void renderMuteButtons(void) {}
void updateCurrSample(void) {}
void updateCursorPos(void) {}
void statusAllRight(void) {}
void statusOutOfMemory(void) { fprintf(stderr, "render: out of memory\n"); }
void statusSampleIsEmpty(void) {}
void statusNotSampleZero(void) {}
void changeStatusText(const char *text) { (void)text; }

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "usage: render MODULE OUT.WAV SECONDS\n");
		return 2;
	}

	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	rewind(f);
	uint8_t *buf = malloc(n);
	if (fread(buf, 1, n, f) != (size_t)n)
		return 1;
	fclose(f);

	hostRec.dspVoices = 8;
	hostRec.dspSet = hostSet;
	hostRec.dspFlush = hostFlush;
	hostRec.dspBase = BANK;
	arena = malloc(arenaBytes);

	editor.initialTempo = 125;
	editor.initialSpeed = 6;
	module_t *m = modLoadFromBuffer(buf, (uint32_t)n);
	if (m == NULL)
	{
		fprintf(stderr, "render: %s does not load (%s)\n", argv[1], ui.statusMessage);
		return 1;
	}
	song = m;
	setupLoadedMod();
	song->loaded = true;
	paulaSetBank(BANK, true);

	// RENDER_ONLY=c: channel c alone (the others muted, as the mute buttons do)
	const char *only = getenv("RENDER_ONLY");
	if (only)
		for (int c = 0; c < PAULA_VOICES; c++)
			editor.muted[c] = c != atoi(only);

	// main.c's playSong
	editor.playMode = PLAY_MODE_NORMAL;
	modPlay(DONT_SET_PATTERN, song->currPos, DONT_SET_ROW);
	editor.currMode = MODE_PLAY;

	const uint32_t frames = (uint32_t)(atof(argv[3]) * DSP_RATE);
	int16_t *pcm = calloc(frames, 2);
	uint32_t done = 0, ticks = 0;
	while (done < frames && editor.songPlaying)
	{
		// main.c's service: a tick, then its commands; then the frames until the next
		const uint32_t bpm = (song->currBPM >= 32 && song->currBPM <= 255) ? (uint32_t)song->currBPM : 125;
		const uint32_t tickFrames = (DSP_RATE * 5 / 2 + bpm / 2) / bpm;
		paulaAdvance(tickFrames);
		tickReplayer();
		paulaFlush();
		ticks++;
		const uint32_t next = (DSP_RATE * 5 / 2 + (uint32_t)song->currBPM / 2) / (uint32_t)song->currBPM;
		for (uint32_t i = 0; i < next && done < frames; i++)
			pcm[done++] = mixFrame();
	}

	f = fopen(argv[2], "wb");
	fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 2); fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16); put16(f, 1); put16(f, 1); put32(f, DSP_RATE); put32(f, DSP_RATE * 2); put16(f, 2); put16(f, 16);
	fwrite("data", 1, 4, f); put32(f, frames * 2);
	fwrite(pcm, 2, frames, f);
	fclose(f);
	printf("render: %s, %u ticks, %.1f s\n", argv[1], ticks, done / (double)DSP_RATE);
	return 0;
}
