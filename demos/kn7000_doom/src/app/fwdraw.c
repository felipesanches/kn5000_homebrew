/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * fwdraw.c -- the firmware's screen kept from repainting its wallpaper while the game task runs (README.md,
 * "Speed"). Answered request 5 on every converter call, program 893 changes to its text title (0x4A) again each
 * time, and each change repaints the screen: ScreenProc (0x484177CE), the base of the screen's views, answers the
 * paint event 0x600B2 with DrawWall, a RAM-to-RAM copy of the whole 640 x 240 wallpaper plane (150 KB) and the whole
 * screen marked for the LCD. In MAME, 573 of them in 57 s of play, and 31% of the CPU in that copy
 * (notes/session-2026-10-26, pf_test.lua's DrawWall census).
 * A view's procedure is found through the class table at 0x5011FA88, in RAM: group 4's entry (0x5011FAC8: id,
 * group procedure, count 0x6B, records) points at its records in flash (0x48724914, 0x18 bytes each, the procedure
 * first); record 0x41 is ScreenProc's. fwdraw_hook copies the records to FWDRAW_RECORDS (kn7.h: ARENA3's top, which
 * programs 893-941 leave alone; KN7APP.BIN has no room), puts screen_proc_hook (glue.S: 0x600B2
 * answered 0, every other event to ScreenProc) in record 0x41, and points the entry at the copy; fwdraw_unhook points
 * it back. Anything else found there (another program) is left alone.
 * Without the wallpaper each repaint is cheaper, so the converter calls come faster (33 a second in MAME, from 10),
 * and each still draws the title bar and the text box and copies their union, 640 x 216, to the LCD a dot at a time
 * (func_4841508C: 36% of the CPU in MAME even with entry.c's rest). So from the game task's start (task.c) the
 * firmware's drawing is off (fwdraw_drawing(0)): every record queued for its draw task checks
 * DrawEnable (0x5002A018) and draws nothing while it is 0, so nothing is marked changed and nothing is copied; the
 * draw task sets DrawEnable from 0x5002A01C whenever its queue runs empty, so that is 0 too. Only DrawWall writes
 * 0x5002A01C, and off its own task it waits for DrawEnable: ScreenProc's is the hook's to skip. Both go back to 1 in
 * fwdraw_unhook. DrawWall sets 0x5002A01C to 0 as it begins and to 1 as it ends, so one under way as the game task
 * starts must be let finish: both set to 0 meanwhile, the main task waited in it for good, the converter calls
 * stopped, and task.c's watch put the drawing back after 5 s (MAME, 2026-10-27; before that, a 0 written in the middle
 * of it was undone at its end and the firmware drew over the menu and the table from then on). So the drawing goes off
 * only while 0x5002A01C reads 1, at the first yield of the game task when that holds (fwdraw_quiet), and again at any
 * yield that finds it on; and the menu is drawn once that is done and the firmware's last copy to the LCD with it
 * (fwdraw_settle). With the drawing left on in the table menu (as it was until 2026-10-27), the firmware drew its
 * text titles there one over another without their wallpaper, over the menu (the owner's unit). */
#include "app.h"

#define GROUP4 0x5011FAC8u     /* group 4's entry in the class table: +8 the count, +0xC the records */
#define RECORDS 0x48724914u
#define NRECORDS 0x6B
#define RECORD_WORDS 6         /* 0x18 bytes */
#define SCREEN_RECORD 0x41
#define SCREEN_PROC 0x484177CEu
#define DRAW_ENABLE 0x5002A018u   /* nonzero: drawing allowed */
#define DRAW_KEEP 0x5002A01Cu     /* what the draw task sets DrawEnable to when its queue is empty */
#define CHANGE_PENDING 0x5002A02Cu  /* SetChangeRect's flag: a rectangle of the frame waits to be copied to the LCD */

void screen_proc_hook(void);   /* glue.S */
u32 screen_proc_skipped;       /* paints answered without DrawWall (glue.S counts them) */
#define records ((volatile u32*)FWDRAW_RECORDS)
static int hooked, quiet, want_quiet;
u32 fwdraw_reasserted;         /* times fwdraw_quiet found the drawing on again */

void fwdraw_hook(void) {
  if (hooked || RD32(GROUP4 + 0xC) != RECORDS || RD32(GROUP4 + 8) != NRECORDS ||
      RD32(RECORDS + SCREEN_RECORD * 0x18) != SCREEN_PROC)
    return;
  for (u32 i = 0; i < NRECORDS * RECORD_WORDS; ++i) records[i] = RD32(RECORDS + 4 * i);
  records[SCREEN_RECORD * RECORD_WORDS] = (u32)screen_proc_hook;
  RD32(GROUP4 + 0xC) = FWDRAW_RECORDS;  /* one store: the firmware sees the old records or the new */
  hooked = 1;
}

/* 0: the drawing goes off (now, or at the yield that finds no DrawWall under way); 1: on again. */
void fwdraw_drawing(int on) {
  if (!hooked) return;
  want_quiet = !on;
  if (!on) {
    fwdraw_quiet();
  } else if (quiet) {
    quiet = 0;
    RD32(DRAW_KEEP) = 1;
    RD32(DRAW_ENABLE) = 1;
  }
}

/* On the game task, at every yield: the drawing off, when it is wanted off and no DrawWall is under way (0x5002A01C
 * reads 1: none began, or the last one ended, which turns the drawing on again). */
void fwdraw_quiet(void) {
  if (!hooked || !want_quiet || !RD32(DRAW_KEEP)) return;
  RD32(DRAW_KEEP) = 0;
  RD32(DRAW_ENABLE) = 0;
  if (quiet) ++fwdraw_reasserted;
  quiet = 1;
}

/* On the game task, before it first draws: until no rectangle waits to be copied and the drawing has stayed off for
 * five yields of 20 ms (at most 2 s). */
void fwdraw_settle(void) {
  if (!hooked) return;
  int calm = 0;
  for (int i = 0; i < 100 && calm < 5; ++i) {
    task_yield(20);
    calm = !quiet || RD8(CHANGE_PENDING) || RD32(DRAW_KEEP) || RD32(DRAW_ENABLE) ? 0 : calm + 1;
  }
}

void fwdraw_unhook(void) {
  if (!hooked) return;
  fwdraw_drawing(1);
  want_quiet = 0;
  RD32(GROUP4 + 0xC) = RECORDS;
  hooked = 0;
}
