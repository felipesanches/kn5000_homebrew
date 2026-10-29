/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * w_file_kn7.c -- the WAD's file on the KN7000, in place of Chocolate Doom's w_file_stdc.c (whose class w_file.c opens
 * every WAD with, so it keeps its name): a file of the card's DOOM\ folder, read by sectors anywhere in it
 * (main.c kn7FileOpen, PfHost::rawRead), never mapped. */
#include <stdlib.h>

#include "kn7doom.h"
#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

typedef struct
{
    wad_file_t wad;
    PfRawFile raw;
} kn7_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static wad_file_t *W_KN7_OpenFile(const char *path)
{
    kn7_wad_file_t *result = Z_Malloc(sizeof(kn7_wad_file_t), PU_STATIC, 0);
    if (kn7FileOpen(path, &result->raw))
    {
        Z_Free(result);
        return NULL;
    }
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = result->raw.bytes;
    result->wad.path = M_StringDuplicate(path);
    return &result->wad;
}

static void W_KN7_CloseFile(wad_file_t *wad)
{
    kn7_wad_file_t *kn7_wad = (kn7_wad_file_t *)wad;
    kn7FileClose(&kn7_wad->raw);
    Z_Free(kn7_wad);
}

static size_t W_KN7_Read(wad_file_t *wad, unsigned int offset, void *buffer, size_t buffer_len)
{
    kn7_wad_file_t *kn7_wad = (kn7_wad_file_t *)wad;
    const int got = kn7FileRead(&kn7_wad->raw, offset, buffer, buffer_len);
    return got < 0 ? 0 : (size_t)got;
}

wad_file_class_t stdc_wad_file = {
    W_KN7_OpenFile,
    W_KN7_CloseFile,
    W_KN7_Read,
};
