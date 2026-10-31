/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * pfhost.h -- between KN7APP.BIN (the platform: firmware search, files, task, panel, keybed, DSP) and the program it
 * runs, PDF.BIN (the PDF viewer). It is Doom's (kn7000_doom), which is ProTracker's and the Pinball Fantasies port's
 * interface (Pinball kn7000/src/common/pfhost.h) with ABI 10 added: the program codes its own sample bank in RAM and the
 * platform sends it to the DSP (dspSend); ABI 12 lists the program's folder (list); ABI 13 (Doom) reads a file anywhere
 * in it (rawOpen, rawRead) and loads the program where its header says (PfGameHeader::base); ABI 14 (the viewer) loads
 * segments of the program to other regions as well (PfSegment); ABI 15 asks it for its state when KN7APP's watchdog
 * finds its task stuck (stuck); ABI 16 keeps each switch press until the program takes it (panelHit). KN7APP.BIN loads the program, checks its header,
 * clears its bss, runs its constructors and calls its entry on the game task, with a PfHost. The program returns when
 * the player leaves it. */
#ifndef PF_PFHOST_H
#define PF_PFHOST_H
#include "kn7.h"

#define PFHOST_ABI 16
#define PFGAME_MAGIC 0x4D474650u  /* "PFGM" */
#define PFHOST_MAGIC 0x54534850u  /* "PHST" */
#define PDF_PROGRAM 0x50  /* PfGameHeader::table of PDF.BIN */

#ifdef __cplusplus
extern "C" {
#endif

/* A piece of a file being read: `bytes` of it into `dst`, or skipped when dst is 0. */
typedef struct PfPiece {
  void* dst;
  u32 bytes;
} PfPiece;

/* A file in the program's folder (PfHost::list): its 8.3 name as the card's FAT holds it, and its size. */
typedef struct PfFile {
  char name[13];
  u32 bytes;
} PfFile;

/* A file of the program's folder opened for reading anywhere in it (PfHost::rawOpen): read by sectors, with the program
 * menu's FAT16 reader, never through the firmware's file routines. The program fills in `chain` (room for maxClusters
 * of the file's clusters) and `bounce` (bounceSectors x 512 bytes of work RAM, which the card's reads go through); the
 * platform fills in the rest. */
typedef struct PfRawFile {
  u32 bytes;          /* the file's size */
  u32 clusterBytes;   /* the card's cluster */
  u32 clusters;       /* the file's clusters: rawOpen with maxClusters too small tells how many */
  u32* chain;         /* each cluster's first card sector */
  u32 maxClusters;
  u8* bounce;
  u32 bounceSectors;
} PfRawFile;

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
  /* ABI 12: the files in the program's folder (prefix), in the directory's order, at most max: read off the card by the
   * converter entry, as load is, with the program menu's FAT16 reader. -> how many, or < 0 (no raw sector read, not a
   * FAT16 card, no such folder, a read error). */
  s32 (*list)(PfFile* files, u32 max);
  /* ABI 13: a file of the program's folder for reading at any offset (PfRawFile): -> 0, -1 (no raw sector read), -2 (not
   * a FAT16 card), -3 (no such folder or file), -4 (a read error), -5 (more clusters than maxClusters: f->clusters says
   * how many). Then `bytes` of it from `offset` into dst -> the bytes read, or -4. Both made by the converter entry on
   * the firmware's main task, as load is; the program's task waits. */
  s32 (*rawOpen)(const char* name, PfRawFile* f);
  s32 (*rawRead)(const PfRawFile* f, u32 offset, void* dst, u32 bytes);
  /* ABI 14: the switches the program takes from the firmware (a bitmap laid out as `panel`): those the firmware never
   * sees change; every other switch acts as the firmware makes it act, and reads in `panel` as ever. Until the program
   * calls this, it takes them all (but CONTRAST UP and DOWN). Program 893 only, as panelScan. */
  void (*panelTake)(const u8* mask);
  /* ABI 14: a line into KN7APP's log (no newline), and the log saved on the card as PDFLOG.TXT in the program's folder
   * (waits for the converter entry, as save does) */
  void (*diag)(const char* line);
  void (*diagSave)(void);
  /* ABI 15: set by the program (KN7APP leaves it 0): when the watchdog finds the program's task has not yielded for
   * 4 s, the task is made to leave what it was doing and run KN7APP's report, which calls this to put the program's
   * own state into the log (diag) before the log is saved; the program's task never goes back */
  void (*stuck)(void);
  /* ABI 16: each switch that went down since the program cleared it, a bit each, laid out as `panel`, set by every
   * switch frame KN7APP takes from the panel's ring: a press and its release that both come while the program reads
   * no switches (a page loading, a file being written) still show here. The program clears the bits it takes. */
  u8* panelHit;
} PfHost;

/* The header at the start of the program's file, which is read to `base` (ABI 13: PT.BIN was read to GAME_BASE; DOOM.BIN
 * is linked to ARENA2 with its bss at GAME_BASE). ABI 14: reserved[0] is how many segments follow the main image in the
 * file, in the order of the table of PfSegment right after the header (at base + 0x40); each is read to its address,
 * which lies in one of the regions (PfHost::regions), and checked by its CRC-32. The header's crc covers the main image
 * from 0x40 on, the table with it. */
typedef struct PfGameHeader {
  u32 magic;                  /* PFGAME_MAGIC */
  u32 abi;                    /* PFHOST_ABI */
  u32 size;                   /* bytes in the file */
  u32 table;                  /* PDF_PROGRAM (the Pinball port's table number) */
  u32 bssLo, bssHi;           /* cleared before the entry */
  u32 initLo, initHi;         /* the constructors: void (*)(void), run in order */
  u32 end;                    /* the first byte past everything it uses (image and bss) */
  int (*entry)(const PfHost* host);  /* runs the program; returns when the player leaves it */
  u32 crc;                    /* CRC-32 of the file from offset 0x40 to its end (mkgame.py) */
  u32 base;                   /* ABI 13: where the file is read to (the header's own address) */
  u32 reserved[4];
} PfGameHeader;

#define PF_MAX_SEGMENTS 8
typedef struct PfSegment {
  u32 addr;   /* where it is read to */
  u32 bytes;
  u32 crc;    /* CRC-32 of its bytes */
  u32 pad;
} PfSegment;

#ifdef __cplusplus
}
#endif
#endif
