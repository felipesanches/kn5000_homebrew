/* KN7000 shim (src/kn7/pt2_kn7.h): there is no mouse; the pointer's modes are kept for the replayer, which sets them. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { POINTER_MODE_IDLE = 0, POINTER_MODE_PLAY = 1, POINTER_MODE_EDIT = 2, POINTER_MODE_RECORD = 3,
       POINTER_MODE_MSG1 = 4, POINTER_MODE_MSG2 = 5 };

typedef struct guiButton_t { uint16_t x1, y1, x2, y2, b; } guiButton_t;

void pointerSetMode(uint8_t pointerMode, bool carry);
void pointerSetModeThreadSafe(uint8_t pointerMode, bool carry);
void pointerSetPreviousMode(void);
void setErrPointer(void);
void setMsgPointer(void);
