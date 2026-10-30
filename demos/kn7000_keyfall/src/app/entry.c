/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * entry.c -- KN7APP.BIN's header and its entry, which the loader calls on every converter call, on the
 * firmware's main task (technics-roms-disasm kn7000/tools/cnv_romdump/README.md; kn7cnv.inc is the contract).
 * Each call does one step and returns. Every call but the last answers request 5, a short text, which parks
 * the firmware's user interface on the converter while the main task goes on decoding the panel and passing
 * the keybed's notes to MIDI OUT (Another World's port does the same: README-CNV.md, "How it works"):
 *   * the first calls find the firmware (fwfind.c), one step each; then the program task starts (game.c);
 *   * then each call serves the program task's file reads (files.c); and watches EXIT: held 1.5 s, the program task is
 *     asked to end at its next yield; once it has, the call answers 0xFE, which hands the instrument back (the
 *     firmware's message, then "Continue Next?": YES loads KN7APP.BIN afresh, NO leaves).
 * (The PDF viewer's entry.c, unchanged but for the names; it is Doom's, the Pinball Fantasies port's, without the
 * effects DSP: the program leaves the instrument's
 * sound alone; and with the switches the program takes named by it, PfHost::panelTake.) */
#include "app.h"
#include "build_id.h"

#define QUIT_HOLD_MS 1500
#define FAIL_CALLS 200         /* about 10 s of calls: a run that cannot start ends when EXIT cannot be read */

int cnv_entry(void);

/* ---- the application header (kn7cnv.inc AH_*): mkapp.py fills in the size and the CRC-32 ---- */
__attribute__((section(".header"), used)) static const struct {
  u32 magic, abi, size, entry, crc, base, pad[2];
  char name[32];
} header = {APP_MAGIC, LOADER_ABI, 0, (u32)cnv_entry, 0, APP_BASE, {0, 0}, "KEYFALL"};

enum { S_FIRST, S_SEARCH, S_RUN, S_FAILED };
static u32 state, step, fail_calls, exit_t0, ended;
u32 calls;
static char fail_text[40];
static const char* status_text = "LOADING KEYFALL";
char prefix[24];

void set_status(const char* s) { status_text = s; }

static void fail(const char* why, int n) {
  int i = 0;
  for (; why[i] && i < (int)sizeof fail_text - 3; ++i) fail_text[i] = why[i];
  if (n >= 0) fail_text[i++] = (char)('0' + n);
  fail_text[i] = 0;
  state = S_FAILED;
  task_done = 1;
}

/* The folder the program menu started us from (its launch block), or the card's root. */
static void launch_folder(void) {
  prefix[0] = 0;
  if (RD32(LB_ADDR + LB_MAGIC) != LAUNCH_MAGIC || RD32(LB_ADDR + LB_CHECK) != LAUNCH_CHECK) return;
  int i = 0;
  for (; i < 16 && RD8(LB_ADDR + LB_PREFIX + i); ++i) prefix[i] = (char)RD8(LB_ADDR + LB_PREFIX + i);
  prefix[i] = 0;
}

static void progress(const char* what, u32 done, u32 of) {
  static const char* shown;
  if (what != shown) {  /* (the LCD is slow: the text once) */
    shown = what;
    lcd_fill(0, 0, FB_W, FB_H, 0);
    lcd_text("KEYFALL", 292, 100, 0xFFFF, 0);
    int n = 0;
    while (what[n]) ++n;
    lcd_text(what, (FB_W - 8 * n) / 2, 120, RGB565(160, 160, 160), 0);
    lcd_fill(160, 140, 320, 4, RGB565(48, 48, 48));
  }
  lcd_fill(160, 140, (int)(320 * (unsigned long long)done / (of ? of : 1)), 4, 0xFFFF);
}

/* EXIT held for QUIT_HOLD_MS: the game task is asked to end (the firmware ignores EXIT while we answer 5). */
static void watch_exit(void) {
  if (!fw.panel || !fw.ms) return;
  if (!(panel_real[BTN_EXIT >> 8] & (BTN_EXIT & 0xFF))) {
    exit_t0 = 0;
    return;
  }
  if (!exit_t0) exit_t0 = now_ms() | 1;
  else if (now_ms() - exit_t0 >= QUIT_HOLD_MS) quit_req = 1;
}

/* The TEMPO/PROGRAM wheel's LED (cpr_led97) blinking while the game wants the wheel (PfHost::wheelLed): the firmware
 * keeps its panel LEDs in a shadow in RAM and sends a register to the panel when it differs from what was sent last
 * (kn7000_mame notes/panel-serial-protocol.md), so the LED is the shadow's bit, flipped here, on the main task, by
 * bset and bclr (the firmware's tasks change other bits of those bytes). Both banks: the firmware sends the one its
 * panel kind uses (bank A with the tone generators, bank B without), the other is never sent. The bits as the
 * firmware had them go back when the blinking stops, and at the end. */
#define LED_HALF_MS 300
static volatile int led_want;
static int led_on, led_lit, led_saved[2];
static u32 led_t;
static const u8 led_mask[2] = {0x02, 0x80};

void wheel_led(int blink) { led_want = blink; }

static void led_bit(int k, int on) {
  const u32 a = fw.dial[k];
  const u32 m = led_mask[k];
  if (on) __asm__ volatile("bset %1,(%0)" : : "a"(a), "d"(m) : "memory");
  else __asm__ volatile("bclr %1,(%0)" : : "a"(a), "d"(m) : "memory");
}

static void led_service(void) {
  if (!fw.dial[0] || !fw.ms) return;
  if (led_want && !led_on) {
    for (int k = 0; k < 2; ++k) led_saved[k] = (RD8(fw.dial[k]) & led_mask[k]) != 0;
    led_on = 1, led_lit = 0, led_t = now_ms() - LED_HALF_MS;
  }
  if (led_on && !led_want) {
    for (int k = 0; k < 2; ++k) led_bit(k, led_saved[k]);
    led_on = 0;
    return;
  }
  if (led_on && now_ms() - led_t >= LED_HALF_MS) {
    led_t = now_ms();
    led_lit = !led_lit;
    for (int k = 0; k < 2; ++k) led_bit(k, led_lit);
  }
}

/* While the game task runs, each call waits a while before it answers. Program 893 answers request 5 by changing to
 * its text title again, which repaints the screen and calls again: a loop on the main task as fast as it goes, 33
 * calls a second in MAME with the wallpaper left out (fwdraw.c), each costing the game the CPU it takes. The main task
 * also decodes the panel, so the wait ends as soon as the panel's receive ring holds bytes the main task has not
 * decoded (PanelFrameDecode, 0x484ACF04: 0x5C bytes at 0x5006BDB0, u16 read and write indexes at 0x5006BDAC and
 * 0x5006BDAE), when the game task wants a file or has ended, or after REST_MS. It sleeps on the kernel's dly_tsk,
 * REST_TICKS at a time, so the other tasks run meanwhile. A raw read (a part of a file) asked for meanwhile is served at
 * once and the wait goes on: Doom reads its WAD in hundreds of small pieces, and a converter call each cost 30 ms in
 * MAME. Program 893 only. */
#define REST_MS 100
#define REST_TICKS 2
#define PANEL_RX_RD 0x5006BDACu
#define PANEL_RX_WR 0x5006BDAEu
u32 rest_calls, rest_early;  /* calls that waited; of them, those the panel or a file ended early */
u32 rest_raw;                /* raw reads served during a wait (a converter call serves as many as come in REST_MS) */

/* The TEMPO/PROGRAM wheel, for the game (PfHost::wheel). Each detent comes from the panel as a frame [0x17, step] (a
 * signed step) into the receive ring, and the firmware adds the step to the tempo (kn7000_mame notes,
 * slider-cp-protocol.md: handler 0x484AD6A0). Every frame of the panel is two bytes, and the ring keeps what it held
 * after it is decoded, so wheel_scan follows the ring a frame at a time from where the firmware's read index stood
 * when it began: a wheel frame's step is added to wheel_steps and, while the firmware has not decoded it yet, set
 * to 0 there, so that the tempo stays as it is. It runs on the firmware's main task (the converter calls), which
 * decodes the ring, so a frame is either still to be decoded or decoded already as it looks. */
#define PANEL_RING 0x5006BDB0u
#define PANEL_RING_LEN 0x5C
volatile s32 wheel_steps;
u32 wheel_frames, wheel_late;  /* wheel frames seen; of them, those the firmware had decoded first (the tempo moved) */
u32 wheel_undone;              /* frames put in the ring to take a late frame's step back */
static u32 wheel_at;
static int wheel_on;

/* A wheel frame the firmware decoded before wheel_scan took it moved the tempo by its step: a frame of the opposite step
 * goes into the ring after the panel's, as the panel would send it, and the tempo goes back (PANEL_RING's write index
 * moved with interrupts off, as the receive interrupt moves it). wheel_scan passes over the frames it wrote. */
#define UNDONE_MAX 8
static u32 undone_at[UNDONE_MAX], undone_n;

static void wheel_undo(signed char step) {
  if (undone_n >= UNDONE_MAX || !step) return;
  u32 psw;
  __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
  volatile u16* const wrp = (volatile u16*)PANEL_RX_WR;
  const u32 wr = *wrp, rd = *(volatile u16*)PANEL_RX_RD;
  const u32 filled = (wr + PANEL_RING_LEN - rd) % PANEL_RING_LEN;
  if (wr < PANEL_RING_LEN && !(wr & 1) && filled + 2 < PANEL_RING_LEN) {
    volatile u8* const ring = (volatile u8*)PANEL_RING;
    ring[wr] = 0x17;
    ring[(wr + 1) % PANEL_RING_LEN] = (u8)(-step);
    *wrp = (u16)((wr + 2) % PANEL_RING_LEN);
    undone_at[undone_n++] = wr;
    ++wheel_undone;
  }
  __asm__ volatile("mov %0,psw\n\tnop\n\tnop" ::"d"(psw) : "memory");
}

/* 1 when the frame at `at` is one wheel_undo wrote (it is forgotten then) */
static int undone_frame(u32 at) {
  for (u32 i = 0; i < undone_n; ++i)
    if (undone_at[i] == at) {
      undone_at[i] = undone_at[--undone_n];
      return 1;
    }
  return 0;
}

/* The panel's switches, some kept from the firmware (ProTracker's PLAN-EDITING.md, "Risks"). A switch frame is [ADDR, DATA]:
 * ADDR's bits 7-6 the bank (00 the CPR board, 11 the CPL and CPC boards), bits 3-0 the segment; DATA the segment's
 * switches, bit n switch n, 1 pressed, the whole segment each time; the firmware finds the presses by XOR with its own
 * copy (kn7000_mame notes/panel-serial-protocol.md, "Switch-report frame"). In MAME, while the converter ran, the
 * firmware still acted on some: R1/R2 OCTAVE moved the keybed's notes an octave, and PANEL MEMORY 1 recalled a
 * registration, which reloaded the effects DSP over the mixer (the sound stopped). So panel_scan keeps each segment's
 * DATA in panel_real (PfHost::panel: the program reads the switches there, laid out as the firmware's bitmap: CPR's
 * segments at 0-9, CPL's and CPC's at 0x30 + segment) and, in a frame the firmware has not decoded yet, clears the
 * switches the program takes (panel_take, PfHost::panelTake; until it names them, every switch but CONTRAST UP and DOWN):
 * to the firmware those never change, and the others act as ever. Program 893 only, as the wheel. */
u8 panel_real[0x40];
static u8 taken[0x40];
static int taken_set;

void panel_take(const u8* mask) {
  for (u32 i = 0; i < sizeof taken; ++i) taken[i] = mask[i];
  taken_set = 1;
}

static u8 take_of(u32 idx) {
  if (taken_set) return taken[idx];
  return idx == 0x30 + 5 ? (u8)~0x0C : 0xFF;  /* (CPC_SEG5: CONTRAST UP and DOWN stay the firmware's) */
}
u32 panel_frames, panel_late;  /* switch frames seen; of them, those the firmware had decoded first */
u8 panel_late_log[16];         /* the last 8 of those: ADDR, DATA (a press the firmware saw, or a harmless release) */
static int panel_copied;

void wheel_scan(void) {
  if (!panel_copied && fw.panel) {  /* the switches as they are when the scan begins */
    panel_copied = 1;
    for (u32 i = 0; i < sizeof panel_real; ++i) panel_real[i] = RD8(fw.panel + i);
  }
  if (fw.build != 893) {  /* (no scan: the firmware's own bitmap, unfiltered) */
    for (u32 i = 0; i < sizeof panel_real; ++i) panel_real[i] = RD8(fw.panel + i);
    return;
  }
  const u32 rd = *(volatile u16*)PANEL_RX_RD, wr = *(volatile u16*)PANEL_RX_WR;
  if (rd >= PANEL_RING_LEN || wr >= PANEL_RING_LEN) return;
  if (!wheel_on) {
    wheel_on = 1;
    wheel_at = rd;
  }
  for (int n = 0; n < PANEL_RING_LEN / 2 && wheel_at != wr; ++n) {
    const u32 next = (wheel_at + 1) % PANEL_RING_LEN;
    if (next == wr) break;  /* (half a frame so far) */
    volatile u8* const ring = (volatile u8*)PANEL_RING;
    const u8 addr = ring[wheel_at];
    const u32 type = (addr >> 4) & 3, bank = addr & 0xC0;
    if (addr != 0x17 && type == 0 && (bank == 0 || bank == 0xC0)) {  /* a switch frame (types 0 and 1: bit 3 the segment's) */
      const u32 idx = (bank ? 0x30 : 0) + (addr & 0x0F);
      const u8 data = ring[next];
      panel_real[idx] = data;
      ++panel_frames;
      const u32 ahead = (wheel_at + PANEL_RING_LEN - rd) % PANEL_RING_LEN, filled = (wr + PANEL_RING_LEN - rd) % PANEL_RING_LEN;
      if (ahead < filled) {
        ring[next] = (u8)(data & ~take_of(idx));
      } else {
        panel_late_log[(panel_late % 8) * 2] = addr;
        panel_late_log[(panel_late % 8) * 2 + 1] = data;
        ++panel_late;
      }
    } else if (addr == 0x17 && !undone_frame(wheel_at)) {
      const signed char step = (signed char)ring[next];
      wheel_steps += step;
      ++wheel_frames;
      /* still to be decoded: between the read index and the write index */
      const u32 ahead = (wheel_at + PANEL_RING_LEN - rd) % PANEL_RING_LEN, filled = (wr + PANEL_RING_LEN - rd) % PANEL_RING_LEN;
      if (ahead < filled) {
        ring[next] = 0;
      } else {
        ++wheel_late;
        wheel_undo(step);
      }
    }
    wheel_at = (wheel_at + 2) % PANEL_RING_LEN;
  }
}

/* A frame is two bytes and the panel sends them one after the other. The main task decodes the ring as soon as a
 * converter call returns, so every way out of the call takes the ring's frames first (scan_whole): when a frame's
 * first byte has come, it waits up to FRAME_WAIT_MS for the second. In MAME, returning on a first byte let a third of
 * the switch frames through; returning at the end of the rest without a last look let PANEL MEMORY 1 through. */
#define FRAME_WAIT_MS 6

static void scan_whole(void) {
  const u32 t1 = now_ms();
  for (;;) {
    wheel_scan();
    if (wheel_at == *(volatile u16*)PANEL_RX_WR || now_ms() - t1 >= FRAME_WAIT_MS) return;
    fw_call(fw.dly, 1, 0, 0, 0);  /* (half a frame: its second byte is on its way) */
  }
}

static void rest(void) {
  if (fw.build != 893 || !fw.dly || !fw.ms) return;
  ++rest_calls;
  const u32 t0 = now_ms();
  while (now_ms() - t0 < REST_MS) {
    if (file_raw_pending()) {  /* a part of a file (the program): read now, not a converter call later */
      file_serve();
      ++rest_raw;
      scan_whole();
      continue;
    }
    if (*(volatile u16*)PANEL_RX_RD != *(volatile u16*)PANEL_RX_WR || file_pending() || task_done || quit_req) {
      ++rest_early;
      break;
    }
    fw_call(fw.dly, REST_TICKS, 0, 0, 0);
  }
  scan_whole();  /* (the bytes that came meanwhile, before the main task decodes them) */
}

static int answer(const char* text) {
  volatile u8* m = (volatile u8*)MAILBOX;
  m[0] = RQ_TEXT;
  int i = 0;
  for (; text[i] && i < 60; ++i) m[1 + i] = (u8)text[i];
  m[1 + i] = 0;
  return 0;
}


int cnv_entry(void) {
  ++calls;
  if (ended) {  /* "Continue Next?" YES without a reload (the loader always reloads): start again */
    ended = 0;
    quit_req = 0;
    exit_t0 = 0;
    state = S_FIRST;
    step = 0;
  }
  switch (state) {
    case S_FIRST:
      lcdc_save();
      launch_folder();
      diag("KN7APP.BIN (Keyfall, build %s) starts; folder \"%s\"", BUILD_ID, prefix);
      state = S_SEARCH;
      progress("Looking for the firmware's routines...", 0, FW_STEPS);
      /* fall through */
    case S_SEARCH:
      if (fw_step((int)step)) {
        diag("firmware search: step %u failed", step);
        fail("KEYFALL: UNKNOWN FIRMWARE, STEP ", (int)step);
        break;
      }
      diag("firmware search: step %u found", step);
      progress("Looking for the firmware's routines...", ++step, FW_STEPS);
      if (step < FW_STEPS) break;
      notes_init();
      fw_find_dial();
      diag("firmware: PROGRAM %u; open %08X read %08X close %08X write %08X raw %08X; cre %08X sta %08X dly %08X exd %08X",
           fw.build, fw.open, fw.read, fw.close, fw.write, fw.raw, fw.cre, fw.sta, fw.dly, fw.exd);
      diag("firmware: ms %08X panel %08X MIDI IN 1 %08X OUT 1 %08X dial %08X %08X", fw.ms, fw.panel, fw.midiIn1,
           fw.midiOut1, fw.dial[0], fw.dial[1]);
      state = S_RUN;
      if (task_start()) {
        diag("task: no slot");
        fail("KEYFALL: NO TASK SLOT", -1);
      }
      break;
    case S_RUN:
      wheel_scan();
      led_service();
      /* A part of a file is served inside rest(), which keeps the main task in the call: returned at once, as when
       * every call served the read waiting for it, the call let the main task decode the frames that came before the
       * next, and in MAME 80% of the wheel's frames reached the firmware (its tempo) while the viewer drew a page. */
      if (file_pending() && !file_raw_pending() && file_serve()) scan_whole();
      else rest();
      break;
    case S_FAILED:
      if (fw.panel ? button(BTN_EXIT) : ++fail_calls >= FAIL_CALLS) quit_req = 1;
      break;
  }
  if (state != S_FAILED) watch_exit();
  if (quit_req && task_done) {
    diag("the end: converter calls %u", calls);
    led_want = 0;
    led_service();  /* the wheel's LED as the firmware had it */
    wd_disarm();
    crash_disarm();  /* the firmware's NMI stub back */
    fwdraw_unhook();  /* its screen procedure */
    lcdc_restore();  /* and its LCD start address */
    *(volatile u8*)MAILBOX = RQ_END;
    ended = 1;
    return 0;
  }
  return answer(state == S_FAILED ? fail_text : status_text);
}
