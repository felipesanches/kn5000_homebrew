/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * task.c -- the program task. Every firmware task runs at priority 3 and the kernel (micro-ITRON) does not time-slice;
 * the games ran at 3 too and yielded every frame. The viewer's task runs at 4, below them all: the kernel runs it only
 * while no firmware task is ready, and takes the CPU back from it as soon as one is, so the instrument plays on while
 * MuPDF draws a page for seconds (when the kernel refuses 4, at 3, and the drawing holds the firmware off). Slots 10,
 * then 9, are free (Another World's port: technics-anotherworld kn7000/src/platformcnv.inc). The kernel gives the task
 * a small stack of its own, which the task leaves at once for TASK_STACK bytes here (glue.S task_entry). */
#include "app.h"

#define TASK_ID_FIRST 10
#define TASK_ID_LAST 9
#define TASK_PRI 4      /* below the firmware's tasks */
#define TASK_PRI_ALSO 3 /* the firmware's, when the kernel refuses 4 */
#define TASK_KSTACK 0x800      /* the kernel's stack for it, used only until task_entry leaves it */
#define TASK_STACK 0x6000  /* (MuPDF: Doom's 24 KB) */

void task_entry(void);         /* glue.S */

static u32 stack[TASK_STACK / 4];
u32 task_stack_top = (u32)&stack[TASK_STACK / 4];
volatile u32 quit_req, task_done = 1;
static u32 task_id;

/* micro-ITRON's T_CTSK, as the firmware's own tasks are made (cnv_main.s game_ctsk) */
static struct {
  u32 exinf, tskatr, task;
  u16 itskpri, pad;
  u32 stksz;
} ctsk = {0, 1, (u32)task_entry, TASK_PRI, 0, TASK_KSTACK};
u32 task_pri;  /* the priority it got */

/* 0 when the task runs. */
int task_start(void) {
  for (u32 id = TASK_ID_FIRST; id >= TASK_ID_LAST; --id)
    if (fw_call(fw.cre, id, 0, (u32)&ctsk, 0) == 0 ||
        (ctsk.itskpri = TASK_PRI_ALSO, fw_call(fw.cre, id, 0, (u32)&ctsk, 0) == 0)) {
      task_pri = ctsk.itskpri;
      task_id = id;
      task_done = 0;
      fw_call(fw.sta, id, 0, 0, 0);
      fwdraw_hook();
      fwdraw_drawing(0);  /* the LCD is the game task's from here: the menu, then the tables */
      return 0;
    }
  return 1;
}

/* The firmware's yield: the ready firmware tasks run while the millisecond counter goes on by `ms`. When the
 * player has asked to quit, the task ends here instead, between frames: exd_tsk never returns. */
#define WATCH_MS 5000

/* The firmware has stopped calling the converter for WATCH_MS: it left STYLE CONVERT some other way than our 0xFE
 * (Another World's DspWatch). The LCD goes back to the firmware and the program ends at this yield. */
static void watch(void) {
  static u32 last_calls, last_ms;
  const u32 t = now_ms();
  if (calls != last_calls) {
    last_calls = calls;
    last_ms = t;
  } else if (t - last_ms >= WATCH_MS && !quit_req) {
    quit_req = 1;
    lcdc_restore();
    fwdraw_unhook();
  }
}

void task_yield(u32 ms) {
  watch();
  if (quit_req) {
    keys_sound(1);  /* the keybed sounds again, whatever the game did with it */
    keys_sync();
    task_done = 1;
    fw_call(fw.exd, 0, 0, 0, 0);
    for (;;) {}
  }
  keys_sync();
  fwdraw_quiet();
  fw_call(fw.dly, ms, 0, 0, 0);
}
