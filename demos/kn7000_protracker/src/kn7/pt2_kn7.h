/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * pt2_kn7.h -- included first in every file of the KN7000 build (-include): pt2-clone's sources (src/pt2/) built
 * without SDL, without an audio thread and without a mouse. What they call of the parts that are left out is declared
 * in the shim headers (src/kn7/shim/, which stand in for pt2-clone's headers of the same names) and done by src/kn7/. */
#pragma once
#define PT2_KN7 1
#include <stdint.h>

typedef uint8_t pixel_t;  /* a pixel: an index into the LCD controller's look-up table (video.c) */
