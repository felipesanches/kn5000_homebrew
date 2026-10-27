#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_structs.h"

#define MIN_BPM 32
#define MAX_BPM 255

void gotoNextMulti(void);
double ciaBpm2Hz(int32_t bpm);
void updatePaulaLoops(void); // used after manipulating Paula sample loop points while playing
void turnOffVoices(void);
void initializeModuleChannels(module_t *s);
module_t *createEmptyMod(void);
#ifdef PT2_KN7
#define KN7_SAMPLE_ALIGN 32 // bytes: a sample starts on a whole number of the DSP mixer's COPY_STEP words (8 x 4 bytes)
bool kn7AllocSamples(module_t *m); // (from the sample headers)
bool kn7AllocPatterns(module_t *m, int32_t count);
note_t *kn7PatternForEdit(module_t *m, int32_t pattern); // the pattern, with room of its own (a note is to go in)
void kn7FreeMod(module_t *m);
int8_t *kn7SampleArena(uint32_t bytes); // main.c: the block for a module's samples, zeroed; NULL when too small
#endif
void setReplayerPosToTrackerPos(void);
void setPattern(int16_t pattern);
bool tickReplayer(void);
void storeTempVariables(void);
void restartSong(void);
void resetSong(void);
void incPatt(void);
void decPatt(void);
void updateNewPos(void); // for after having edited the "POS" digits only
void modSetPos(int16_t pos, int16_t row);
void modStop(void);
void doStopIt(bool resetPlayMode);
void playPattern(int8_t startRow);
void modPlay(int16_t patt, int16_t pos, int8_t row);
void modSetSpeed(int32_t speed);
void modSetTempo(int32_t bpm, bool doLockAudio);
void modFree(void);
void clearSong(void);
void clearSamples(void);
void modSetPattern(uint8_t pattern);
