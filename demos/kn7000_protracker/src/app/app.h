/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * app.h -- KN7APP.BIN's own declarations: what its files share. */
#ifndef PF_APP_H
#define PF_APP_H
#include "pfhost.h"

#define RD32(a) (*(volatile u32*)(a))
#define RD8(a) (*(volatile u8*)(a))

/* glue.S */
u32 fw_call(u32 fn, u32 d0, u32 d1, u32 a0, u32 a1);
u32 fw_call3(u32 fn, u32 d0, u32 d1, u32 third);  /* `third` at (0x10, sp) of the calling frame */
const u8* find_byte(const u8* from, const u8* to, u32 byte);

/* fwfind.c: what the firmware search found */
typedef struct Fw {
  u32 open, read, close, write, setext, resetext;  /* the loader's SD file routines */
  u32 cre, sta, dly, exd;                          /* the kernel's task services, the yield */
  u32 ms;                                          /* the kernel's millisecond counter */
  u32 panel;                                       /* the panel bitmaps (CPR; CPL at +0x30) */
  u32 midiIn1, midiOut1;                           /* the MIDI rings' headers */
  u32 build;                                       /* the PROGRAM build, or 0 */
  u32 dial[2];                                     /* the TEMPO/PROGRAM wheel LED's shadow bytes (fw_find_dial), or 0 */
} Fw;
extern Fw fw;
#define FW_STEPS 6
int fw_step(int step);  /* 0: found what that step looks for */
int fw_count(const u16* sig, u32* first);  /* hits of a signature in the program flash; *first the first */
void fw_find_dial(void);  /* fw.dial, or 0s: the wheel's LED is then left alone */

static inline u32 now_ms(void) { return RD32(fw.ms); }
static inline int button(u32 b) { return (RD8(fw.panel + (b >> 8)) & (b & 0xFF)) != 0; }

/* lcd.c */
void lcd_fill(int x, int y, int w, int h, u16 colour);
void lcd_text(const char* s, int x, int y, u16 fg, u16 bg);
void lcdc_save(void);     /* the LCD controller's start address and line offset, as the firmware has them */
void lcdc_restore(void);  /* ... back (a table left, the end, the crash screen) */
#define RGB565(r, g, b) ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* notes.c */
extern u16 notes[128];
extern u16 notes_sustain;  /* the MIDI channels whose sustain pedal (CC 64) is down */
extern u8 notes_mod_wheel;  /* CC 1 */
u32 notes_ons(u8* note, u8* vel, u32 max);  /* PfHost::noteOns */
void notes_init(void);
void notes_poll(void);
void keys_sound(int on);  /* the keybed's own sound on or off (sent by keys_sync, from the game task's yields) */
int keys_sound_on(void);
void keys_sync(void);

/* files.c: the entry's side (one request served per call) and the game task's */
s32 file_load(const char* name, const PfPiece* pieces, u32 count);  /* game task: waits */
int file_serve(void);                                               /* entry: 1 when it did a request */
int file_pending(void);                                             /* entry: a request waits */
s32 file_save(const char* name, const void* data, u32 bytes);       /* game task: waits */

/* task.c */
extern volatile u32 quit_req, task_done;
int task_start(void);
void task_yield(u32 ms);
void game_task(void);

/* game.c */
void caches_sync(void);  /* the data cache written back, the instruction cache emptied (as program 893 does it) */

/* crash.c: the crash screen while the game task runs */
void crash_arm(void);
void crash_disarm(void);
void crash_selftest_load(void);  /* glue.S, PF_CRASH_SELFTEST only */

/* fwdraw.c: the firmware's screen without its wallpaper repaints while the game task runs */
void fwdraw_hook(void);
void fwdraw_unhook(void);
void fwdraw_drawing(int on);
void fwdraw_quiet(void);
void fwdraw_settle(void);
extern u32 fwdraw_reasserted;
extern u32 screen_proc_skipped;

/* entry.c */
extern char prefix[24];
extern u32 calls;  /* converter calls so far */
int bank_send(const u32* rec, u32 bytes, u32 words, u32 sum, u32 id);  /* game task: PfHost::dspSend */
void wheel_led(int blink);  /* PfHost::wheelLed (entry.c) */
extern u8 panel_real[0x40];
void wheel_scan(void);       /* PfHost::panelScan: the ring's frames, the switches and the wheel taken from the firmware */  /* the switches as the panel reports them, kept from the firmware (entry.c panel_scan) */
extern volatile s32 wheel_steps;  /* the TEMPO/PROGRAM wheel's detents so far, signed (entry.c wheel_scan) */
void set_status(const char* s);
/* The diagnostic build (`make DIAG=1`, README.md "If it does not start"): the text the converter calls answer, which
 * the firmware shows in its own box, names what comes next. Nothing in the normal build. */
#ifdef PF_DIAG
#define DIAG_STATUS(s) set_status(s)
#else
#define DIAG_STATUS(s) ((void)0)
#endif
s32 file_read_now(const char* name, void* dst, u32 max);  /* files.c: on the main task (the entry), whole */

/* dsp.c: the effects DSP (Another World's mixer) */
#define DFB_WAIT 1
#define DFB_REGS 2
#define DFB_IDX 4
#define DFB_DATA 8
#define DFB_WRITE 0x10
#define DFB_ALL 0x1F
enum { DS_NONE, DS_BANK, DS_READY, DS_OFF, DS_IDLE };  /* DS_IDLE: the mixer hooked, no bank in hand */
enum { DF_NOTFOUND = 1, DF_DEAD, DF_NOTDOWN, DF_STATE, DF_SLOT, DF_VERIFY, DF_HOOK, DF_FILE, DF_STALL, DF_RUNDEAD,
       DF_WATCH, DF_RUNTIME, DF_SUM, DF_BANKTAB };
typedef struct Dsp {
  u32 found, dwait, regwr, regrd, idxwr, datard, datawr, dead, down;  /* the firmware's routines (dsp_find) */
  u32 state, fail, ready, hooked, u9, sent;
  u32 syscon, dmac0, iiep0, imep0, cep0, dmastat, slot0, slot9, hb0;
  u32 want_words, want_sum, want_id;                           /* what dsp_bank_step brings into SDRAM, checked */
  u32 rec_lo, rec_bytes, to_send;                              /* its coded blocks in RAM (and the words they fill) */
  u32 ptr, len, done, stalls, buf, held, phase, t0, ms;        /* the bank's upload */
  u32 pend, busy, flushes;                                     /* the voices */
  u32 cmds[8 * 5];
} Dsp;
extern Dsp dsp;
void dsp_find(void);
int dsp_prepare(void);
int dsp_bank_begin(const u32* rec, u32 bytes, u32 words, u32 sum, u32 id);
int dsp_bank_step(void);
void dsp_quit(void);
void dsp_set(u32 voice, const PfVoice* v, u32 how);
void dsp_flush(void);

#endif
