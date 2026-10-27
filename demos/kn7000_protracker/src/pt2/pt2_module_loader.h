#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pt2_header.h"
#ifndef PT2_KN7
#include "pt2_unicode.h"
#endif
#include "pt2_structs.h"

#ifdef PT2_KN7
module_t *modLoadFromBuffer(uint8_t *modBuffer, uint32_t filesize);
#else
void loadModFromArg(char *arg);
void loadDroppedFile(char *fullPath, uint32_t fullPathLen, bool autoPlay, bool songModifiedCheck);
module_t *modLoad(UNICHAR *fileName);
#endif
void setupLoadedMod(void);
