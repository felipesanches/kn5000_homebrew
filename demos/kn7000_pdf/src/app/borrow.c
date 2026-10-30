/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * borrow.c -- firmware RAM lent to the program while it runs, its contents kept in the LCD controller's display memory
 * meanwhile and put back before the firmware has it again. Doom needs about 1.7 MB and the free regions (kn7.h ARENA1-7)
 * hold 1.3 MB; these buffers serve features of the firmware's user interface, which the main task does not reach while
 * it sits in the converter's loop, and the firmware's drawing, which is off meanwhile (fwdraw.c).
 * Where (technics-roms-disasm kn7000/notes/work-ram-map.md, build 941's addresses; build 893's lie 4 bytes lower up to
 * 0x500A0000 and 0x24 bytes lower above it: SdPlaylistDbClear, FavFormat, SdStateReset, LcdOut and the LZSS
 * variables, read in both builds' disassembly), each range the 4 KB pages inside the buffer in both builds:
 *   * the custom-flash 64 KB sector buffer (0x5004FB68), read-modify-write of a flash sector;
 *   * the 64 KB compressed-file stream buffer (0x500712D0);
 *   * the SD card favourites' working copy (0x50083D72, 0xD620 bytes);
 *   * the frame buffer and PICTURE_PLANE2 (0x500D4080, 2 x 640 x 240 bytes), the firmware's drawing;
 *   * the LZSS encoder's trees (0x501523E4, 0xE210 bytes);
 *   * the SD playlist and audio-list database (0x50160E98, 0x11BB8 bytes).
 * In MAME (research-scratch aw-memory-plan/taps893_ranges.out, program 893) no part of the firmware read or wrote any
 * of these pages while a converter ran. Programs 893 and 941 only. */
#include "app.h"

typedef struct Borrow {
  u32 lo, hi;
} Borrow;

static const Borrow borrow[] = {
    {0x50050000u, 0x5005F000u},  /* custom-flash sector buffer */
    {0x50072000u, 0x50081000u},  /* compressed-file stream buffer */
    {0x50084000u, 0x50091000u},  /* SD favourites */
    {0x500D5000u, 0x5011E000u},  /* frame buffer, PICTURE_PLANE2 */
    {0x50153000u, 0x50160000u},  /* LZSS trees */
    {0x50161000u, 0x50172000u},  /* SD playlist database */
};
#define NBORROW (sizeof borrow / sizeof *borrow)

static int saved;
u32 borrow_check;  /* the contents' sum as saved */
u32 borrow_bad;    /* 1: the copy in display memory no longer summed to it when it went back (something wrote there) */

static volatile u16* keep(void) { return (volatile u16*)(LCD_MEM + LCD_BORROW); }

/* The regions the program may have of these (into list, at most room) -> how many; 0 on other builds. */
int borrow_regions(PfRegion* list, int room) {
  if (fw.build != 893 && fw.build != 941) return 0;
  int n = 0;
  for (u32 i = 0; i < NBORROW && n < room; ++i) list[n++] = (PfRegion){borrow[i].lo, borrow[i].hi};
  return n;
}

/* Their contents into display memory, before the program has them. */
void borrow_save(void) {
  if (saved || (fw.build != 893 && fw.build != 941)) return;
  volatile u16* to = keep();
  u32 sum = 0;
  for (u32 i = 0; i < NBORROW; ++i)
    for (u32 a = borrow[i].lo; a < borrow[i].hi; a += 4) {
      const u32 w = RD32(a);
      sum += w;
      *to++ = (u16)w;
      *to++ = (u16)(w >> 16);
    }
  borrow_check = sum;
  saved = 1;
}

/* ... and back, once the program has ended (entry.c, task.c's watch). */
void borrow_restore(void) {
  if (!saved) return;
  const volatile u16* from = keep();
  u32 sum = 0;
  for (u32 i = 0; i < NBORROW; ++i)
    for (u32 a = borrow[i].lo; a < borrow[i].hi; a += 4) {
      const u32 lo = *from++;
      const u32 w = lo | (u32)*from++ << 16;
      sum += w;
      RD32(a) = w;
    }
  borrow_bad = sum != borrow_check;
  saved = 0;
}

/* The bytes the saved contents take in display memory. */
u32 borrow_bytes(void) {
  u32 n = 0;
  for (u32 i = 0; i < NBORROW; ++i) n += borrow[i].hi - borrow[i].lo;
  return n;
}
