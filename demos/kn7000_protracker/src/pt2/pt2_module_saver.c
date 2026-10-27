#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include "pt2_structs.h"
#include "pt2_mouse.h"
#include "pt2_textout.h"
#include "pt2_helpers.h"
#include "pt2_sampler.h"
#include "pt2_config.h"
#include "pt2_askbox.h"

#ifdef PT2_KN7
/* KN7000: the module, exactly as modSave below writes it, into `buf` (at most `max` bytes) -> its bytes, or 0 when it
** does not fit. main.c hands the bytes to PfHost::save. */
typedef struct out_t { uint8_t *p, *end; } out_t;

static void out8(out_t *o, uint8_t v)
{
	if (o->p < o->end)
		*o->p = v;
	o->p++;
}

static void outN(out_t *o, const void *src, uint32_t n)
{
	const uint8_t *s = (const uint8_t *)src;
	while (n--)
		out8(o, *s++);
}

static void out16be(out_t *o, uint16_t v)
{
	out8(o, (uint8_t)(v >> 8));
	out8(o, (uint8_t)v);
}

uint32_t modSaveToBuffer(uint8_t *buf, uint32_t max)
{
	out_t o = { buf, buf + max };

	outN(&o, song->header.name, 20);

	for (int32_t i = 0; i < MOD_SAMPLES; i++)
	{
		moduleSample_t *s = &song->samples[i];

		outN(&o, s->text, 22);
		out16be(&o, (uint16_t)(s->length >> 1));
		out8(&o, s->fineTune & 0xF);
		out8(&o, ((uint8_t)s->volume > 64) ? 64 : s->volume);

		int32_t loopStart = s->loopStart;
		int32_t loopLength = s->loopLength;

		if (loopLength < 2)
			loopLength = 2;

		if (loopStart+loopLength <= 2 || loopStart+loopLength > s->length)
		{
			loopStart = 0;
			loopLength = 2;
		}

		out16be(&o, (uint16_t)(loopStart >> 1));
		out16be(&o, (uint16_t)(loopLength >> 1));
	}

	out8(&o, (uint8_t)song->header.songLength);
	out8(&o, 0x7F); // ProTracker puts 0x7F at this place (restartPos/tempo in other trackers)

	for (int32_t i = 0; i < 128; i++)
		out8(&o, (uint8_t)song->header.patternTable[i]);

	int32_t numPatterns = 0;
	for (int32_t i = 0; i < 128; i++)
	{
		if (song->header.patternTable[i] > numPatterns)
			numPatterns = song->header.patternTable[i];
	}

	numPatterns++;
	if (numPatterns > MAX_PATTERNS)
		numPatterns = MAX_PATTERNS;

	outN(&o, (numPatterns <= 64) ? "M.K." : "M!K!", 4);

	for (int32_t i = 0; i < numPatterns; i++)
	{
		note_t *note = song->patterns[i];
		for (int32_t j = 0; j < MOD_ROWS * PAULA_VOICES; j++, note++)
		{
			out8(&o, (note->sample & 0xF0) | (note->period >> 8));
			out8(&o, note->period & 0xFF);
			out8(&o, ((note->sample << 4) & 0xF0) | (note->command & 0x0F));
			out8(&o, note->param);
		}
	}

	for (int32_t i = 0; i < MOD_SAMPLES; i++)
	{
		moduleSample_t *s = &song->samples[i];
		const int8_t *smpPtr8 = &song->sampleData[s->offset];

		// clear first two bytes of non-looping samples (prevents stuck beep)
		if (s->length >= 2 && s->loopStart+s->loopLength == 2)
		{
			out8(&o, 0);
			out8(&o, 0);
			outN(&o, &smpPtr8[2], s->length-2);
		}
		else
		{
			outN(&o, smpPtr8, s->length);
		}
	}

	return o.p <= o.end ? (uint32_t)(o.p - buf) : 0;
}
#else
bool modSave(char *fileName)
{
	FILE *f = fopen(fileName, "wb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	fwrite(song->header.name, 1, 20, f);

	for (int32_t i = 0; i < MOD_SAMPLES; i++)
	{
		moduleSample_t *s = &song->samples[i];

		fwrite(s->text, 1, 22, f);

		uint16_t length = SWAP16(s->length >> 1);
		fwrite(&length, sizeof (int16_t), 1, f);

		fputc(s->fineTune & 0xF, f);
		fputc(((uint8_t)s->volume > 64) ? 64 : s->volume, f);

		int32_t loopStart = s->loopStart;
		int32_t loopLength = s->loopLength;

		if (loopLength < 2)
			loopLength = 2;

		if (loopStart+loopLength <= 2 || loopStart+loopLength > s->length)
		{
			loopStart = 0;
			loopLength = 2;
		}

		uint16_t loopStart16 = SWAP16(loopStart >> 1);
		uint16_t loopLength16 = SWAP16(loopLength >> 1);

		fwrite(&loopStart16, sizeof (int16_t), 1, f);
		fwrite(&loopLength16, sizeof (int16_t), 1, f);
	}

	fputc((uint8_t)song->header.songLength, f);
	fputc(0x7F, f); // ProTracker puts 0x7F at this place (restartPos/tempo in other trackers)

	for (int32_t i = 0; i < 128; i++)
		fputc((uint8_t)song->header.patternTable[i], f);

	int32_t numPatterns = 0;
	for (int32_t i = 0; i < 128; i++)
	{
		if (song->header.patternTable[i] > numPatterns)
			numPatterns = song->header.patternTable[i];
	}

	numPatterns++;
	if (numPatterns > MAX_PATTERNS)
		numPatterns = MAX_PATTERNS;

	fwrite((numPatterns <= 64) ? "M.K." : "M!K!", 1, 4, f);

	for (int32_t i = 0; i < numPatterns; i++)
	{
		note_t *note = song->patterns[i];
		for (int32_t j = 0; j < MOD_ROWS * PAULA_VOICES; j++, note++)
		{
			fputc((note->sample & 0xF0) | (note->period >> 8), f);
			fputc(note->period & 0xFF, f);
			fputc(((note->sample << 4) & 0xF0) | (note->command & 0x0F), f);
			fputc(note->param, f);
		}
	}

	for (int32_t i = 0; i < MOD_SAMPLES; i++)
	{
		moduleSample_t *s = &song->samples[i];
		const int8_t *smpPtr8 = &song->sampleData[s->offset];

		// clear first two bytes of non-looping samples (prevents stuck beep)
		if (s->length >= 2 && s->loopStart+s->loopLength == 2)
		{
			fputc(0, f);
			fputc(0, f);

			fwrite(&smpPtr8[2], 1, s->length-2, f);
		}
		else
		{
			fwrite(smpPtr8, 1, s->length, f);
		}
	}

	fclose(f);

	displayMsg("MODULE SAVED !");
	setMsgPointer();

	diskop.cached = false;
	if (ui.diskOpScreenShown)
		ui.updateDiskOpFileList = true;

	updateWindowTitle(MOD_NOT_MODIFIED);
	return true;
}

bool saveModule(bool checkIfFileExist, bool giveNewFreeFilename)
{
	char fileName[128], tmpBuffer[64];
	struct stat statBuffer;

	memset(tmpBuffer, 0, sizeof (tmpBuffer));
	memset(fileName, 0, sizeof (fileName));

	if (config.modDot)
	{
		// extension.filename
		if (song->header.name[0] == '\0')
		{
			strcat(fileName, "mod.untitled");
		}
		else
		{
			strcat(fileName, "mod.");
			for (int32_t i = 4; i < 20+4; i++)
			{
				fileName[i] = (char)tolower(song->header.name[i-4]);
				if (fileName[i] == '\0') break;
				sanitizeFilenameChar(&fileName[i]);
			}
		}
	}
	else
	{
		// filename.extension
		if (song->header.name[0] == '\0')
		{
			strcat(fileName, "untitled.mod");
		}
		else
		{
			for (int32_t i = 0; i < 20; i++)
			{
				fileName[i] = (char)tolower(song->header.name[i]);
				if (fileName[i] == '\0') break;
				sanitizeFilenameChar(&fileName[i]);
			}
			strcat(fileName, ".mod");
		}
	}

	if (giveNewFreeFilename && stat(fileName, &statBuffer) == 0)
	{
		for (int32_t i = 1; i <= 999; i++)
		{
			if (config.modDot)
			{
				// extension.filename
				if (song->header.name[0] == '\0')
				{
					sprintf(fileName, "mod.untitled-%d", i);
				}
				else
				{
					for (int32_t j = 0; j < 20; j++)
					{
						tmpBuffer[j] = (char)tolower(song->header.name[j]);
						if (tmpBuffer[j] == '\0') break;
						sanitizeFilenameChar(&tmpBuffer[j]);
					}
					sprintf(fileName, "mod.%s-%d", tmpBuffer, i);
				}
			}
			else
			{
				// filename.extension
				if (song->header.name[0] == '\0')
				{
					sprintf(fileName, "untitled-%d.mod", i);
				}
				else
				{
					for (int32_t j = 0; j < 20; j++)
					{
						tmpBuffer[j] = (char)tolower(song->header.name[j]);
						if (tmpBuffer[j] == '\0') break;
						sanitizeFilenameChar(&tmpBuffer[j]);
					}
					sprintf(fileName, "%s-%d.mod", tmpBuffer, i);
				}
			}

			if (stat(fileName, &statBuffer) != 0)
				break; // this filename can be used
		}
	}

	if (checkIfFileExist && stat(fileName, &statBuffer) == 0)
	{
		if (!askBox(ASKBOX_YES_NO, "OVERWRITE FILE ?"))
			return false;
	}

	return modSave(fileName);
}
#endif
