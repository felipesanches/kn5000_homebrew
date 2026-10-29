/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * doom_kn7.h -- included first in every file of the KN7000 build (-include) and of the host test build: Chocolate Doom's
 * sources (src/choco/) built for the KN7000. Their changes are behind CHOCO_KN7; what they call of the KN7000 side is
 * declared here and done by src/kn7/ (or, in the host build, tests/host/host.c). */
#pragma once
#define CHOCO_KN7 1
#define DISABLE_SDL2MIXER 1  /* (no SDL: i_sound.c's sound and music modules are src/kn7/'s) */

/* A stretch of RAM, [lo, hi). */
typedef struct kn7_region
{
    unsigned lo, hi;
} kn7_region_t;

/* The zone's stretches of RAM, in address order, *count of them (z_zone.c Z_Init). */
const kn7_region_t *I_ZoneRegions(int *count);

/* The first `bytes` of lump `lump` into dest (w_part_kn7.c; r_data.c's sprite sizes). */
void W_ReadLumpStart(int lump, void *dest, int bytes);
