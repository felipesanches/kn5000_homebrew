/* KN7000 shim (src/kn7/pt2_kn7.h): no audio thread. The replayer is ticked from the main loop (main.c) by the
 * kernel's clock, and Paula's registers become the effects DSP's voice commands (paula.c). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct audio_t
{
	volatile bool locked;
	bool ledFilterEnabled;
	uint32_t samplesPerTickInt, samplesPerTickIntTab[256 - 32 + 1];
	uint64_t samplesPerTickFrac, samplesPerTickFracTab[256 - 32 + 1];
	uint32_t tickTimeIntTab[256 - 32 + 1];
	uint64_t tickTimeFracTab[256 - 32 + 1];
	uint32_t tickSampleCounter;
	uint64_t tickSampleCounterFrac;
} audio_t;

extern audio_t audio;

void lockAudio(void);
void unlockAudio(void);
void resetAudioDither(void);
void setLEDFilter(bool state);
void updateReplayerTimingMode(void);
