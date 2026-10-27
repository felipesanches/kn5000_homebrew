#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef PT2_KN7
uint32_t modSaveToBuffer(uint8_t *buf, uint32_t max); // KN7000: the module as modSave writes it; 0 if it does not fit
#else
bool saveModule(bool checkIfFileExist, bool giveNewFreeFilename);

bool modSave(char *fileName); // used by saveModule() and crash handler
#endif
