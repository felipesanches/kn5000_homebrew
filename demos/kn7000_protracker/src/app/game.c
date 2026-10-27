/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * game.c -- the game task: the menu of the four tables, with the table's angle and its scrolling (PINBALL.CFG);
 * then the chosen table's sounds into the DSP (entry.c bank_load) and its program (PFn.BIN: Encore's engine with
 * that table's rules, src/game/), read to GAME_BASE, checked, started with a PfHost; when the player leaves the
 * table, the menu again. Holding EXIT ends everything (entry.c). The firmware draws nothing on the LCD meanwhile
 * (task.c, fwdraw.c): the menu and the table are all it shows. */
#include "app.h"

extern const unsigned char font8x13[95][13];

static const char* const kTables[4] = {"PARTY LAND", "SPEED DEVILS", "BILLION DOLLAR GAMESHOW", "STONES 'N BONES"};
static const u32 kSelect[4] = {BTN_LCDL1, BTN_LCDL2, BTN_LCDL3, BTN_LCDL4};
static const u32 kSelectMsa[4] = {BTN_MSA1, BTN_MSA2, BTN_MSA3, BTN_MSA4};  /* (the same, VARIATION & MSA 1-4) */

#define WHITE 0xFFFF
#define GREY RGB565(160, 160, 160)
#define RED RGB565(255, 64, 64)

static PfRegion regions[] = {{ARENA1_LO, ARENA1_HI}, {ARENA2_LO, ARENA2_HI}, {ARENA4_LO, ARENA4_HI},
                             {ARENA3_LO, ARENA3_HI}, {ARENA5_LO, ARENA5_HI}, {ARENA6_LO, ARENA6_HI},
                             {ARENA7_LO, ARENA7_HI}, {0, 0}};
static u32 host_ms(void) { return now_ms(); }
static void host_status(const char* s) { set_status(s); }
static int table;  /* the one being played */
int crash_table;   /* the one being played, 1..4, for the crash screen; 0 in the menu */
static int sound_module;  /* whose samples the program plays: a table's (0-3), INTRO.MOD's (4), MOD2.MOD's (5) */
static int sound_ready(void) {
  return dsp.ready && (sound_module >= 4 ? dsp.loaded >= 0 : dsp.loaded == sound_module);
}
static void host_dsp_bank(u32 n, u32* base) { *base = dsp_sample(sound_module, n); }
static void host_dsp_set(u32 v, const PfVoice* p, u32 how) {
  if (dsp.ready) dsp_set(v, p, how);
}
static void host_dsp_flush(void) {
  if (dsp.ready) dsp_flush();
}
/* every voice silent (a table left) */
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
    .font = &font8x13[0][0],
    .wheelLed = wheel_led,
};

static u32 crc32(const u8* p, u32 n) {
  u32 c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

/* PINBALL.CFG, the DOS game's options file, as the table programs read it (src/game/pfmain.cpp readOptions): the
 * balls (1: five), the angle (1 low, 0 high, 2 higher), the scrolling (2 soft, 1 medium, 0 hard), the music off,
 * the resolution, mono. The menu sets the angle (LCDR1) and the scrolling (LCDR2) and writes the file when the
 * player has changed either; without a file, three balls, the angle low and the scrolling soft. */
static u8 cfg[6] = {0, 1, 2, 0, 0, 0};
static int cfg_changed;

static void options_read(void) {
  u8 b[7];
  PfPiece p = {b, sizeof b};
  if (file_load("PINBALL.CFG", &p, 1) == 6)
    for (int i = 0; i < 6; ++i) cfg[i] = b[i];
}

static void options_draw(void) {
  static const char* const angle[3] = {"ANGLE: HIGH", "ANGLE: LOW", "ANGLE: HIGHER"};
  static const char* const scroll[3] = {"SCROLLING: HARD", "SCROLLING: MEDIUM", "SCROLLING: SOFT"};
  const char* const line[2] = {angle[cfg[1] < 3 ? cfg[1] : 0], scroll[cfg[2] < 3 ? cfg[2] : 1]};
  for (int k = 0; k < 2; ++k) {
    int n = 0;
    while (line[k][n]) ++n;
    lcd_fill(400, 64 + k * 40, 240, 13, 0);
    lcd_text(line[k], 620 - 8 * n, 64 + k * 40, WHITE, 0);
    lcd_text(">", 628, 64 + k * 40, WHITE, 0);
  }
}

static void menu(int message_table, const char* message) {
  lcd_fill(0, 0, FB_W, FB_H, 0);
  lcd_text("PINBALL FANTASIES", 248, 14, WHITE, 0);
  lcd_text("Choose a table with the buttons left of the display", 116, 34, GREY, 0);
  for (int t = 0; t < 4; ++t) {
    lcd_text("<", 4, 64 + t * 40, WHITE, 0);
    lcd_text(kTables[t], 20, 64 + t * 40, WHITE, 0);
    if (t == message_table) lcd_text(message, 20, 80 + t * 40, RED, 0);
  }
  options_draw();
  lcd_text("Hold EXIT to leave", 248, 222, GREY, 0);
}

/* The table chosen, or -1 when the menu must be drawn again (the firmware turned its drawing on meanwhile). */
static int choose(void) {
  const u32 quiet = fwdraw_reasserted;
  for (;;) {
    if (fwdraw_reasserted != quiet) return -1;
    for (int t = 0; t < 4; ++t)
      if (button(kSelect[t]) || button(kSelectMsa[t])) {
        while (button(kSelect[t]) || button(kSelectMsa[t])) task_yield(20);
        return t;
      }
    for (int k = 0; k < 2; ++k)
      if (button(k ? BTN_LCDR2 : BTN_LCDR1)) {
        u8* v = &cfg[1 + k];
        *v = k ? (*v == 2 ? 1 : *v == 1 ? 0 : 2)    /* soft, medium, hard */
               : (*v == 1 ? 0 : *v == 0 ? 2 : 1);  /* low, high, higher */
        cfg_changed = 1;
        options_draw();
        while (button(k ? BTN_LCDR2 : BTN_LCDR1)) task_yield(20);
      }
    task_yield(20);
  }
}

/* The CPU's caches (an MN103002A: instruction cache, and a write-back data cache, 4 KB each in two ways of 16-byte
 * lines, both on: CHCTR 0x008F on the unit) after PFn.BIN was read with data stores: its bytes written back to RAM and
 * no older line of GAME_BASE left in the instruction cache, as program 893 does it (its routines at 0x48407B..: the
 * instruction cache off (CHCTR bit 0), waited for (bit 2), invalidated (bit 4), on again; and func_48407C39: the data
 * cache off (bit 1), waited for (bit 3), each line of both ways written back by reading the purge window 0x28400000 and
 * 0x28401000 at 16-byte steps (unless the cache writes through, bit 6, or a way is locked, bits 12 and 13), invalidated
 * (bit 5), on again). With interrupts off: while the data cache is off, a handler would read RAM that its dirty lines
 * have not reached.
 * Until 2026-10-27 this was a sled of nops written to free RAM, the data cache emptied by reading 64 KB of program flash,
 * the sled run twice (the program menu's way). On the unit it stopped the CPU in the sled (a system error NMI at
 * 0x5031C75C) as the menu came back after a table: a line of the sled was still in the data cache and the
 * instruction fetch read the table's old heap from RAM. MAME models no cache contents, so it could not show this. */
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

/* Reads PFn.BIN (n = t + 1; the front program, PF0.BIN, for t = 4) and checks it; 0, or why not. */
static const char* load_table(int t) {
  static char name[] = "PF1.BIN";
  name[2] = (char)(t == 4 ? '0' : '1' + t);
  lcd_fill(0, 196, FB_W, 24, 0);
  lcd_text("Loading...", 280, 200, WHITE, 0);
  DIAG_STATUS("PF DIAG: NEXT PFn.BIN, CACHES, TABLE START");
  PfPiece piece = {(void*)GAME_BASE, GAME_END - GAME_BASE};
  const s32 got = file_load(name, &piece, 1);
  if (got < (s32)sizeof(PfGameHeader)) return "its program (PFn.BIN) is not on the card";
  const PfGameHeader* h = (const PfGameHeader*)GAME_BASE;
  if (h->magic != PFGAME_MAGIC || h->abi != PFHOST_ABI || h->table != (u32)t) return "PFn.BIN is not this table's";
  if (h->size != (u32)got || h->end > GAME_END || h->bssLo < GAME_BASE + h->size || h->bssHi > h->end)
    return "PFn.BIN is damaged (size)";
  if (crc32((const u8*)GAME_BASE + 0x40, h->size - 0x40) != h->crc) return "PFn.BIN is damaged (CRC)";
  for (u32 a = h->bssLo; a < h->bssHi; a += 4) *(volatile u32*)a = 0;
  caches_sync();
  for (u32 a = h->initLo; a < h->initHi; a += 4) (*(void (**)(void))a)();
  return 0;
}

/* The slides and the menu (PF0.BIN, Encore's Front: src/front/front.cpp), with their music: the table chosen (0-3), -1
 * when the player left the game there, -2 when the front program cannot run (the menu above takes over). `last`: the
 * table played last, or -1 the first time (the slides, and INTRO.MOD; after a table, MOD2.MOD). */
static int front(int last) {
  set_status("HOLD EXIT TO QUIT");
  lcd_fill(0, 0, FB_W, FB_H, 0);
  lcd_text("PINBALL FANTASIES", 248, 100, WHITE, 0);
  bank_load(BANK_MENU);  /* (without it the menu is silent) */
  if (load_table(4)) return -2;
  const PfGameHeader* h = (const PfGameHeader*)GAME_BASE;
  sound_module = last < 0 ? 4 : 5;
  host.lastTable = last;
  host.dspVoices = sound_ready() ? 8 : 0;
  keys_sound(0);  /* (the keybed only moves the slides on here) */
  const int r = h->entry(&host);
  wheel_led(0);  /* (whatever the program left it doing) */
  lcdc_restore();
  dsp_silence();
  keys_sound(1);
  return r < -1 || r > 3 ? -2 : r;
}

void game_task(void) {
  host.build = fw.build;
  host.panel = (const volatile u8*)fw.panel;
  host.msCounter = fw.ms;
  if (fw.build == BUILD_SP7001) regions[3] = (PfRegion){0, 0};  /* its SD plug-in lives in ARENA3 */
  else if (fw.build != 893 && fw.build != 941) regions[4] = (PfRegion){0, 0};  /* ARENA5-7: 893 and 941 only */
  int failed = -1;
  const char* why = "";
  crash_arm();
  fwdraw_settle();  /* (the firmware's last copy to the LCD, before the menu is drawn) */
#ifdef PF_CRASH_SELFTEST
  task_yield(3000);
  crash_selftest_load();
#endif
  int last = -1, simple = 0;
  for (;;) {
    set_status("HOLD EXIT TO QUIT");
    int t = simple ? -2 : front(last);
    if (t == -1) {  /* left from the menu: as EXIT held (entry.c) */
      quit_req = 1;
      for (;;) task_yield(20);
    }
    if (t == -2) {  /* no front program: this one's own menu */
      simple = 1;
      options_read();  /* (a table may have changed them: its pause's A and S) */
      menu(failed, why);
      t = choose();
      if (t < 0) continue;
      if (cfg_changed) {
        file_save("PINBALL.CFG", cfg, sizeof cfg);
        cfg_changed = 0;
      }
    }
    table = sound_module = t;
    bank_load(t);  /* (without it the table plays silent) */
    why = load_table(t);
    failed = why ? t : -1;
    if (why) {
      simple = 1;  /* (its menu says why) */
      continue;
    }
    last = t;
    const PfGameHeader* h = (const PfGameHeader*)GAME_BASE;
    host.dspVoices = sound_ready() ? 8 : 0;
    crash_table = t + 1;
    h->entry(&host);
    wheel_led(0);
    lcdc_restore();  /* (the table program puts it back as it ends; this, whatever it did) */
    crash_table = 0;
    dsp_silence();
    keys_sound(1);
  }
}
