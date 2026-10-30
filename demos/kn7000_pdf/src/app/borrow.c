/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * borrow.c -- the firmware's frame buffers lent to the program while it runs, their contents kept in the LCD
 * controller's display memory meanwhile and put back before the firmware has them again. Doom's borrow.c lent six
 * buffers of the firmware's user interface (technics-roms-disasm kn7000/notes/work-ram-map.md); the viewer leaves the
 * instrument playing and its panel working, so it takes only the one that nothing but the firmware's drawing uses: the
 * frame buffer and PICTURE_PLANE2 (0x500D4080, 2 x 640 x 240 bytes; the 4 KB pages inside it in builds 893 and 941),
 * and the drawing is off while the program runs (fwdraw.c: every record queued for the draw task checks DrawEnable
 * and draws nothing). The other five serve the flash, compressed files, the SD player and its lists, which the panel
 * can reach. The copy is at LCD_KEEP, above the page the viewer draws (kn7.h). Programs 893 and 941 only. */
#include "app.h"

static const PfRegion borrow = {0x500D5000u, 0x5011E000u};  /* frame buffer, PICTURE_PLANE2 */

static int saved;
u32 borrow_check;  /* the contents' sum as saved */
u32 borrow_bad;    /* 1: the copy in display memory no longer summed to it when it went back (something wrote there) */

static volatile u16* keep(void) { return (volatile u16*)(LCD_MEM + LCD_KEEP); }

/* The region the program may have of it (into list, at most room) -> how many; 0 on other builds. */
int borrow_regions(PfRegion* list, int room) {
  if ((fw.build != 893 && fw.build != 941) || room < 1) return 0;
  list[0] = borrow;
  return 1;
}

/* Its contents into display memory, before the program has it. */
void borrow_save(void) {
  if (saved || (fw.build != 893 && fw.build != 941)) return;
  volatile u16* to = keep();
  u32 sum = 0;
  for (u32 a = borrow.lo; a < borrow.hi; a += 4) {
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
  for (u32 a = borrow.lo; a < borrow.hi; a += 4) {
    const u32 lo = *from++;
    const u32 w = lo | (u32)*from++ << 16;
    sum += w;
    RD32(a) = w;
  }
  borrow_bad = sum != borrow_check;
  saved = 0;
}
