/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * pfhost.h -- between KN7APP.BIN (the platform: firmware search, files, task, panel, keybed, DSP) and the program it
 * runs, PT.BIN (ProTracker). The Pinball Fantasies port's interface (Pinball kn7000/src/common/pfhost.h), with ABI 10
 * added: the program codes its own sample bank in RAM and the platform sends it to the DSP (dspSend). KN7APP.BIN loads
 * the program at GAME_BASE, checks its header, clears its bss, runs its constructors and calls its entry on the game
 * task, with a PfHost. The program returns when the player leaves it. */
#ifndef PF_PFHOST_H
#define PF_PFHOST_H
#include "kn7.h"

#define PFHOST_ABI 11
#define PFGAME_MAGIC 0x4D474650u  /* "PFGM" */
#define PFHOST_MAGIC 0x54534850u  /* "PHST" */
#define PT_PROGRAM 0x50  /* PfGameHeader::table of PT.BIN */

#ifdef __cplusplus
extern "C" {
#endif

/* A piece of a file being read: `bytes` of it into `dst`, or skipped when dst is 0. */
typedef struct PfPiece {
  void* dst;
  u32 bytes;
} PfPiece;

/* A stretch of RAM the table program may use as it likes until it returns. */
typedef struct PfRegion {
  u32 lo, hi;
} PfRegion;

/* One voice of the effects DSP's mixer (dsp.c): what it plays from the sample bank. */
typedef struct PfVoice {
  u32 base;      /* the sample's first word in the DSP's SDRAM bank (four 8-bit samples a word) */
  u32 position;  /* where in it to start, in bytes, 16.16 */
  u32 step;      /* bytes a DSP frame (44,100 a second), 16.16 */
  u32 end;       /* its end, in bytes from base */
  u32 loop;      /* the loop's length in bytes (going past the end goes back by it), or 0: no loop */
  u32 volume;    /* 0 to 64: each sample times this (the mixer's VOL; 64 at full scale is about -18 dBFS a voice) */
} PfVoice;

typedef struct PfHost {
  u32 magic, abi;
  u32 build;                 /* the firmware's PROGRAM build (893, 935, 941, 942), or 0 */
  const char* prefix;        /* the game's folder for file names ("PINBALL\"), or "" */
  /* time */
  u32 (*ms)(void);           /* the kernel's millisecond counter */
  u32 msCounter;             /* its address, for reading it without a call */
  void (*yield)(u32 ms);     /* lets the firmware run (at least ms); ends the task here when the player quits */
  /* files: read by the converter entry on the firmware's main task; the game task waits */
  s32 (*load)(const char* name, const PfPiece* pieces, u32 count);  /* bytes read, or < 0 */
  s32 (*save)(const char* name, const void* data, u32 bytes);        /* the file made afresh: bytes written, or < 0 */
  /* input */
  const volatile u8* panel;  /* the firmware's panel bitmap (kn7.h BTN_*) */
  void (*pollNotes)(void);   /* brings notes[] up to date from the keybed's MIDI OUT ring and MIDI IN 1 */
  const u16* notes;          /* [128]: the MIDI channels holding each note */
  /* memory */
  const PfRegion* regions;   /* the RAM the table program may use, ending with {0, 0} */
  /* text, for messages (8 x 13, RGB565) */
  void (*text)(const char* s, int x, int y, u16 fg, u16 bg);
  void (*fill)(int x, int y, int w, int h, u16 colour);
  /* the effects DSP: 0 when it is not ours (its routines not found, or the bank not loaded) */
  u32 dspVoices;                                       /* how many voices it mixes */
  void (*dspBank)(u32 sample, u32* base);              /* where sample n of the loaded bank starts (word) */
  void (*dspSet)(u32 voice, const PfVoice* v, u32 how); /* how: 0 stop, 1 start, 2 change step and volume */
  void (*dspFlush)(void);                              /* sends what dspSet queued, as one command */
  /* what the table program asks the platform to show in the converter's text line */
  void (*status)(const char* s);
  /* the keybed's own sound (the instrument's notes for the keys that play the game): on or off; and as it is */
  void (*keybedSound)(int on);
  int (*keybedSoundOn)(void);
  /* input, ABI 2: the MIDI channels whose sustain pedal (CC 64) is down, as pollNotes last brought it */
  const u16* sustain;
  /* ABI 3: the LCD controller's start address back as the firmware had it (the table program scrolls with it) */
  void (*lcdRestore)(void);
  /* ABI 6: the TEMPO/PROGRAM wheel's detents so far, signed (clockwise up); the firmware's tempo is left as it was */
  const volatile s32* wheel;
  /* ABI 7: for the front program (PF0.BIN, header table 4): the table played last (0-3), or -1 the first time */
  s32 lastTable;
  /* ABI 8: KN7APP's 8 x 13 font: 95 glyphs from ' ', 13 rows each, the leftmost dot in bit 7 */
  const u8* font;
  /* ABI 9: the TEMPO/PROGRAM wheel's LED blinking (1) while the game wants the wheel, or as the firmware had it (0) */
  void (*wheelLed)(int blink);
  /* ABI 10: a sample bank from RAM into the DSP's SDRAM, from its word dspBase. `records`: the bank coded as
   * Another World's tools/dsp_bank.py codes it (blocks of u32 n and n DM words, the first two the block's first SDRAM
   * word from dspBase and its words), `bytes` of them; `words`: the SDRAM words the bank fills; `sum`: dsp_bank.checksum
   * of them; `id`: names the bank (the DSP keeps it in its marker, and a bank with the same id already there and
   * checked is not sent again). Waits while the converter calls send it: 1 when it is in SDRAM and checked, 2 when not
   * (no sound). Nothing may play meanwhile. */
  int (*dspSend)(const u32* records, u32 bytes, u32 words, u32 sum, u32 id);
  u32 dspBase;    /* the bank's first SDRAM word: a voice's base is dspBase + the sample's word in the bank */
  u32 dspMaxWords; /* the SDRAM words a bank may fill */
  /* ABI 11: the panel's receive ring read now, from the program's task (as the converter calls read it): the switches
   * into `panel` and the wheel's detents into `wheel`, each frame taken from the firmware before its main task decodes
   * it. The firmware's tasks do not preempt each other, so this never meets the main task halfway through a frame. */
  void (*panelScan)(void);
  /* ABI 11: the keybed's (and MIDI IN 1's) note-ons since the last call, in order, each with its velocity, at most max;
   * pollNotes brings them in. And the modulation wheel (CC 1), 0-127. */
  u32 (*noteOns)(u8* note, u8* velocity, u32 max);
  const volatile u8* modWheel;
} PfHost;

/* The header at the start of PT.BIN (GAME_BASE). */
typedef struct PfGameHeader {
  u32 magic;                  /* PFGAME_MAGIC */
  u32 abi;                    /* PFHOST_ABI */
  u32 size;                   /* bytes in the file */
  u32 table;                  /* PT_PROGRAM: (the Pinball port's table number) */
  u32 bssLo, bssHi;           /* cleared before the entry */
  u32 initLo, initHi;         /* the constructors: void (*)(void), run in order */
  u32 end;                    /* the first byte past everything it uses (image and bss) */
  int (*entry)(const PfHost* host);  /* runs the program; returns when the player leaves it */
  u32 crc;                    /* CRC-32 of the file from offset 0x40 to its end (mkgame.py) */
  u32 reserved[5];
} PfGameHeader;

#ifdef __cplusplus
}
#endif
#endif
