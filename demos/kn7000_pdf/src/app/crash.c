/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * crash.c -- the crash screen (README.md, "If it does not start"). The KN7000's CPU answers some faults with a
 * system-error NMI (NMICR bit 2; probe PFT10), and the firmware's NMI handler, its power-fail routine, restarts the
 * instrument without a word. While the game task runs, crash_arm points the NMI stub at 0x50000006 at crash_nmi
 * (glue.S): a system error paints the PC the CPU pushed and the stack on the LCD and holds them; any other NMI (the
 * instrument switched off) goes on to the firmware's handler, which stamps IC23 as always. crash_disarm puts the
 * firmware's stub back before the program ends. */
#include "app.h"

#define STUB ((volatile u16*)0x90000006u) /* the stub at 0x50000006, through the +0x40000000 view */
#define GPIO8004 ((volatile u8*)0x36008004u)
#define BLUE RGB565(0, 0, 160)

#include "build_id.h"
static const char wd_build[] = BUILD_ID;

u32 crash_fw;           /* the firmware's NMI handler, where crash_nmi sends every other NMI */
static u16 saved[3];
static int armed;
void crash_nmi(void);   /* glue.S */
void crash_main(const u32* sp);

static void stub_put(const u16* h) {
  STUB[0] = h[0];
  STUB[1] = h[1];
  STUB[2] = h[2];
  caches_sync();        /* the stub shares its cache line with the interrupt stub at 0x50000000 */
}

void crash_arm(void) {
  if (armed) return;
  saved[0] = STUB[0];
  saved[1] = STUB[1];
  saved[2] = STUB[2];
  crash_fw = 0x50000007u + ((u32)saved[1] | (u32)saved[2] << 16); /* nop; jmp d32, from the jmp */
  const u32 rel = (u32)crash_nmi - 0x50000007u;
  const u16 ours[3] = {0xDCCB, (u16)rel, (u16)(rel >> 16)};
  stub_put(ours);
  armed = 1;
}

void crash_disarm(void) {
  if (!armed) return;
  stub_put(saved);
  armed = 0;
}

static char* hex(char* p, u32 v, int digits) {
  for (int i = digits - 1; i >= 0; --i) *p++ = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
  return p;
}

static char* str(char* p, const char* s) {
  while (*s) *p++ = *s++;
  return p;
}

static const char* where(u32 pc) {
  if ((pc >= GAME_BASE && pc < GAME_END) || (pc >= ARENA2_LO && pc < ARENA2_HI) || (pc >= ARENA4_LO && pc < ARENA4_HI))
    return "IN PDF.BIN";
  if (pc >= APP_BASE && pc < GAME_BASE) return "IN KN7APP.BIN";
  if (pc >= 0x48000000u && pc < 0x4D000000u) return "IN THE FIRMWARE";
  return "";
}

void wd_disarm(void);

void crash_main(const u32* sp) {
  crash_disarm();
  wd_disarm();
  fwdraw_unhook();
  lcdc_restore();  /* the firmware's frame on the LCD again: the screen is drawn there */
  lcd_fill(0, 0, FB_W, FB_H, BLUE);
  lcd_text("PDF VIEWER: THE CPU STOPPED THE PROGRAM (SYSTEM ERROR NMI)", 8, 8, 0xFFFF, BLUE);
  char line[96];
  char* p = str(line, "PC ");
  p = hex(p, sp[1], 8);
  p = str(p, " ");
  p = str(p, where(sp[1]));
  *p = 0;
  lcd_text(line, 8, 32, 0xFFFF, BLUE);
  p = str(line, "PSW ");
  p = hex(p, sp[0], 8);
  p = str(p, "  NMICR ");
  p = hex(p, *(volatile u16*)0x34000100u, 4);
  *p = 0;
  lcd_text(line, 8, 48, 0xFFFF, BLUE);
  for (int r = 0; r < 8; ++r) {
    p = str(line, "SP+");
    p = hex(p, 8 + 16 * r, 2);
    for (int i = 0; i < 4; ++i) {
      p = str(p, " ");
      p = hex(p, sp[2 + 4 * r + i], 8);
    }
    *p = 0;
    lcd_text(line, 8, 72 + 16 * r, 0xFFFF, BLUE);
  }
  lcd_text("PHOTOGRAPH THIS SCREEN, THEN SWITCH THE INSTRUMENT OFF", 8, 216, 0xFFFF, BLUE);
  for (;;) { /* strobe GPIO 0x36008004 bit 5 as the firmware's NMI handler does (a watchdog kick, by its pattern) */
    for (volatile int i = 0; i < 20000; ++i) {}
    *GPIO8004 |= 0x20;
    *GPIO8004 &= (u8)~0x20;
  }
}

/* ---- the watchdog: where the program task is when it has not yielded for WD_MS ----------------------------------
 * The firmware's tasks and ours share the CPU only where a task yields, so a program task that loops without yielding
 * holds the instrument, and nothing it could log reaches the card. The interrupts still come: wd_arm puts a jmp to
 * wd_tick (glue.S) at the start of the firmware's level-6 system-tick entry (1 kHz; in the library SRAM, its address by
 * the PROGRAM build as MAME's kn7000_bios_info has it, checked by its first 7 bytes), and each tick asks wd_check
 * whether the task yielded in the last WD_MS (task.c task_yield stamps wd_last). When it has not, the PC the tick came
 * from is taken every WD_EVERY_MS, WD_SAMPLES times, with the stack's words over the first in our code. Then, at the next
 * tick that comes from our code, the registers the tick's entry saved are kept, and the
 * tick returns to wd_escape instead: on the program task, it writes all of it into the log, asks the program for its
 * own (PfHost::stuck), saves the log on the card, shows the screen, and yields from then on, so the instrument goes on
 * (the firmware still runs; the program never goes back to what it was doing). */
#define WD_MS 4000
#define WD_EVERY_MS 100
#define WD_SAMPLES 16

void wd_tick(void);  /* glue.S */
extern u8 wd_back[]; /* glue.S: jmp d32 back into the firmware's entry */
volatile u32 wd_last;
static u32 wd_on, wd_n, wd_t, wd_shown, wd_entry;
#define WD_STACK 48
#define WD_DEEP 1024 /* words of the task's stack the escape lists from (the first WD_COPY copied at the tick: what the */
#define WD_COPY 8    /* escape's own frame may write over; it pushes only below the task's SP, so the rest stays) */
static u32 wd_pc[WD_SAMPLES], wd_stack[WD_STACK], wd_stack_at;
/* at the escape: D0-D3, A0-A3, MDR, the PSW, the PC and the SP the task had, and the stack's words from that SP */
static u32 wd_reg[12], wd_copy[WD_COPY];
volatile u32 wd_escaped; /* 1 at the escape, 2 once the log is on the card (tests/mame/hang.lua) */
static u8 wd_saved[7];
static const u8 wd_head[7] = {0xCF, 0xF8, 0xF8, 0xFE, 0xF4, 0xF2, 0xE7};

static u32 tick_entry(void) {
  switch (fw.build) {
    case 893: return 0x4C03DE13u;
    case 935: case 941: return 0x4C03DE26u;
    case 942: return 0x4C03E34Cu;
    default: return 0;
  }
}

static void psw_off(u32* psw) { __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(*psw)::"memory"); }
static void psw_back(u32 psw) { __asm__ volatile("mov %0,psw\n\tnop\n\tnop" ::"d"(psw) : "memory"); }

void wd_arm(void) {
  if (wd_on) return;
  const u32 e = tick_entry();
  volatile u8* const p = (volatile u8*)e;
  for (int i = 0; e && i < 7; ++i)
    if (p[i] != wd_head[i]) {
      diag("watchdog: the tick's entry %08X reads %02X at +%d, not armed", e, p[i], i);
      return;
    }
  if (!e) {
    diag("watchdog: no tick entry known for PROGRAM %u, not armed", fw.build);
    return;
  }
  const u32 back = (e + 7) - (u32)wd_back;
  wd_back[1] = (u8)back, wd_back[2] = (u8)(back >> 8), wd_back[3] = (u8)(back >> 16), wd_back[4] = (u8)(back >> 24);
  wd_last = now_ms();
  wd_n = wd_shown = wd_stack_at = 0;
  caches_sync();  /* (wd_back's bytes written as data) */
  const u32 rel = (u32)wd_tick - e;
  u32 psw;
  psw_off(&psw);
  for (int i = 0; i < 7; ++i) wd_saved[i] = p[i];
  p[1] = (u8)rel, p[2] = (u8)(rel >> 8), p[3] = (u8)(rel >> 16), p[4] = (u8)(rel >> 24), p[5] = 0xCB, p[6] = 0xCB;
  p[0] = 0xDC;
  caches_sync();
  psw_back(psw);
  wd_entry = e;
  wd_on = 1;
  diag("watchdog: armed at the tick's entry %08X", e);
}

void wd_disarm(void) {
  if (!wd_on) return;
  volatile u8* const p = (volatile u8*)wd_entry;
  u32 psw;
  psw_off(&psw);
  p[0] = wd_saved[0];
  for (int i = 1; i < 7; ++i) p[i] = wd_saved[i];
  caches_sync();
  psw_back(psw);
  wd_on = 0;
}

static void wd_show(void) {
  lcdc_restore();          /* the firmware's mode, but not its frame: */
  lcd_away(LCD_FRAME);     /* the firmware still runs and repaints that, so the screen goes where the page was */
  lcd_fill(0, 0, FB_W, FB_H, BLUE);
  lcd_text("PDF VIEWER: STUCK (NO YIELD FOR 4 S). THE PC, EVERY 0.1 S:", 8, 4, 0xFFFF, BLUE);
  char line[96];
  for (int r = 0; r < WD_SAMPLES / 8; ++r) {
    char* p = line;
    for (int i = 0; i < 8; ++i) {
      p = hex(p, wd_pc[8 * r + i], 8);
      p = str(p, " ");
    }
    *p = 0;
    lcd_text(line, 8, 20 + 14 * r, 0xFFFF, BLUE);
  }
  char* p = str(line, "THE STACK AT SAMPLE ");
  p = hex(p, wd_stack_at, 2);
  p = str(p, wd_stack_at ? ":" : " (NONE IN OUR CODE)");
  *p = 0;
  lcd_text(line, 8, 56, 0xFFFF, BLUE);
  for (int r = 0; r < WD_STACK / 6; ++r) {
    p = line;
    for (int i = 0; i < 6; ++i) {
      p = hex(p, wd_stack[6 * r + i], 8);
      p = str(p, "  ");
    }
    *p = 0;
    lcd_text(line, 8, 72 + 14 * r, 0xFFFF, BLUE);
  }
  p = str(line, "BUILD ");
  p = str(p, wd_build);
  *p = 0;
  lcd_text(line, 8, 192, 0xFFFF, BLUE);
  lcd_text("PHOTOGRAPH THIS SCREEN, THEN SWITCH THE INSTRUMENT OFF", 8, 216, 0xFFFF, BLUE);
}

static int ours(u32 pc) { return pc >= 0x50000000u && pc < 0x58000000u; }

static void wd_escape(void) __attribute__((noreturn));
static void wd_escape(void) {
  static const char* const name[12] = {"D0", "D1", "D2", "D3", "A0", "A1", "A2", "A3", "MDR", "PSW", "PC", "SP"};
  diag("watchdog: no yield since %u ms (now %u); the PC every %u ms:", wd_last, now_ms(), WD_EVERY_MS);
  for (int r = 0; r < WD_SAMPLES / 8; ++r) {
    const u32* v = wd_pc + 8 * r;
    diag("  %08X %08X %08X %08X %08X %08X %08X %08X", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
  }
  for (int r = 0; r < 2; ++r) {
    const int i = 6 * r;
    diag("  %s %08X  %s %08X  %s %08X  %s %08X  %s %08X  %s %08X", name[i], wd_reg[i], name[i + 1], wd_reg[i + 1],
         name[i + 2], wd_reg[i + 2], name[i + 3], wd_reg[i + 3], name[i + 4], wd_reg[i + 4], name[i + 5], wd_reg[i + 5]);
  }
  /* the stack's words that may be return addresses (our RAM), with their place */
  u32 n = 0;
  char line[96];
  char* p = line;
  const u32* const sp = (const u32*)wd_reg[11];
  for (u32 i = 0; i < WD_DEEP && n < 96; ++i) {
    const u32 w = i < WD_COPY ? wd_copy[i] : sp[i];
    if (!ours(w)) continue;
    p = str(p, "+");
    p = hex(p, i * 4, 3);
    p = str(p, ":");
    p = hex(p, w, 8);
    p = str(p, " ");
    if (++n % 6 == 0) *p = 0, diag("  %s", line), p = line;
  }
  if (p != line) *p = 0, diag("  %s", line);
  game_stuck();  /* the program's own account (PfHost::stuck) */
  diag("watchdog: the log saved; the program stays stopped here");
  diag_save();
  wd_escaped = 2;
  wd_show();
  for (;;) task_yield(50);
}

/* every tick, from wd_tick: `frame` the PSW and the PC the CPU pushed, the 12 words the entry's movm pushed below them
 * (D2 D3 A2 A3 D0 D1 A0 A1 MDR LIR LAR and one more, from the top) */
void wd_check(u32* frame) {
  if (!wd_on || wd_shown || task_done) return;
  const u32 t = now_ms();
  if (t - wd_last < WD_MS) {
    wd_n = 0;
    return;
  }
  if (wd_n == WD_SAMPLES) {  /* the samples taken: the escape, from a tick that comes from our code */
    if (!ours(frame[1])) {
      if (t - wd_t < 3000) return;
      wd_shown = 1;  /* (none in 3 s: the screen alone) */
      wd_show();
      return;
    }
    const u32 r[12] = {frame[-5], frame[-6], frame[-1], frame[-2], frame[-7], frame[-8], frame[-3], frame[-4],
                       frame[-9], frame[0], frame[1], (u32)(frame + 2)};
    for (int i = 0; i < 12; ++i) wd_reg[i] = r[i];
    for (int i = 0; i < WD_COPY; ++i) wd_copy[i] = frame[2 + i];
    wd_shown = wd_escaped = 1;
    frame[1] = (u32)wd_escape;
    return;
  }
  if (wd_n && t - wd_t < WD_EVERY_MS) return;
  wd_t = t;
  /* the stack at the first sample in our code (RAM, not the firmware's flash or library) */
  if (!wd_stack_at && ours(frame[1])) {
    for (int i = 0; i < WD_STACK; ++i) wd_stack[i] = frame[2 + i];
    wd_stack_at = wd_n + 1;
  }
  wd_pc[wd_n++] = frame[1];
}
