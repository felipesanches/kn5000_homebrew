/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * w_file_kn7.c -- the WAD's file on the KN7000, in place of Chocolate Doom's w_file_stdc.c (whose class w_file.c opens
 * every WAD with, so it keeps its name): a file of the card's DOOM\ folder, read by sectors anywhere in it
 * (main.c kn7FileOpen, PfHost::rawRead), never mapped. Each read is a request the platform serves on the firmware's main
 * task, so a small one costs about as much as a large one: a read smaller than CACHE_BYTES fills a cache of that many
 * bytes from its sector on, and the reads after it that lie within come from there (the sprites, the flats and a
 * map's lumps lie side by side in the WAD). */
#include <stdlib.h>
#include <string.h>

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

#ifndef WAD_CACHE
#define WAD_CACHE 0x8000  /* (the Makefile's WAD_CACHE; 0: no cache) */
#endif
#define CACHE_BYTES WAD_CACHE
static byte *cache;                 /* (from the zone, once) */
static const kn7_wad_file_t *cacheOf;
static unsigned cacheAt, cacheLen;  /* the file's bytes [cacheAt, cacheAt + cacheLen) */

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
    if (cacheOf == kn7_wad)
        cacheOf = NULL;
    kn7FileClose(&kn7_wad->raw);
    Z_Free(kn7_wad);
}

/* what the card's reads cost: tests/mame/doom_test.lua reads them */
u32 kn7WadReads, kn7WadBytes, kn7WadMs, kn7WadCalls;

static int fileRead(const kn7_wad_file_t *w, unsigned offset, void *dst, unsigned bytes)
{
    const u32 t0 = host->ms();
    const int got = kn7FileRead(&w->raw, offset, dst, bytes);
    kn7WadMs += host->ms() - t0;
    ++kn7WadReads;
    kn7WadBytes += bytes;
    return got;
}

static size_t W_KN7_Read(wad_file_t *wad, unsigned int offset, void *buffer, size_t buffer_len)
{
    kn7_wad_file_t *kn7_wad = (kn7_wad_file_t *)wad;
    ++kn7WadCalls;
    if (CACHE_BYTES && buffer_len < CACHE_BYTES / 2)
    {
        if (cache == NULL)
            cache = Z_Malloc(CACHE_BYTES, PU_STATIC, NULL);
        if (cacheOf != kn7_wad || offset < cacheAt || offset + buffer_len > cacheAt + cacheLen)
        {
            const unsigned at = offset & ~511u;
            const int got = fileRead(kn7_wad, at, cache, CACHE_BYTES);
            cacheOf = got > 0 ? kn7_wad : NULL;
            cacheAt = at;
            cacheLen = got > 0 ? (unsigned)got : 0;
        }
        if (cacheOf == kn7_wad && offset >= cacheAt && offset < cacheAt + cacheLen)
        {
            const unsigned n = cacheAt + cacheLen - offset < buffer_len ? cacheAt + cacheLen - offset : buffer_len;
            memcpy(buffer, cache + (offset - cacheAt), n);
            return n;
        }
        return 0;
    }
    const int got = fileRead(kn7_wad, offset, buffer, buffer_len);
    return got < 0 ? 0 : (size_t)got;
}

wad_file_class_t stdc_wad_file = {
    W_KN7_OpenFile,
    W_KN7_CloseFile,
    W_KN7_Read,
};
