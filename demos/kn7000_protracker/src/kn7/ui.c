/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * ui.c -- ProTracker's main screen on the KN7000: pt2-clone's pt2_visuals.c for that screen (displayMainScreen,
 * updateSongInfo1, updateSongInfo2, renderMuteButtons, renderVuMeters, updateVisualizer, drawScopes,
 * updateSpectrumAnalyzer, sinkVisualizerBars), drawing into its 320 x 240 frame buffer as pt2-clone does, with these
 * changes for the instrument:
 *   * 240 lines, not 255: the pattern shows 13 rows (pt2_pattern_viewer.c), its middle one 7 lines higher; the pattern
 *     cursor and the VU meters move up with it, the meters 41 lines tall instead of 48 (their bars are scaled);
 *   * the scopes read where each voice is from Paula's model (paula.c), not from a scope of their own;
 *   * no mouse, so no buttons are pressed on the screen: the labels beside it name the soft keys (main.c's controls).
 * The disk op., the position editor, the edit ops., the sampler and the about screen are not part of the port. */
#include <string.h>

#include "kn7pt.h"
#include "pt2_structs.h"
#include "pt2_tables.h"
#include "pt2_textout.h"
#include "pt2_bmp.h"
#include "pt2_config.h"
#include "pt2_pattern_viewer.h"
#include "pt2_visuals.h"

#define PATTERN_MIDDLE_Y (140 + 6 * 7)      /* the middle row's top line, less the line its big glyphs add */
#define VU_BOTTOM (PATTERN_MIDDLE_Y - 2)    /* pt2-clone: 187, two lines above its middle row (189) */
#define VU_HEIGHT (VU_BOTTOM - 140 + 1)     /* 41: from the pattern's first line */

static pixel_t vuMetersBg[PAULA_VOICES * 10 * 48];
static bool redrawAll = true, cursorShown;
static pixel_t cursorBg[11 * 14];
static int cursorX = -1;

static const uint16_t cursorPosTable[24] =
{
	 30,  54,  62,  70,  78,  86,
	102, 126, 134, 142, 150, 158,
	174, 198, 206, 214, 222, 230,
	246, 270, 278, 286, 294, 302
};

/* ---- pt2_visuals.c's drawing primitives, 8 bits a pixel ---------------------------------------------------- */
void blit32(int32_t x, int32_t y, int32_t w, int32_t h, const pixel_t *src)
{
	pixel_t *dst = &video.frameBuffer[(y * SCREEN_W) + x];
	for (int32_t yy = 0; yy < h; yy++)
	{
		memcpy(dst, src, w);
		src += w;
		dst += SCREEN_W;
	}
}

void putPixel(int32_t x, int32_t y, const uint32_t pixelColor)
{
	video.frameBuffer[(y * SCREEN_W) + x] = (pixel_t)pixelColor;
}

void hLine(int32_t x, int32_t y, int32_t w, const uint32_t pixelColor)
{
	memset(&video.frameBuffer[(y * SCREEN_W) + x], (pixel_t)pixelColor, w);
}

void vLine(int32_t x, int32_t y, int32_t h, const uint32_t pixelColor)
{
	pixel_t *dst = &video.frameBuffer[(y * SCREEN_W) + x];
	for (int32_t yy = 0; yy < h; yy++, dst += SCREEN_W)
		*dst = (pixel_t)pixelColor;
}

void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t pixelColor)
{
	pixel_t *dst = &video.frameBuffer[(y * SCREEN_W) + x];
	for (int32_t yy = 0; yy < h; yy++, dst += SCREEN_W)
		memset(dst, (pixel_t)pixelColor, w);
}

/* ---- the status line ------------------------------------------------------------------------------------- */
void statusAllRight(void) { setStatusMessage("ALL RIGHT", DO_CARRY); }
void statusOutOfMemory(void) { displayErrorMsg("OUT OF MEMORY !!!"); }
void statusSampleIsEmpty(void) { displayErrorMsg("SAMPLE IS EMPTY"); }
void statusNotSampleZero(void) { displayErrorMsg("NOT SAMPLE 0 !"); }
void changeStatusText(const char *text) { setStatusMessage(text, NO_CARRY); }
void uiMessage(const char *s) { setStatusMessage(s, NO_CARRY); }

/* ---- the screen ---------------------------------------------------------------------------------------------- */
void updateCursorPos(void)
{
	// (drawn by uiUpdate when the edit mode shows it)
}

void renderMuteButtons(void)
{
	pixel_t *dst = &video.frameBuffer[(3 * SCREEN_W) + 310];
	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		const pixel_t *src;
		uint32_t pitch;
		if (editor.muted[i])
		{
			src = &muteButtonsBMP[i * (6 * 7)];
			pitch = 7;
		}
		else
		{
			src = &trackerFrameBMP[((3 + (i * 11)) * SCREEN_W) + 310];
			pitch = SCREEN_W;
		}

		for (uint32_t y = 0; y < 6; y++, src += pitch, dst += SCREEN_W)
			memcpy(dst, src, 7);

		dst += SCREEN_W * 5;
	}
}

void updateCurrSample(void)
{
	ui.updateCurrSampleName = true;
	ui.updateSongSize = true;
	ui.updateCurrSampleFineTune = true;
	ui.updateCurrSampleNum = true;
	ui.updateCurrSampleVolume = true;
	ui.updateCurrSampleLength = true;
	ui.updateCurrSampleRepeat = true;
	ui.updateCurrSampleReplen = true;
}

void updatePatternData(void)
{
	if (ui.updatePatternData)
	{
		ui.updatePatternData = false;
		redrawPattern();
	}
}

void renderQuadrascopeBg(void)
{
	for (uint32_t y = 44; y < 44 + 55; y++)
		memcpy(&video.frameBuffer[(y * SCREEN_W) + 120], &trackerFrameBMP[(y * SCREEN_W) + 120], 200);
}

void renderSpectrumAnalyzerBg(void)
{
	blit32(120, 44, 200, 55, spectrumVisualsBMP);
}

/* pt2-clone's displayMainScreen, for the main screen alone */
void uiFullRedraw(void)
{
	redrawAll = true;
}

void displayMainScreen(void)
{
	editor.blockMarkFlag = false;

	ui.updateSongName = true;
	ui.updateSongSize = true;
	ui.updateSongTiming = true;
	ui.updateTrackerFlags = true;
	ui.updateStatusText = true;
	updateCurrSample();

	// the frame's first 240 lines; the bottom line of its border (line 254) on the last
	blit32(0, 0, 320, SCREEN_H - 1, trackerFrameBMP);
	memcpy(&video.frameBuffer[(SCREEN_H - 1) * SCREEN_W], &trackerFrameBMP[254 * SCREEN_W], SCREEN_W);

	ui.updateSongBPM = true;
	ui.updateCurrPattText = true;
	ui.updatePatternData = true;
	ui.updateSongPos = true;
	ui.updateSongPattern = true;
	ui.updateSongLength = true;

	// draw zeroes that will never change (to the left of numbers)
	charOut(64,  3, '0', video.palette[PAL_GENTXT]);
	textOut(64, 14, "00", video.palette[PAL_GENTXT]);
	charOut(64, 25, '0', video.palette[PAL_GENTXT]);
	textOut(64, 47, "00", video.palette[PAL_GENTXT]);
	textOut(64, 58, "00", video.palette[PAL_GENTXT]);

	if (ui.visualizerMode == VISUAL_QUADRASCOPE)
		renderQuadrascopeBg();
	else
		renderSpectrumAnalyzerBg();

	renderMuteButtons();
	cursorShown = false;
	cursorX = -1;
}

static void updateSongInfo1(void) // left side of screen
{
	moduleSample_t *currSample = &song->samples[editor.currSample];

	if (ui.updateSongPos)
	{
		ui.updateSongPos = false;
		printThreeDecimalsBg(72, 3, *editor.currPosDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateSongPattern)
	{
		ui.updateSongPattern = false;
		printTwoDecimalsBg(80, 14, *editor.currPatternDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateSongLength)
	{
		ui.updateSongLength = false;
		printThreeDecimalsBg(72, 25, *editor.currLengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleFineTune)
	{
		ui.updateCurrSampleFineTune = false;
		textOutBg(80, 36, ftuneStrTab[currSample->fineTune & 0xF], video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleNum)
	{
		ui.updateCurrSampleNum = false;
		printTwoHexBg(80, 47, editor.sampleZero ? 0 : ((*editor.currSampleDisp) + 1), video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleVolume)
	{
		ui.updateCurrSampleVolume = false;
		printTwoHexBg(80, 58, *currSample->volumeDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleLength)
	{
		ui.updateCurrSampleLength = false;
		printFourHexBg(64, 69, *currSample->lengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleRepeat)
	{
		ui.updateCurrSampleRepeat = false;
		printFourHexBg(64, 80, *currSample->loopStartDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrSampleReplen)
	{
		ui.updateCurrSampleReplen = false;
		printFourHexBg(64, 91, *currSample->loopLengthDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}
}

static void updateSongInfo2(void) // two middle rows of screen
{
	if (ui.updateStatusText)
	{
		ui.updateStatusText = false;
		fillRect(88, 127, 17*FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);
		textOut(88, 127, ui.statusMessage, video.palette[PAL_GENTXT]);
	}

	if (ui.updateSongBPM)
	{
		ui.updateSongBPM = false;
		printThreeDecimalsBg(32, 123, song->currBPM, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateCurrPattText)
	{
		ui.updateCurrPattText = false;
		printTwoDecimalsBg(8, 127, *editor.currEditPatternDisp, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}

	if (ui.updateTrackerFlags)
	{
		ui.updateTrackerFlags = false;
		charOutBg(1, 113, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		charOutBg(8, 113, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		charOutBg(1, 102, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		charOutBg(16, 102, ' ', video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		charOutBg(24, 102, '0' + editor.editMoveAdd, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		charOutBg(311, 128, ' ', video.palette[PAL_GENBKG], video.palette[PAL_GENBKG]);
	}

	// playback timer
	uint32_t seconds = editor.playbackSeconds;
	if (seconds > 5999)
		seconds = 5999;
	printTwoDecimalsBg(272, 102, seconds / 60, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	printTwoDecimalsBg(296, 102, seconds % 60, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);

	if (ui.updateSongName)
	{
		ui.updateSongName = false;
		for (int32_t x = 0; x < 20; x++)
		{
			char tempChar = song->header.name[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(104 + (x * FONT_CHAR_W), 102, tempChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateCurrSampleName)
	{
		ui.updateCurrSampleName = false;
		moduleSample_t *currSample = &song->samples[editor.currSample];
		for (int32_t x = 0; x < 22; x++)
		{
			char tempChar = currSample->text[x];
			if (tempChar == '\0')
				tempChar = '_';

			charOutBg(104 + (x * FONT_CHAR_W), 113, tempChar, video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
		}
	}

	if (ui.updateSongSize)
	{
		ui.updateSongSize = false;
		fillRect(264, 123, 6*FONT_CHAR_W, FONT_CHAR_H, video.palette[PAL_GENBKG]);

		uint32_t totalSampleDataSize = 0;
		for (int32_t i = 0; i < MOD_SAMPLES; i++)
			totalSampleDataSize += song->samples[i].length;

		uint32_t totalPatterns = 0;
		for (int32_t i = 0; i < 128; i++)
		{
			if (song->header.patternTable[i] > totalPatterns)
				totalPatterns = song->header.patternTable[i];
		}

		const uint32_t moduleSize = 2108 + (totalPatterns * 1024) + totalSampleDataSize;
		printSixDecimals(264, 123, moduleSize, video.palette[PAL_GENTXT]);
	}

	if (ui.updateSongTiming)
	{
		ui.updateSongTiming = false;
		textOutBg(288, 130, (editor.timingMode == TEMPO_MODE_CIA) ? "CIA" : "VBL", video.palette[PAL_GENTXT], video.palette[PAL_GENBKG]);
	}
}

/* ---- the VU meters, over the pattern (pt2-clone keeps what is under them and puts it back first) ---------------- */
static void fillFromVuMetersBgBuffer(void)
{
	const pixel_t *src = vuMetersBg;
	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		pixel_t *dst = &video.frameBuffer[(VU_BOTTOM * SCREEN_W) + 55 + (72 * i)];
		for (uint32_t y = 0; y < VU_HEIGHT; y++, src += 10, dst -= SCREEN_W)
			memcpy(dst, src, 10);
	}
}

void fillToVuMetersBgBuffer(void)
{
	pixel_t *dst = vuMetersBg;
	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		const pixel_t *src = &video.frameBuffer[(VU_BOTTOM * SCREEN_W) + 55 + (72 * i)];
		for (uint32_t y = 0; y < VU_HEIGHT; y++, dst += 10, src -= SCREEN_W)
			memcpy(dst, src, 10);
	}
}

void renderVuMeters(void)
{
	fillToVuMetersBgBuffer();
	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		uint32_t h = editor.vuMeterVolumes[i];
		if (h > 48)
			h = 48;

		h = (h * VU_HEIGHT) / 48;
		pixel_t *dst = &video.frameBuffer[(VU_BOTTOM * SCREEN_W) + 55 + (72 * i)];
		for (uint32_t y = 0; y < h; y++, dst -= SCREEN_W)
			memcpy(dst, &vuMeterBMP[((y * 48) / VU_HEIGHT) * 10], 10);
	}
}

void sinkVisualizerBars(void)
{
	for (int32_t i = 0; i < PAULA_VOICES; i++)
		if (editor.vuMeterVolumes[i] > 0)
			editor.vuMeterVolumes[i]--;

	for (int32_t i = 0; i < SPECTRUM_BAR_NUM; i++)
		if (editor.spectrumVolumes[i] > 0)
			editor.spectrumVolumes[i]--;
}

void updateSpectrumAnalyzer(uint8_t vol, uint16_t period)
{
	if (ui.visualizerMode != VISUAL_SPECTRUM || vol == 0)
		return;

	if (period < 108 || period > 907)
		return; // (undefined in real PT)

	if (period > 856)
		return; // C-1 with finetune < 0 is ignored

	if (period < 113)
		period = 113; // B-3 with finetune > 0 behaves like finetune 0

	if (vol > 64)
		vol = 64;

	const uint8_t vol24 = (vol * 24) >> 6;
	period = (856 - 113) - (period - 113);
	const uint32_t index = (period * period) / 25093; // 0..22

	editor.spectrumVolumes[index] += vol24;
	if (editor.spectrumVolumes[index] > SPECTRUM_BAR_HEIGHT)
		editor.spectrumVolumes[index] = SPECTRUM_BAR_HEIGHT;

	if (index > 0)
	{
		editor.spectrumVolumes[index-1] += vol24 / 2;
		if (editor.spectrumVolumes[index-1] > SPECTRUM_BAR_HEIGHT)
			editor.spectrumVolumes[index-1] = SPECTRUM_BAR_HEIGHT;
	}

	if (index < SPECTRUM_BAR_NUM-1)
	{
		editor.spectrumVolumes[index+1] += vol24 / 2;
		if (editor.spectrumVolumes[index+1] > SPECTRUM_BAR_HEIGHT)
			editor.spectrumVolumes[index+1] = SPECTRUM_BAR_HEIGHT;
	}
}

/* pt2-clone's drawScopes, each scope's data where Paula's model has the voice */
static void drawScopes(void)
{
	int32_t scopeX = 128;
	const pixel_t bg = (pixel_t)video.palette[PAL_BACKGRD], fg = (pixel_t)video.palette[PAL_QADSCP];
	for (int32_t i = 0; i < PAULA_VOICES; i++, scopeX += SCOPE_WIDTH + 8)
	{
		fillRect(scopeX, 55, SCOPE_WIDTH, SCOPE_HEIGHT, bg);

		const int8_t *data;
		uint32_t length, pos16, volume;
		if (!editor.muted[i] && paulaVoice(i, &data, &length, &pos16, &volume) && volume != 0)
		{
			const int8_t vol = -(int8_t)(volume >> 1);
			uint32_t pos = pos16 >> 16;
			pixel_t *draw = &video.frameBuffer[(71 * SCREEN_W) + scopeX];
			for (int32_t x = 0; x < SCOPE_WIDTH; x++)
			{
				const int16_t d = (data[pos] * vol) >> 8;
				draw[(d * SCREEN_W) + x] = fg;
				if (++pos >= length)
					pos = 0;
			}
		}
		else
		{
			hLine(scopeX, 71, SCOPE_WIDTH, fg);
		}
	}
}

void updateVisualizer(void)
{
	if (ui.visualizerMode == VISUAL_SPECTRUM)
	{
		pixel_t *dst = &video.frameBuffer[(59 * SCREEN_W) + 129];
		for (uint32_t i = 0; i < SPECTRUM_BAR_NUM; i++)
		{
			pixel_t pixel = (pixel_t)video.palette[PAL_GENBKG];
			int32_t tmpVol = editor.spectrumVolumes[i];
			if (tmpVol > SPECTRUM_BAR_HEIGHT)
				tmpVol = SPECTRUM_BAR_HEIGHT;

			for (int32_t y = SPECTRUM_BAR_HEIGHT-1; y >= 0; y--, dst += SCREEN_W)
			{
				if (y < tmpVol)
					pixel = (pixel_t)analyzerColorsRGB24[y];

				memset(dst, pixel, SPECTRUM_BAR_WIDTH);
			}

			dst -= (SCREEN_W * SPECTRUM_BAR_HEIGHT) - (SPECTRUM_BAR_WIDTH + 2);
		}
	}
	else
	{
		drawScopes();
	}
}

/* the pattern cursor (pt2-clone's sprite) on the middle row while editing; what is under it kept */
static void cursorHide(void)
{
	if (!cursorShown)
		return;

	for (int y = 0; y < 14; y++)
		memcpy(&video.frameBuffer[((PATTERN_MIDDLE_Y - 1 + y) * SCREEN_W) + cursorX], &cursorBg[y * 11], 11);
	cursorShown = false;
}

static void cursorDraw(void)
{
	if (editor.currMode != MODE_EDIT && editor.currMode != MODE_RECORD)
		return;

	cursorX = cursorPosTable[cursor.pos];
	for (int y = 0; y < 14; y++)
	{
		pixel_t *row = &video.frameBuffer[((PATTERN_MIDDLE_Y - 1 + y) * SCREEN_W) + cursorX];
		memcpy(&cursorBg[y * 11], row, 11);
		for (int x = 0; x < 11; x++)
			if (patternCursorBMP[(y * 11) + x] != (pixel_t)video.palette[PAL_COLORKEY])
				row[x] = patternCursorBMP[(y * 11) + x];
	}
	cursorShown = true;
}

void uiInit(void)
{
	ui.visualizerMode = VISUAL_QUADRASCOPE;
	redrawAll = true;
}

void uiUpdate(void)
{
	if (song == NULL)
		return;

	cursorHide();
	if (redrawAll)
	{
		redrawAll = false;
		displayMainScreen();
	}
	else
	{
		fillFromVuMetersBgBuffer();
	}

	updateSongInfo1();
	updateSongInfo2();
	updatePatternData();
	updateVisualizer();
	renderVuMeters();
	cursorDraw();
}

/* ---- the soft keys' labels, beside the screen ---------------------------------------------------------------- */
static const char *const leftLabels[5] = { "PLAY SONG", "PLAY PATT", "STOP", "EDIT", "SCOPE/SPEC" };
static const char *const rightLabels[5] = { "PREV MOD", "NEXT MOD", "SAMPLE -", "SAMPLE +", "MUTE ALL" };

void uiSides(void)
{
	const pixel_t bg = (pixel_t)video.palette[PAL_BACKGRD];
	const pixel_t text = kn7Colour(0xFFFFFF), dim = kn7Colour(0x888888), accent = kn7Colour(0xFFDD00);
	sideFill(0, 0, PT_LEFT, LCD_H, bg);
	sideFill(PT_LEFT + SCREEN_W, 0, PT_LEFT, LCD_H, bg);
	for (int i = 0; i < 5; i++)
	{
		const int y = 48 * i + 18;
		sideText(4, y, "<", accent, bg);
		sideText(16, y, leftLabels[i], text, bg);
		const int n = (int)strlen(rightLabels[i]);
		sideText(LCD_W - 16 - 8 * n, y, rightLabels[i], text, bg);
		sideText(LCD_W - 12, y, ">", accent, bg);
	}

	sideText(16, 226, "PROTRACKER 2.3D", dim, bg);
	sideText(LCD_W - 16 - 8 * 17, 226, "HOLD EXIT TO QUIT", dim, bg);
}
