/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * bank.c -- the module's samples into the effects DSP's SDRAM. The DSP's mixer has a decoder for Another World's
 * lossless code (technics-anotherworld kn7000/tools/dsp_bank.py), and the host port moves one 16-bit half a DSP frame,
 * so the samples go coded: about 60 % of their bytes. bankEncode is dsp_bank.encode in C, block for block and bit for
 * bit (tests/bank/ checks it against the Python on the PC):
 *   * the bank is the samples as song->sampleData holds them (each from a multiple of 32 bytes: the mixer's COPY_STEP),
 *     four 8-bit samples a 32-bit word, the first in the low byte;
 *   * it is cut into blocks of whole words, each at most STAGE_HALF DM words once coded: two header words (the first
 *     word's offset from the bank's start, the words), then the bit stream, 32 bits a word, most significant first;
 *   * in a block each sample is predicted by the one before it (the first by 0): d = (v - previous) mod 256 as
 *     -128..127, folded to z = 2d (d >= 0) or -2d - 1;
 *   * every GROUP samples a 3-bit k, the one that codes the group in the fewest bits (the smallest of equals); then
 *     each z as Rice(k): q = z >> k zero bits, a one and z's low k bits; when q >= QMAX: QMAX zero bits and z in 8 bits.
 * The records PfHost::dspSend takes are dsp_bank.card_files' (u32 n, then the block's n words). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef BANK_HOST_TEST
#include "kn7pt.h"
#include "pt2_structs.h"
#endif

#define STAGE_HALF 128
#define HEADER 2
#define GROUP 64
#define QMAX 12
#define KBITS 3
#define LIMIT ((STAGE_HALF - HEADER) * 32)

static inline uint32_t zOf(uint8_t v, uint8_t prev)
{
	const int32_t d = (int8_t)(uint8_t)(v - prev);
	return d >= 0 ? (uint32_t)(2 * d) : (uint32_t)(-2 * d - 1);
}

static inline uint32_t zCost(uint32_t z, uint32_t k)
{
	const uint32_t q = z >> k;
	return q >= QMAX ? QMAX + 8 : q + 1 + k;
}

typedef struct bits_t
{
	uint32_t *out, acc, n;
} bits_t;

static void put(bits_t *b, uint32_t value, uint32_t bits)
{
	while (bits--)
	{
		b->acc = (b->acc << 1) | ((value >> bits) & 1);
		if (++b->n == 32)
		{
			*b->out++ = b->acc;
			b->acc = b->n = 0;
		}
	}
}

/* One block: words [w, w + n) of the bank -> its DM words at out (header included); their number. */
static uint32_t encodeBlock(const uint8_t *bank, uint32_t w, uint32_t n, uint32_t *out)
{
	bits_t b = { out + HEADER, 0, 0 };
	out[0] = w;
	out[1] = n;
	const uint8_t *s = bank + 4 * w;
	const uint32_t samples = 4 * n;
	uint8_t prev = 0;
	for (uint32_t g = 0; g < samples; g += GROUP)
	{
		const uint32_t end = g + GROUP < samples ? g + GROUP : samples;
		uint32_t cost[8] = { 0 };
		uint8_t p = prev;
		for (uint32_t i = g; i < end; i++)
		{
			const uint32_t z = zOf(s[i], p);
			for (uint32_t k = 0; k < 8; k++)
				cost[k] += zCost(z, k);
			p = s[i];
		}

		uint32_t k = 0;
		for (uint32_t j = 1; j < 8; j++)
			if (cost[j] < cost[k])
				k = j;

		put(&b, k, KBITS);
		for (uint32_t i = g; i < end; i++)
		{
			const uint32_t z = zOf(s[i], prev);
			prev = s[i];
			const uint32_t q = z >> k;
			if (q >= QMAX)
			{
				put(&b, 0, QMAX);
				put(&b, z, 8);
			}
			else
			{
				put(&b, 0, q);
				put(&b, 1, 1);
				put(&b, z & ((1u << k) - 1), k);
			}
		}
	}

	if (b.n)
		*b.out++ = b.acc << (32 - b.n);

	return (uint32_t)(b.out - out);
}

/* The bank (`words` words) coded, as records (u32 n, then the block's n words) at out -> the bytes written. out must
 * hold bankEncodeMax(words) bytes. */
uint32_t bankEncode(const uint8_t *bank, uint32_t words, uint32_t *out)
{
	uint32_t *o = out, w = 0;
	while (w < words)
	{
		// as many words as fit the block once coded: dsp_bank.encode's measure, group by group
		uint32_t n = 0, done = 0, ksum[8] = { 0 };
		uint8_t prev = 0;
		while (w + n < words)
		{
			uint32_t trial[8];
			memcpy(trial, ksum, sizeof (trial));
			uint8_t p = prev;
			const uint8_t *s = bank + 4 * (w + n);
			for (int i = 0; i < 4; i++)
			{
				const uint32_t z = zOf(s[i], p);
				for (uint32_t k = 0; k < 8; k++)
					trial[k] += zCost(z, k);
				p = s[i];
			}

			uint32_t least = trial[0];
			for (int k = 1; k < 8; k++)
				if (trial[k] < least)
					least = trial[k];

			if (done + KBITS + least > LIMIT)
				break;

			n++;
			prev = p;
			memcpy(ksum, trial, sizeof (ksum));
			if ((4 * n) % GROUP == 0)
			{
				done += KBITS + least;
				memset(ksum, 0, sizeof (ksum));
			}
		}

		const uint32_t len = encodeBlock(bank, w, n, o + 1);
		o[0] = len;
		o += 1 + len;
		w += n;
	}

	return (uint32_t)(o - out) * 4;
}

uint32_t bankEncodeMax(uint32_t words)
{
	// at most 9 bits a sample (k = 7) and 3 a group, plus a record's three words for every 100 words or fewer
	return words * 5 + (words / 100 + 1) * 16 + 64;
}

/* dsp_bank.checksum: c = rotl(c, 1) xor w over the bank's words, as the DSP computes it over its SDRAM */
uint32_t bankChecksum(const uint8_t *bank, uint32_t words)
{
	uint32_t c = 0;
	for (uint32_t i = 0; i < words; i++)
	{
		const uint32_t w = bank[4 * i] | (bank[4 * i + 1] << 8) | (bank[4 * i + 2] << 16) | ((uint32_t)bank[4 * i + 3] << 24);
		c = ((c << 1) | (c >> 31)) ^ w;
	}

	return c;
}

#ifndef BANK_HOST_TEST
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	while (n--)
	{
		c ^= *p++;
		for (int k = 0; k < 8; ++k)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
	}

	return ~c;
}

int bankSend(void)
{
	paulaSetBank(host->dspBase, false);
	if (!host->dspVoices || song == NULL || song->sampleData == NULL)
		return 2;

	const uint32_t words = song->sampleBytes / 4;
	if (words > host->dspMaxWords)
		return 2;

	const uint8_t *bank = (const uint8_t *)song->sampleData;
	uint32_t *records = (uint32_t *)malloc(bankEncodeMax(words));
	if (records == NULL)
		return 2;

	const uint32_t bytes = bankEncode(bank, words, records);
	const uint32_t id = crc32(bank, words * 4) ^ words;
	const int r = host->dspSend(records, bytes, words, bankChecksum(bank, words), id);
	free(records);
	paulaSetBank(host->dspBase, r == 1);
	return r;
}
#endif
