/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * files.c -- reading the game's files from the SD card. The reads are made by the converter entry, on the firmware's
 * main task; the game task asks and waits (Another World's port loads its parts the same way: technics-anotherworld
 * kn7000/README-CNV.md, "How it works"). A whole file is read with the loader's routines (the drive switch to the SD
 * card and back around them), in pieces of 16 KB, as the loader reads KN7APP.BIN, and starts only once no panel button
 * is down (or after about 5 s): the main task is busy for the whole file, and a button's release must not be the panel
 * event it misses. A part of a file (Doom's WAD) is read by sectors (folder.c), at most RAW_MAX a call, at once. */
#include "app.h"

#define PIECE 0x4000
#define HELD_CALLS 100

static struct {
  volatile u32 pending;      /* set by the game task, cleared by the entry when done */
  u32 write;                 /* 1: write the first piece to the file (made afresh), not read; 2: list the folder;
                                3: open a file for raw reads; 4: a raw read */
  PfFile* files;
  PfRawFile* raw;
  u32 offset;
  void* dst;
  const char* name;
  const PfPiece* pieces;
  u32 count;
  volatile s32 result;
  u32 held;
} req;

static u8 skip_buf[512];
/* 1 while the firmware's SD routines fill a buffer of ours: its writes there are ours (tests/mame/pf_test.lua's
 * taps on ARENA3/4 leave them out; Another World's LD_IN_READ). */
volatile u32 file_busy;
static char path[48];

static int panel_idle(void) {
  for (int i = 0; i < 16; ++i)
    if (RD8(fw.panel + i) || RD8(fw.panel + PANEL_CPL + i)) return 0;
  return 1;
}

static s32 read_into(u8* dst, u32 bytes) {
  s32 total = 0;
  file_busy = 1;
  while (bytes) {
    u32 n = bytes > PIECE ? PIECE : bytes;
    u8* to = dst;
    if (!dst && n > sizeof skip_buf) n = sizeof skip_buf;
    if (!dst) to = skip_buf;
    const s32 got = (s32)fw_call(fw.read, n, 0, (u32)to, 0);
    if (got < 0) {
      file_busy = 0;
      return -2;
    }
    total += got;
    if (dst) dst += got;
    bytes -= (u32)got;
    if ((u32)got < n) break;  /* the end of the file */
  }
  file_busy = 0;
  return total;
}

static void make_path(const char* name) {
  int n = 0;
  for (const char* s = prefix; *s && n < (int)sizeof path - 1; ++s) path[n++] = *s;
  for (const char* s = name; *s && n < (int)sizeof path - 1; ++s) path[n++] = *s;
  path[n] = 0;
}

/* On the main task (the entry): the file whole into dst, at most max bytes -> bytes read, or < 0. A file longer
 * than max reads as max bytes: callers that care ask for one byte more than may come. */
s32 file_read_now(const char* name, void* dst, u32 max) {
  make_path(name);
  fw_call(fw.setext, 1, 0, 0, 0);
  const s32 h = (s32)(int16_t)fw_call(fw.open, 0, 0, (u32)path, (u32) "rb");
  s32 total = -1;
  if (h >= 0) {
    total = read_into((u8*)dst, max);
    fw_call(fw.close, 0, 0, 0, 0);
  }
  fw_call(fw.resetext, 0, 0, 0, 0);
  return total;
}

/* A request waits for the entry; a raw one (a part of a file). */
int file_pending(void) { return req.pending != 0; }
int file_raw_pending(void) { return req.pending != 0 && req.write >= 3; }

/* The entry: serves the pending request, if there is one; 1 when it did. */
int file_serve(void) {
  if (!req.pending) return 0;
  if (req.write == 3 || req.write == 4) {  /* (short: a raw read is at most RAW_MAX, so it waits for no button) */
    req.result = req.write == 3 ? folder_open(prefix, req.name, req.raw)
                                : folder_read(req.raw, req.offset, (u8*)req.dst, req.count);
    req.pending = 0;
    return 1;
  }
  if (!panel_idle() && ++req.held < HELD_CALLS) return 0;
  req.held = 0;
  if (req.write == 2) {
    req.result = folder_list(prefix, req.files, req.count);
    req.pending = 0;
    return 1;
  }
  make_path(req.name);
  if (req.write) {  /* drive switch, open, one write a piece, close: as the ROM dumper writes its files */
    s32 total = -1;
    if (fw.write) {
      fw_call(fw.setext, 1, 0, 0, 0);
      const s32 h = (s32)(int16_t)fw_call(fw.open, 0, 0, (u32)path, (u32) "wb");
      if (h >= 0) {
        total = 0;
        const u8* p = (const u8*)req.pieces[0].dst;
        for (u32 left = req.pieces[0].bytes; left;) {
          const u32 n = left > PIECE ? PIECE : left;
          const s32 put = (s32)fw_call(fw.write, n, 0, (u32)p, 0);
          if (put <= 0) break;
          total += put, p += put, left -= (u32)put;
        }
        fw_call(fw.close, 0, 0, 0, 0);
      }
      fw_call(fw.resetext, 0, 0, 0, 0);
    }
    req.result = total;
    req.pending = 0;
    return 1;
  }
  fw_call(fw.setext, 1, 0, 0, 0);
  const s32 h = (s32)(int16_t)fw_call(fw.open, 0, 0, (u32)path, (u32) "rb");
  s32 total = -1;
  if (h >= 0) {
    total = 0;
    for (u32 i = 0; i < req.count; ++i) {
      const s32 got = read_into((u8*)req.pieces[i].dst, req.pieces[i].bytes);
      if (got < 0) {
        total = got;
        break;
      }
      total += got;
      if ((u32)got < req.pieces[i].bytes) break;
    }
    fw_call(fw.close, 0, 0, 0, 0);
  }
  fw_call(fw.resetext, 0, 0, 0, 0);
  req.result = total;
  req.pending = 0;
  return 1;
}

/* The game task: reads the file, a piece after another, and returns the bytes read (or < 0). */
s32 file_load(const char* name, const PfPiece* pieces, u32 count) {
  req.write = 0;
  req.name = name;
  req.pieces = pieces;
  req.count = count;
  req.result = -1;
  req.pending = 1;
  while (req.pending) task_yield(10);
  return req.result;
}

/* The game task: the file made afresh with `bytes` of `data` -> bytes written, or < 0 (no write routine, or the
 * card refused). */
s32 file_save(const char* name, const void* data, u32 bytes) {
  static PfPiece piece;
  piece.dst = (void*)data;
  piece.bytes = bytes;
  req.write = 1;
  req.name = name;
  req.pieces = &piece;
  req.count = 1;
  req.result = -1;
  req.pending = 1;
  while (req.pending) task_yield(10);
  return req.result;
}

/* The game task: a file of the program's folder for reading anywhere in it (folder.c) -> 0, or < 0. */
s32 file_raw_open(const char* name, PfRawFile* f) {
  req.write = 3;
  req.name = name;
  req.raw = f;
  req.result = -1;
  req.pending = 1;
  while (req.pending) task_yield(1);
  return req.result;
}

/* The game task: `bytes` of it from `offset` into dst -> the bytes read, or < 0. A request reads at most RAW_MAX, so
 * that the main task, which decodes the panel, is never kept long. */
#define RAW_MAX 0x8000

s32 file_raw_read(const PfRawFile* f, u32 offset, void* dst, u32 bytes) {
  u32 done = 0;
  while (done < bytes) {
    const u32 n = bytes - done > RAW_MAX ? RAW_MAX : bytes - done;
    req.write = 4;
    req.raw = (PfRawFile*)f;
    req.offset = offset + done;
    req.dst = (u8*)dst + done;
    req.count = n;
    req.result = -1;
    req.pending = 1;
    while (req.pending) task_yield(1);
    if (req.result < 0) return req.result;
    done += (u32)req.result;
    if ((u32)req.result < n) break;
  }
  return (s32)done;
}

/* The game task: the files in the program's folder (folder.c) -> how many, or < 0. */
s32 file_list(PfFile* files, u32 max) {
  req.write = 2;
  req.files = files;
  req.count = max;
  req.result = -1;
  req.pending = 1;
  while (req.pending) task_yield(10);
  return req.result;
}
