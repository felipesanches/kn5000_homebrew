/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * entry.c -- KN7APP.BIN's header and its entry, which the loader calls on every converter call, on the
 * firmware's main task (technics-roms-disasm kn7000/tools/cnv_romdump/README.md; kn7cnv.inc is the contract).
 * Each call does one step and returns. Every call but the last answers request 5, a short text, which parks
 * the firmware's user interface on the converter while the main task goes on decoding the panel and passing
 * the keybed's notes to MIDI OUT (Another World's port does the same: README-CNV.md, "How it works"):
 *   * the first calls find the firmware (fwfind.c), one step each, and the firmware's routines for the effects
 *     DSP (dsp.c); then the DSP gets the mixer, and the game task starts (game.c). Without the DSP the game goes on
 *     silent;
 *   * then each call serves the game task's file reads (files.c) and, when a table is chosen, sends that table's
 *     sample bank (PFtSn.BIN) to the DSP, a slice of work a call (bank_load); and watches EXIT: held 1.5 s, the game task
 *     is asked to end at its next yield; once it has, the call answers 0xFE, which hands the instrument back
 *     (the firmware's message, then "Continue Next?": YES loads KN7APP.BIN afresh, NO leaves). */
#include "app.h"

#define QUIT_HOLD_MS 1500
#define FAIL_CALLS 200         /* about 10 s of calls: a run that cannot start ends when EXIT cannot be read */

int cnv_entry(void);

/* ---- the application header (kn7cnv.inc AH_*): mkapp.py fills in the size and the CRC-32 ---- */
__attribute__((section(".header"), used)) static const struct {
  u32 magic, abi, size, entry, crc, base, pad[2];
  char name[32];
} header = {APP_MAGIC, LOADER_ABI, 0, (u32)cnv_entry, 0, APP_BASE, {0, 0}, "PINBALL FANTASIES"};

enum { S_FIRST, S_SEARCH, S_DSP, S_RUN = 4, S_FAILED };  /* (3 was the four tables' bank, sent before the menu) */
static u32 state, step, part, fail_calls, exit_t0, ended;
u32 calls;
static DspBankTable bank_table;
static char fail_text[40];
static const char* status_text = "LOADING PINBALL FANTASIES";
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
#ifdef PF_DIAG
  /* started from its own folder (PFDIAG\): the game's files are in PINBALL\ */
  const char* own = "PINBALL\\";
  for (i = 0; own[i]; ++i) prefix[i] = own[i];
  prefix[i] = 0;
#endif
}

static void progress(const char* what, u32 done, u32 of) {
  static const char* shown;
  if (what != shown) {  /* (the LCD is slow: the text once) */
    shown = what;
    lcd_fill(0, 0, FB_W, FB_H, 0);
    lcd_text("PINBALL FANTASIES", 248, 100, 0xFFFF, 0);
    int n = 0;
    while (what[n]) ++n;
    lcd_text(what, (FB_W - 8 * n) / 2, 120, RGB565(160, 160, 160), 0);
    lcd_fill(160, 140, 320, 4, RGB565(48, 48, 48));
  }
  lcd_fill(160, 140, (int)(320 * (unsigned long long)done / (of ? of : 1)), 4, 0xFFFF);
}

/* The sound, in three calls: the DSP's routines, PFBANK.BIN, then the mixer into the DSP (dsp_prepare). 2 while
 * there is more; 0 when the DSP is ours; 1 without it. */
static int dsp_start(u32 n) {
  if (n == 0) {
    dsp_find();
    return 2;
  }
  if (n == 1) {
    const s32 got = file_read_now("PFBANK.BIN", &bank_table, sizeof bank_table);
    if (got != (s32)sizeof bank_table || bank_table.magic != 0x4B424650u /* "PFBK" */ || bank_table.version != 3) {
      dsp.fail = DF_BANKTAB;
      return 1;
    }
    return 2;
  }
  return dsp_prepare();
}

/* A table's bank into the DSP, asked for on the game task (bank_load) and sent by the converter calls (bank_serve). */
static volatile s32 bank_want = -1, bank_result;
static int bank_begun;

/* On the game task: table t's sounds into the DSP, waiting; 1 when they are there, else 2 (no sound). The LCD shows how
 * far it has got. */
int bank_load(int t) {
  if (dsp.state != DS_IDLE && dsp.state != DS_READY) return 2;
  if (dsp.state == DS_READY && dsp.loaded == t) return 1;
  bank_want = t;
  u32 shown = 0xFFFFFFFFu;
  int checking = -1;
  while (bank_want >= 0) {
    if (dsp.state == DS_BANK && dsp.banks) {
      const int c = dsp.sent == 2;
      if (c != checking) {
        checking = c;
        lcd_fill(0, 196, FB_W, 24, 0);
        const char* what = c ? "Checking the sounds in the DSP..." : "Sending the sounds to the DSP...";
        int n = 0;
        while (what[n]) ++n;
        lcd_text(what, (FB_W - 8 * n) / 2, 196, RGB565(160, 160, 160), 0);
        lcd_fill(160, 214, 320, 4, RGB565(48, 48, 48));
      }
      const u32 w = dsp.to_send ? (u32)(320ull * (dsp.done < dsp.to_send ? dsp.done : dsp.to_send) / dsp.to_send) : 0;
      if (w != shown) lcd_fill(160, 214, (int)(shown = w), 4, 0xFFFF);
    }
    task_yield(50);
  }
  return bank_result;
}

/* On a converter call: 1 when it did bank work. */
static int bank_serve(void) {
  if (bank_want < 0) return 0;
  int r;
  if (!bank_begun) {
    bank_begun = 1;
    r = dsp_bank_begin(&bank_table, bank_want);
  } else {
    r = dsp_bank_step((u8*)ARENA2_LO);  /* (no table program is loaded while a bank is sent: its RAM is free) */
  }
  if (r) {
    bank_result = r;
    bank_begun = 0;
    bank_want = -1;
  }
  return 1;
}

#ifdef PF_DIAG
#define DIAG_HOLD_MS 1000  /* before each of the setup's calls that works: time to read the text */
static u32 hold_t0, hold_calls;
static char diag_text[64];

/* The card log, C:\PFDIAG.TXT (PF_HELLO: C:\PFHELLO.TXT), made afresh with each line, so that after a restart its
 * last line is what the unit was doing: "<call> <ms> <text>". Through the loader's service table (no search
 * needed), drive switch, open "wb", one write, close, as the ROM dumper writes its files. */
#if defined PF_HELLO && defined PF_ALIGN_ENTRY
#define LOG_NAME "PFHELLOA.TXT"
#elif defined PF_HELLO
#define LOG_NAME "PFHELLO.TXT"
#else
#define LOG_NAME "PFDIAG.TXT"
#endif
static char log_buf[512];  /* (2 KB until 2026-10-27, when KN7APP.BIN grew to 2 KB short of APP_END: the first lines are the ones that matter) */
static u32 log_len, logged_writes;

static void log_dec(u32 v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n && log_len < sizeof log_buf) log_buf[log_len++] = t[--n];
}

static void diag_log(const char* s) {
  if (!(RD32(SVC + SV_FOUND) & (FOUND_FILESIG | FOUND_TABLES))) return;
  const u32 setext = RD32(SVC + SV_SETEXT), resetext = RD32(SVC + SV_RESETEXT), open = RD32(SVC + SV_OPEN),
            write = RD32(SVC + SV_WRITE), close = RD32(SVC + SV_CLOSE), c = RD32(SVC + SV_MSCOUNT);
  if (!setext || !resetext || !open || !write || !close) return;
  if (log_len > sizeof log_buf - 80) return;  /* full: the first lines are the ones that matter */
  log_dec(calls);
  log_buf[log_len++] = ' ';
  log_dec(c ? RD32(c) : 0);
  log_buf[log_len++] = ' ';
  while (*s && log_len < sizeof log_buf - 2) log_buf[log_len++] = *s++;
  log_buf[log_len++] = '\r';
  log_buf[log_len++] = '\n';
  fw_call(setext, 1, 0, 0, 0);
  const s32 h = (s32)(int16_t)fw_call(open, 0, 0, (u32)LOG_NAME, (u32) "wb");
  if (h >= 0) {
    fw_call(write, log_len, 0, (u32)log_buf, 0);
    fw_call(close, 0, 0, 0, 0);
    ++logged_writes;
  }
  fw_call(resetext, 0, 0, 0, 0);
}

/* 1 while the call is only to keep the text up (the setup only: the game task's file reads are not held). */
static int diag_hold(void) {
  const u32 c = RD32(SVC + SV_MSCOUNT);
  if (!c) return hold_calls++ % 20 != 0;
  const u32 t = RD32(c) | 1;
  if (!hold_t0) hold_t0 = t;
  if (t - hold_t0 < DIAG_HOLD_MS) return 1;
  hold_t0 = 0;
  return 0;
}

/* "PF DIAG <call>: NEXT <what the next call that works does>" */
static const char* diag_next(void) {
  static const char* const search[FW_STEPS] = {"PANEL", "CLOCK", "SERVICE TABLE", "TASKS", "EXIT", "MIDI RINGS"};
  static const char* const dsp_parts[3] = {"DSP SEARCH", "READ PFBANK.BIN", "DSP MIXER, HOOK, TASK"};
  const char* what = state == S_SEARCH ? search[step < FW_STEPS ? step : 0]
                     : state == S_DSP  ? dsp_parts[part < 3 ? part : 2]
                                       : 0;
  if (!what) return state == S_FAILED ? fail_text : status_text;
  char* o = diag_text;
  for (const char* s = "PF DIAG "; *s;) *o++ = *s++;
  char t[10];
  int n = 0;
  u32 v = calls;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  for (const char* s = ": NEXT "; *s;) *o++ = *s++;
  if (state == S_SEARCH) for (const char* s = "SEARCH "; *s;) *o++ = *s++;
  while (*what && o < diag_text + sizeof diag_text - 1) *o++ = *what++;
  *o = 0;
  return diag_text;
}
#endif

/* EXIT held for QUIT_HOLD_MS: the game task is asked to end (the firmware ignores EXIT while we answer 5). */
static void watch_exit(void) {
  if (!fw.panel || !fw.ms) return;
  if (!button(BTN_EXIT)) {
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
#ifdef PF_DIAG
void wheel_led(int blink) { (void)blink; }  /* (left out of the diagnostic build: KN7APP's room, kn7.h APP_END) */
#define led_service() ((void)0)
static int led_want;
#else
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
#endif

/* While the game task runs, each call waits a while before it answers. Program 893 answers request 5 by changing to
 * its text title again, which repaints the screen and calls again: a loop on the main task as fast as it goes, 33
 * calls a second in MAME with the wallpaper left out (fwdraw.c), each costing the game the CPU it takes. The main task
 * also decodes the panel, so the wait ends as soon as the panel's receive ring holds bytes the main task has not
 * decoded (PanelFrameDecode, 0x484ACF04: 0x5C bytes at 0x5006BDB0, u16 read and write indexes at 0x5006BDAC and
 * 0x5006BDAE), when the game task wants a file or has ended, or after REST_MS. It sleeps on the kernel's dly_tsk,
 * REST_TICKS at a time, so the other tasks run meanwhile. Program 893 only. */
#define REST_MS 100
#define REST_TICKS 2
#define PANEL_RX_RD 0x5006BDACu
#define PANEL_RX_WR 0x5006BDAEu
u32 rest_calls, rest_early;  /* calls that waited; of them, those the panel or a file ended early */

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
static u32 wheel_at;
static int wheel_on;

static void wheel_scan(void) {
  if (fw.build != 893) return;
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
    if (ring[wheel_at] == 0x17) {
      wheel_steps += (signed char)ring[next];
      ++wheel_frames;
      /* still to be decoded: between the read index and the write index */
      const u32 ahead = (wheel_at + PANEL_RING_LEN - rd) % PANEL_RING_LEN, filled = (wr + PANEL_RING_LEN - rd) % PANEL_RING_LEN;
      if (ahead < filled) ring[next] = 0;
      else ++wheel_late;
    }
    wheel_at = (wheel_at + 2) % PANEL_RING_LEN;
  }
}

static void rest(void) {
  if (fw.build != 893 || !fw.dly || !fw.ms) return;
  ++rest_calls;
  const u32 t0 = now_ms();
  while (now_ms() - t0 < REST_MS) {
    if (*(volatile u16*)PANEL_RX_RD != *(volatile u16*)PANEL_RX_WR || file_pending() || bank_want >= 0 || task_done ||
        quit_req) {
      ++rest_early;
      wheel_scan();  /* (the bytes that ended the rest, before the main task decodes them) */
      return;
    }
    fw_call(fw.dly, REST_TICKS, 0, 0, 0);
  }
}

static int answer(const char* text) {
  volatile u8* m = (volatile u8*)MAILBOX;
  m[0] = RQ_TEXT;
  int i = 0;
  for (; text[i] && i < 60; ++i) m[1 + i] = (u8)text[i];
  m[1 + i] = 0;
  return 0;
}

#ifdef PF_HELLO
/* PFHELLO\: a C program started from the menu, with nothing of the game's, to find what restarts the unit. Each step is
 * named for HELLO_HOLD_MS, then done, whole, in one call; each step does again what the one before it did, and one
 * thing more. The name is drawn in a strip at the top of the LCD on every call (the firmware's box does not always
 * show the text we answer: in MAME it kept the menu's) and answered too. The name up when the unit restarts is the
 * step that did it. */
#define HELLO_HOLD_MS 3000
#ifdef PF_ALIGN_ENTRY
#define HELLO "PF HELLO A"  /* PFHELLOA\: the same, with the entry on a 16-byte boundary */
#else
#define HELLO "PF HELLO"
#endif
static const char* const hello_text[] = {
    HELLO " 1: C RUNS; NEXT DRIVE SWITCH",
    HELLO " 2: NEXT DRIVE SWITCH, OPEN, CLOSE",
    HELLO " 3: NEXT OPEN, WRITE, CLOSE (THE LOG)",
    HELLO " 4: ALL DONE; ENDING",
};
static u32 hello_step, hello_t0;

static int hello_show(void) {
  const char* t = hello_text[hello_step];
  lcd_fill(0, 0, FB_W, 21, RGB565(160, 0, 0));
  lcd_text(t, 8, 4, 0xFFFF, RGB565(160, 0, 0));
  return answer(t);
}

static int hello(void) {
  const u32 c = RD32(SVC + SV_MSCOUNT);
  const u32 now = (c ? RD32(c) : calls * 50) | 1;
  if (!hello_t0) hello_t0 = now;
  if (now - hello_t0 < HELLO_HOLD_MS) return hello_show();
  hello_t0 = 0;
  const u32 setext = RD32(SVC + SV_SETEXT), resetext = RD32(SVC + SV_RESETEXT), open = RD32(SVC + SV_OPEN),
            close = RD32(SVC + SV_CLOSE);
  switch (hello_step++) {
    case 0:
      fw_call(setext, 1, 0, 0, 0);
      fw_call(resetext, 0, 0, 0, 0);
      break;
    case 1: {
      fw_call(setext, 1, 0, 0, 0);
      const s32 h = (s32)(int16_t)fw_call(open, 0, 0, (u32)LOG_NAME, (u32) "wb");
      if (h >= 0) fw_call(close, 0, 0, 0, 0);
      fw_call(resetext, 0, 0, 0, 0);
      break;
    }
    case 2:
      diag_log(HELLO " 3: OPEN, WRITE, CLOSE");
      break;
    default:
      diag_log(HELLO " 4: ENDING");
      *(volatile u8*)MAILBOX = RQ_END;
      calls = hello_step = 0;
      log_len = 0;
      return 0;
  }
  return hello_show();
}
#endif

#ifdef PF_ALIGN_ENTRY
__attribute__((aligned(16)))  /* (every program that has run on the unit starts on a word boundary; ours did not) */
#endif
int cnv_entry(void) {
  ++calls;
#ifdef PF_HELLO
  return hello();
#endif
  if (ended) {  /* "Continue Next?" YES without a reload (the loader always reloads): start again */
    ended = 0;
    quit_req = 0;
    exit_t0 = 0;
    state = S_FIRST;
    step = part = 0;
  }
#ifdef PF_DIAG
  static const char* logged_status;
  if (state != S_FIRST && state < S_RUN) {
    if (diag_hold()) return answer(diag_next());
    diag_log(diag_next());  /* the step this call does */
  }
  if (state == S_RUN && status_text != logged_status) diag_log(logged_status = status_text);
#endif
  switch (state) {
    case S_FIRST:
#ifdef PF_DIAG
      diag_log("PF DIAG: ENTERED; NEXT LAUNCH BLOCK");
#endif
      lcdc_save();
      launch_folder();
      state = S_SEARCH;
#ifdef PF_DIAG
      diag_log("PF DIAG: NEXT LCD");
#endif
      progress("Looking for the firmware's routines...", 0, FW_STEPS);
#ifdef PF_DIAG
      diag_log("PF DIAG: LCD DRAWN");
      break;  /* the launch block alone in the first call */
#endif
      /* fall through */
    case S_SEARCH:
      if (fw_step((int)step)) {
        fail("PINBALL: UNKNOWN FIRMWARE, STEP ", (int)step);
        break;
      }
      progress("Looking for the firmware's routines...", ++step, FW_STEPS);
      if (step < FW_STEPS) break;
      notes_init();
#ifndef PF_DIAG
      fw_find_dial();
#endif
      state = S_DSP;
      break;
    case S_DSP: {
      const int r = dsp_start(part++);
      if (r == 2) break;
      state = S_RUN;  /* (r 1, no DSP: the game is silent; DSP.fail says why) */
      if (task_start()) fail("PINBALL: NO TASK SLOT", -1);
      break;
    }
    case S_RUN:
      wheel_scan();
      led_service();
      if (!file_serve() && !bank_serve()) rest();
      break;
    case S_FAILED:
      if (fw.panel ? button(BTN_EXIT) : ++fail_calls >= FAIL_CALLS) quit_req = 1;
      break;
  }
#ifdef PF_DIAG
  if (state == S_FAILED && status_text != fail_text) diag_log(status_text = fail_text);
#endif
  if (state != S_FAILED) watch_exit();
  if (quit_req && task_done) {
#ifdef PF_DIAG
    diag_log("PF DIAG: QUIT");
    log_len = 0;
#endif
    led_want = 0;
    led_service();  /* the wheel's LED as the firmware had it */
    crash_disarm();  /* the firmware's NMI stub back */
    fwdraw_unhook();  /* its screen procedure */
    lcdc_restore();  /* and its LCD start address */
    dsp_quit();  /* the firmware's effects back first */
    *(volatile u8*)MAILBOX = RQ_END;
    ended = 1;
    return 0;
  }
#ifdef PF_DIAG
  return answer(diag_next());
#else
  return answer(state == S_FAILED ? fail_text : status_text);
#endif
}
