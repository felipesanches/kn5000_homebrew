/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * task.c -- the game task. Every firmware task runs at priority 3 and the kernel (micro-ITRON) does not
 * time-slice, so ours runs at 3 too and yields every frame; slots 10, then 9, are free (Another World's port:
 * technics-anotherworld kn7000/src/platformcnv.inc). The kernel gives the task a small stack of its own, which
 * the task leaves at once for TASK_STACK bytes here (glue.S task_entry): Encore's engine is C++ and wants more. */
#include "app.h"

#define TASK_ID_FIRST 10
#define TASK_ID_LAST 9
#define TASK_PRI 3
#define TASK_KSTACK 0x800      /* the kernel's stack for it, used only until task_entry leaves it */
#define TASK_STACK 0x6000  /* (Doom: ProTracker's 16 KB and more) */

void task_entry(void);         /* glue.S */

static u32 stack[TASK_STACK / 4];
u32 task_stack_top = (u32)&stack[TASK_STACK / 4];
volatile u32 quit_req, task_done = 1;
static u32 task_id;

/* micro-ITRON's T_CTSK, as the firmware's own tasks are made (cnv_main.s game_ctsk) */
static const struct {
  u32 exinf, tskatr, task;
  u16 itskpri, pad;
  u32 stksz;
} ctsk = {0, 1, (u32)task_entry, TASK_PRI, 0, TASK_KSTACK};

/* 0 when the task runs. */
int task_start(void) {
  for (u32 id = TASK_ID_FIRST; id >= TASK_ID_LAST; --id)
    if (fw_call(fw.cre, id, 0, (u32)&ctsk, 0) == 0) {
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

/* The firmware has stopped calling the converter for WATCH_MS while the DSP's slots are ours: it left STYLE CONVERT
 * some other way than our 0xFE (Another World's DspWatch). The voices stop and the firmware's effects go back. */
static void watch(void) {
  static u32 last_calls, last_ms;
  const u32 t = now_ms();
  if (calls != last_calls) {
    last_calls = calls;
    last_ms = t;
  } else if (dsp.hooked && t - last_ms >= WATCH_MS) {
    for (u32 v = 0; v < 8; ++v) dsp_set(v, 0, 0);
    dsp_flush();
    dsp_quit();
    borrow_restore();  /* (the program cannot go on without that RAM: it ends at this yield) */
    quit_req = 1;
    fwdraw_unhook();
    dsp.fail = DF_WATCH;
    dsp.ready = 0;
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
