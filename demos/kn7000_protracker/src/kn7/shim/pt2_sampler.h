/* KN7000 shim (src/kn7/pt2_kn7.h): the sample editor is not part of the port. */
#pragma once
#include <stdint.h>
void fillSampleRedoBuffer(uint8_t smp);
void updateSamplePos(void);
void recalcChordLength(void);
