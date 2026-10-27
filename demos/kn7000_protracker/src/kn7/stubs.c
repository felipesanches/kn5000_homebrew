/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * stubs.c -- what pt2-clone's replayer and loader call of the parts the KN7000 port leaves out (the shim headers in
 * src/kn7/shim/ declare them): the configuration, fixed at pt2-clone's defaults for a ProTracker 2.3D replayer; the
 * audio thread's lock (there is no thread: the replayer runs on the program's own task, between the frames it draws);
 * the visuals' sync queue (the scopes read Paula's model directly, paula.c); the mouse pointer, the position editor, the
 * sampler. */
#include "kn7pt.h"
#include "pt2_structs.h"
#include "pt2_audio.h"
#include "pt2_config.h"
#include "pt2_mouse.h"
#include "pt2_posed.h"
#include "pt2_sampler.h"
#include "pt2_helpers.h"
#include "pt2_visuals_sync.h"

module_t *song;
audio_t audio;
config_t config =
{
	.blankZeroFlag = false,
	.accidental = false,            // sharps (C#), as PT
	.compoMode = false,
	.enableE8xEffect = false,       // pt2-clone's default: E8x is demo sync, not Karplus-Strong
	.keepEditModeAfterStepPlay = false,
	.realVuMeters = false,
	.pattDots = false,
	.multiModeNext = false,
	.maxSampleLength = 65534,       // ProTracker 2.3D: 64 KB samples
};

void lockAudio(void) { audio.locked = true; }
void unlockAudio(void) { audio.locked = false; }
void resetAudioDither(void) {}
void setLEDFilter(bool state) { audio.ledFilterEnabled = state; } // (Paula's filters are not modelled: paula.c)
void updateReplayerTimingMode(void) {}

void setSyncTickTimeLen(uint32_t timeLenInt, uint64_t timeLenFrac) { (void)timeLenInt; (void)timeLenFrac; }
void setVisualsDMACON(uint16_t bits) { (void)bits; }
void setVisualsVolume(int32_t ch, uint16_t vol) { (void)ch; (void)vol; }
void setVisualsPeriod(int32_t ch, uint16_t period) { (void)ch; (void)period; }
void setVisualsLength(int32_t ch, uint16_t len) { (void)ch; (void)len; }
void setVisualsDataPtr(int32_t ch, const int8_t *src) { (void)ch; (void)src; }

void pointerSetMode(uint8_t pointerMode, bool carry) { (void)pointerMode; (void)carry; }
void pointerSetModeThreadSafe(uint8_t pointerMode, bool carry) { (void)pointerMode; (void)carry; }
void pointerSetPreviousMode(void) {}
void setErrPointer(void) {}
void setMsgPointer(void) {}

void posEdClearNames(void) {}
void fillSampleRedoBuffer(uint8_t smp) { (void)smp; }
void updateSamplePos(void) {}
void recalcChordLength(void) {}
void updateWindowTitle(bool modified) { (void)modified; }
