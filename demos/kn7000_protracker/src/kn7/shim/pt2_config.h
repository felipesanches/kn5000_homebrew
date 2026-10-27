/* KN7000 shim (src/kn7/pt2_kn7.h): the options pt2-clone reads from PROTRACKER.INI, fixed (src/kn7/stubs.c). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct config_t
{
	bool blankZeroFlag, accidental, compoMode, enableE8xEffect, keepEditModeAfterStepPlay, realVuMeters;
	bool pattDots, multiModeNext, transDel, modDot, rememberPlayMode, startInFullscreen;
	int32_t maxSampleLength;
} config_t;

extern config_t config;
