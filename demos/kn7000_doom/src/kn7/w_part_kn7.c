/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * w_part_kn7.c -- the first bytes of a lump (CHOCO_KN7: r_data.c's R_InitSpriteLumps, which read every sprite whole
 * at start to learn its size and offsets from its 8-byte header). The KN7000 reads the card at about 100 KB a second in
 * MAME. A lump the zone holds already is used where it is. R_GenerateLookup reads the wall patches whole still: reading
 * only their headers and column offsets changed what many frames show (the zone then holds other lumps; Chocolate
 * Doom's frames move with the zone's layout), tests/results/frames-partial-reads-2026-10-29.txt. */
#include <string.h>

#include "doomtype.h"
#include "i_system.h"
#include "w_file.h"
#include "w_wad.h"

void W_ReadLumpStart(lumpindex_t lump, void *dest, int bytes)
{
    const lumpinfo_t *l = lumpinfo[lump];
    if (bytes > l->size)
        bytes = l->size;
    if (l->cache != NULL)
    {
        memcpy(dest, l->cache, bytes);
        return;
    }
    if ((int)W_Read(l->wad_file, l->position, dest, bytes) < bytes)
        I_Error("W_ReadLumpStart: only part of lump %i read", lump);
}
