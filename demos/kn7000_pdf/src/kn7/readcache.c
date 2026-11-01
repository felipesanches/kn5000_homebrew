/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * readcache.c -- the PDF file's blocks kept, so that reading an object the viewer has read before, or one next to it,
 * does not ask the card again. On the KN7000 a read costs about 5 ms and 3.9 ms a 512-byte sector more (the unit's
 * PDFLOG.TXT: 192 bytes in 7 ms, 385012 in 3054 ms), so the bytes matter as much as the requests: blocks are small
 * (RC_BLOCK), a miss fetches only the blocks missing from what was asked, in one request, and RC_AHEAD blocks more
 * when the reads run on from the last one (a content stream read through). The least recently used block goes. */
#include <string.h>

#include "readcache.h"

typedef uint8_t u8;
typedef uint32_t u32;

#define RC_AHEAD 4

static u8* mem;
static u32 nBlocks;
static struct RcSlot { u32 block; u32 used; u8 file, valid; } slot[RC_MAX_BLOCKS];
static u32 stamp, lastEnd = ~0u;
static int lastFile = -1;
u32 rc_fetches, rc_fetch_bytes, rc_hits;

static u8* run;  /* a fetch's blocks, before they go to their slots: the memory's last RC_MAX_RUN blocks */

void rc_init(void* m, u32 bytes) {
  mem = m;
  run = mem + (bytes > RC_MAX_RUN * RC_BLOCK ? bytes - RC_MAX_RUN * RC_BLOCK : 0);
  nBlocks = bytes > RC_MAX_RUN * RC_BLOCK ? (bytes - RC_MAX_RUN * RC_BLOCK) / RC_BLOCK : 0;
  if (nBlocks > RC_MAX_BLOCKS) nBlocks = RC_MAX_BLOCKS;
  memset(slot, 0, sizeof slot);
}

void rc_forget(int file) {
  for (u32 i = 0; i < nBlocks; ++i)
    if (slot[i].file == file) slot[i].valid = 0;
  if (lastFile == file) lastFile = -1;
}

static int find(int file, u32 b) {
  for (u32 i = 0; i < nBlocks; ++i)
    if (slot[i].valid && slot[i].file == file && slot[i].block == b) return (int)i;
  return -1;
}

static int victim(void) {
  u32 best = 0;
  for (u32 i = 0; i < nBlocks; ++i) {
    if (!slot[i].valid) return (int)i;
    if (slot[i].used < slot[best].used) best = i;
  }
  return (int)best;
}

int rc_read(int file, u32 size, u32 off, void* dst, u32 n, RcFetch fetch, void* arg) {
  if (off >= size) return 0;
  if (n > size - off) n = size - off;
  if (!nBlocks) return fetch(arg, off, dst, n);
  const int seq = file == lastFile && off == lastEnd;
  lastFile = file, lastEnd = off + n;
  u8* d = dst;
  u32 done = 0;
  while (done < n) {
    const u32 at = off + done, b = at / RC_BLOCK;
    int s = find(file, b);
    if (s < 0) {
      /* the run of blocks missing from here to the end of what was asked (and RC_AHEAD more when reading on), in
       * one request */
      const u32 lastAsked = (off + n - 1) / RC_BLOCK, lastBlock = (size - 1) / RC_BLOCK;
      const u32 most = nBlocks / 2 < RC_MAX_RUN ? nBlocks / 2 : RC_MAX_RUN;
      u32 e = b;
      while (e < lastAsked && e + 1 - b < most && find(file, e + 1) < 0) ++e;
      if (seq && e == lastAsked)
        while (e < lastBlock && e - lastAsked < RC_AHEAD && e + 1 - b < most && find(file, e + 1) < 0) ++e;
      const u32 from = b * RC_BLOCK, len = (e + 1) * RC_BLOCK > size ? size - from : (e + 1 - b) * RC_BLOCK;
      if (fetch(arg, from, run, len) != (int)len) return done ? (int)done : -1;
      ++rc_fetches, rc_fetch_bytes += len;
      for (u32 c = b; c <= e; ++c) {
        const int v = victim();
        const u32 k = c - b, part = len - k * RC_BLOCK < RC_BLOCK ? len - k * RC_BLOCK : RC_BLOCK;
        memcpy(mem + (u32)v * RC_BLOCK, run + k * RC_BLOCK, part);
        slot[v].block = c, slot[v].file = (u8)file, slot[v].valid = 1, slot[v].used = ++stamp;
      }
      s = find(file, b);
    } else {
      ++rc_hits;
    }
    slot[s].used = ++stamp;
    const u32 in = at - b * RC_BLOCK;
    u32 take = RC_BLOCK - in;
    if (take > n - done) take = n - done;
    memcpy(d + done, mem + (u32)s * RC_BLOCK + in, take);
    done += take;
  }
  return (int)done;
}
