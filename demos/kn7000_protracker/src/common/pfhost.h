/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * pfhost.h -- between KN7APP.BIN (the platform: firmware search, files, task, panel, keybed, DSP) and a table
 * program PFn.BIN (Encore's engine with one table's rules, the picture and the sound). KN7APP.BIN loads the
 * table program at GAME_BASE, checks its header, clears its bss, runs its constructors and calls its entry on
 * the game task, with a PfHost. The table program returns when the player leaves the table. */
#ifndef PF_PFHOST_H
#define PF_PFHOST_H
#include "kn7.h"

#define PFHOST_ABI 9
#define PFGAME_MAGIC 0x4D474650u  /* "PFGM" */
#define PFHOST_MAGIC 0x54534850u  /* "PHST" */

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
} PfHost;

/* The header at the start of PFn.BIN (GAME_BASE). */
typedef struct PfGameHeader {
  u32 magic;                  /* PFGAME_MAGIC */
  u32 abi;                    /* PFHOST_ABI */
  u32 size;                   /* bytes in the file */
  u32 table;                  /* 0 to 3: which table's rules it carries */
  u32 bssLo, bssHi;           /* cleared before the entry */
  u32 initLo, initHi;         /* the constructors: void (*)(void), run in order */
  u32 end;                    /* the first byte past everything it uses (image and bss) */
  int (*entry)(const PfHost* host);  /* runs the table; returns when the player leaves it */
  u32 crc;                    /* CRC-32 of the file from offset 0x40 to its end (mkgame.py) */
  u32 reserved[5];
} PfGameHeader;

#ifdef __cplusplus
}
#endif
#endif
