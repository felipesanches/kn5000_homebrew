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
  if (pc >= GAME_BASE && pc < GAME_END) return "IN THE TABLE PROGRAM (PFn.BIN)";
  if (pc >= APP_BASE && pc < GAME_BASE) return "IN KN7APP.BIN";
  if (pc >= 0x48000000u && pc < 0x4D000000u) return "IN THE FIRMWARE";
  return "";
}

void crash_main(const u32* sp) {
  crash_disarm();
  fwdraw_unhook();
  lcdc_restore();  /* the firmware's frame on the LCD again: the screen is drawn there */
  lcd_fill(0, 0, FB_W, FB_H, BLUE);
  lcd_text("DOOM: THE CPU STOPPED THE PROGRAM (SYSTEM ERROR NMI)", 8, 8, 0xFFFF, BLUE);
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
