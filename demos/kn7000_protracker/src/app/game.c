/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * game.c -- the program task: PT.BIN (ProTracker, src/pt/) read to GAME_BASE, checked, started with a PfHost; when it
 * returns, the conversion ends. Holding EXIT ends everything as well (entry.c). The firmware draws nothing on the LCD
 * meanwhile (task.c, fwdraw.c). The Pinball port's game.c, with its table menu and its card-file sample banks left
 * out: ProTracker codes its own bank in RAM and sends it through dspSend (bank_send, entry.c). */
#include "app.h"
#include "dsp_mixer.h"

extern const unsigned char font8x13[95][13];

#define WHITE 0xFFFF
#define GREY RGB565(160, 160, 160)
#define RED RGB565(255, 64, 64)

static PfRegion regions[] = {{ARENA1_LO, ARENA1_HI}, {ARENA2_LO, ARENA2_HI}, {ARENA4_LO, ARENA4_HI},
                             {ARENA3_LO, ARENA3_HI}, {ARENA5_LO, ARENA5_HI}, {ARENA6_LO, ARENA6_HI},
                             {ARENA7_LO, ARENA7_HI}, {0, 0}};
static u32 host_ms(void) { return now_ms(); }
static void host_status(const char* s) { set_status(s); }
static void host_dsp_set(u32 v, const PfVoice* p, u32 how) {
  if (dsp.ready) dsp_set(v, p, how);
}
static void host_dsp_flush(void) {
  if (dsp.ready) dsp_flush();
}
static void host_dsp_bank(u32 n, u32* base) {
  (void)n;
  *base = 0;
}
/* every voice silent (the program left) */
static void dsp_silence(void) {
  if (!dsp.ready) return;
  for (u32 v = 0; v < 8; ++v) dsp_set(v, 0, 0);
  dsp_flush();
}

static PfHost host = {
    .magic = PFHOST_MAGIC,
    .abi = PFHOST_ABI,
    .prefix = prefix,
    .ms = host_ms,
    .yield = task_yield,
    .load = file_load,
    .save = file_save,
    .pollNotes = notes_poll,
    .notes = notes,
    .regions = regions,
    .text = lcd_text,
    .fill = lcd_fill,
    .dspBank = host_dsp_bank,
    .dspSet = host_dsp_set,
    .dspFlush = host_dsp_flush,
    .status = host_status,
    .keybedSound = keys_sound,
    .keybedSoundOn = keys_sound_on,
    .sustain = &notes_sustain,
    .lcdRestore = lcdc_restore,
    .wheel = &wheel_steps,
    .lastTable = -1,
    .font = &font8x13[0][0],
    .wheelLed = wheel_led,
    .dspSend = bank_send,
    .dspBase = DSP_BANK,
    .dspMaxWords = DSP_BANK_MAX,
};

static u32 crc32(const u8* p, u32 n) {
  u32 c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

/* The CPU's caches (an MN103002A: instruction cache, and a write-back data cache, 4 KB each in two ways of 16-byte
 * lines, both on: CHCTR 0x008F on the unit) after PT.BIN was read with data stores: its bytes written back to RAM and
 * no older line of GAME_BASE left in the instruction cache, as program 893 does it (its routines at 0x48407B..: the
 * instruction cache off (CHCTR bit 0), waited for (bit 2), invalidated (bit 4), on again; and func_48407C39: the data
 * cache off (bit 1), waited for (bit 3), each line of both ways written back by reading the purge window 0x28400000 and
 * 0x28401000 at 16-byte steps (unless the cache writes through, bit 6, or a way is locked, bits 12 and 13), invalidated
 * (bit 5), on again). With interrupts off: while the data cache is off, a handler would read RAM that its dirty lines
 * have not reached. (The Pinball port's caches_sync, unchanged: a sled of nops in its place stopped the unit.) */
#define CHCTR16 (*(volatile u16*)0x20000070u)
#define CHCTR32 (*(volatile u32*)0x20000070u)

void caches_sync(void) {
  u32 psw;
  __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
  u32 c = CHCTR16;
  if (c & 0x02) {  /* the data cache: off, written back, invalidated, on */
    u32 d = c & 0xFFFD;
    CHCTR32 = d;
    for (int n = 0; n < 100000 && (CHCTR16 & 0x08); ++n) {}
    if (!(c & 0x40)) {
      if (!(c & 0x1000))
        for (u32 a = 0x28400000u; a < 0x28400000u + 0x800; a += 0x10) (void)*(volatile u32*)a;
      if (!(c & 0x2000))
        for (u32 a = 0x28401000u; a < 0x28401000u + 0x800; a += 0x10) (void)*(volatile u32*)a;
    }
    d |= 0x20;
    CHCTR32 = d;
    d |= 0x02;
    CHCTR16 = (u16)d;
  }
  c = CHCTR16;
  if (c & 0x01) {  /* the instruction cache: off, invalidated, on */
    u32 d = c & 0xFFFE;
    CHCTR32 = d;
    for (int n = 0; n < 100000 && (CHCTR16 & 0x04); ++n) {}
    d |= 0x10;
    CHCTR32 = d;
    d |= 0x01;
    CHCTR16 = (u16)d;
  }
  __asm__ volatile("mov %0,psw\n\tnop\n\tnop" ::"d"(psw) : "memory");
}

/* Reads PT.BIN and checks it; 0, or why not. */
static const char* load_program(void) {
  PfPiece piece = {(void*)GAME_BASE, GAME_END - GAME_BASE};
  const s32 got = file_load("PT.BIN", &piece, 1);
  if (got < (s32)sizeof(PfGameHeader)) return "ITS PROGRAM (PT.BIN) IS NOT ON THE CARD";
  const PfGameHeader* h = (const PfGameHeader*)GAME_BASE;
  if (h->magic != PFGAME_MAGIC || h->abi != PFHOST_ABI || h->table != PT_PROGRAM) return "PT.BIN IS NOT THIS BUILD'S";
  if (h->size != (u32)got || h->end > GAME_END || h->bssLo < GAME_BASE + h->size || h->bssHi > h->end)
    return "PT.BIN IS DAMAGED (SIZE)";
  if (crc32((const u8*)GAME_BASE + 0x40, h->size - 0x40) != h->crc) return "PT.BIN IS DAMAGED (CRC)";
  for (u32 a = h->bssLo; a < h->bssHi; a += 4) *(volatile u32*)a = 0;
  caches_sync();
  for (u32 a = h->initLo; a < h->initHi; a += 4) (*(void (**)(void))a)();
  return 0;
}

void game_task(void) {
  host.build = fw.build;
  host.panel = (const volatile u8*)fw.panel;
  host.msCounter = fw.ms;
  host.dspVoices = dsp.state == DS_IDLE || dsp.state == DS_READY ? 8 : 0;
  if (fw.build == BUILD_SP7001) regions[3] = (PfRegion){0, 0};  /* its SD plug-in lives in ARENA3 */
  else if (fw.build != 893 && fw.build != 941) regions[4] = (PfRegion){0, 0};  /* ARENA5-7: 893 and 941 only */
  crash_arm();
  fwdraw_settle();  /* (the firmware's last copy to the LCD, before ours) */
  set_status("HOLD EXIT TO QUIT");
  lcd_fill(0, 0, FB_W, FB_H, 0);
  lcd_text("PROTRACKER", 280, 100, WHITE, 0);
  lcd_text("Loading...", 280, 120, GREY, 0);
  const char* why = load_program();
  if (why) {
    int n = 0;
    while (why[n]) ++n;
    lcd_text(why, (FB_W - 8 * n) / 2, 140, RED, 0);
    lcd_text("Hold EXIT to leave", 248, 222, GREY, 0);
    set_status(why);
    for (;;) task_yield(50);
  }
  const PfGameHeader* h = (const PfGameHeader*)GAME_BASE;
  h->entry(&host);
  wheel_led(0);
  lcdc_restore();
  dsp_silence();
  keys_sound(1);
  quit_req = 1;  /* the program left: the conversion ends (entry.c) */
  for (;;) task_yield(20);
}
