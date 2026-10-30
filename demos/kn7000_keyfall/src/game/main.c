/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * main.c -- KEYFALL.BIN, run by KN7APP.BIN (src/app/) on its program task: its header (pfhost.h PfGameHeader, which
 * tools/build/mkgame.py completes), its memory and its log. The image and the bss are in the program area (GAME_BASE:
 * keyfall.ld); the songs and charts are made in ARENA2, from its start, and given back when another song is chosen
 * (kf_mark, kf_release). */
#include "kf.h"

extern char __bss_lo[], __bss_hi[], __init_lo[], __init_hi[], __game_end[];
int kf_entry(const PfHost *h);
int kf_main(void);  /* menu.c */

__attribute__((section(".header"), used)) const PfGameHeader kf_header = {
    PFGAME_MAGIC, PFHOST_ABI, 0, KEYFALL_PROGRAM, (u32)__bss_lo, (u32)__bss_hi, (u32)__init_lo, (u32)__init_hi,
    (u32)__game_end, kf_entry, 0, (u32)&kf_header, {0, 0, 0, 0}};

const PfHost *host;

/* ---- memory: ARENA2, handed out from its start ---- */
static u32 heapAt = ARENA2_LO;
u32 heap_peak;

void *kf_alloc(u32 bytes) {
  bytes = (bytes + 3) & ~3u;
  if (bytes > ARENA2_HI - heapAt) return 0;
  void *p = (void *)heapAt;
  heapAt += bytes;
  if (heapAt - ARENA2_LO > heap_peak) heap_peak = heapAt - ARENA2_LO;
  kf_memset(p, 0, bytes);
  return p;
}
u32 kf_mark(void) { return heapAt; }
void kf_release(u32 mark) {
  if (mark >= ARENA2_LO && mark <= heapAt) heapAt = mark;
}

/* ---- the log: KN7APP's (KFLOG.TXT on the card when saved) ---- */
void kf_log(const char *fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  kf_vfmt(b, sizeof b, fmt, ap);
  va_end(ap);
  host->diag(b);
}

int kf_entry(const PfHost *h) {
  host = h;
  heapAt = ARENA2_LO;
  kf_log("KEYFALL.BIN: PROGRAM %u, folder \"%s\", ABI %u", h->build, h->prefix, h->abi);
  return kf_main();
}
