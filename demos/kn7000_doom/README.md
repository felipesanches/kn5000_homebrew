# Doom for the KN7000

[Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) on the Technics KN7000, run by the KN7000 program
loader (`KN7LOAD.CNV`, STYLE CONVERT) from the program menu's folder `DOOM\` on the SD card, with the shareware
`DOOM1.WAD`.

## Status (2026-10-29)

In MAME (program 893) it starts from the program menu, prints Chocolate Doom's startup log on the LCD, shows the title
screen and plays the demos (`tests/results/`); holding EXIT ends it and gives the instrument back. There is no input
yet (the panel and the keybed come next), no sound, and it has not run on the instrument.

## Building

    make              # build/KN7APP.BIN (the platform) and build/DOOM.BIN (Chocolate Doom)
    make card-dir     # build/card/DOOM/: those, MANIFEST.TXT and DOOM1.WAD (WAD=... names another)
    make host         # build/host/doom: the same game code on the PC (tests/host/)
    tests/mame/run_doom.sh ~/compartilhado/tmp/doom-mame    # a session in MAME (tests/mame/)

It needs the mn10300-elf toolchain the Pinball port builds (GCC patched to make no `(d32,An)` operand), the KN7000
loader's tools (technics-roms-disasm `kn7000/tools/cnv_romdump`) and Another World's DSP tools, as ProTracker does.

## How it works

Two programs, as ProTracker and Pinball Fantasies have them (`src/common/pfhost.h` between them):

- **`KN7APP.BIN`, the platform** (`src/app/`, ProTracker's with ABI 13): the loader runs it on each converter call.
  It finds the firmware's routines, starts a task for the program, serves its file reads, keeps the firmware from
  drawing, and hands the panel's switches over.
- **`DOOM.BIN`, Chocolate Doom** (`src/choco/`, its sources unchanged but for a few lines behind `CHOCO_KN7`;
  `src/kn7/`, what takes SDL's place): the LCD, the timer, the WAD, the C library's files and formatted I/O.

**Memory.** Doom wants about 1.7 MB: an image of 356 KB, 317 KB of static data, a zone of up to 953 KB for the
shareware maps (E1M6, `tests/results/zone-minimum-host-2026-10-29.txt`) and 50 KB of heap. A program has 1.3 MB of
free RAM on the KN7000 (`src/common/kn7.h`, ARENA1-7), so:

- the image is linked to ARENA2, its static data to the program area at GAME_BASE, but for the visplanes and openings
  (123 KB) in ARENA1 (`src/kn7/doom.ld`);
- Chocolate Doom's printf and scanf families are a small formatter of our own (`src/kn7/fmt.c`, 70 KB less);
- the zone is one zone over every free region, joined by blocks that are never freed (`z_zone.c`, `CHOCO_KN7`);
- on programs 893 and 941 KN7APP.BIN lends Doom 584 KB of the firmware's RAM that only its user interface and its
  drawing use (the frame buffers, the SD player's lists, the LZSS trees, two file buffers: `src/app/borrow.c`), keeps
  its contents in the LCD controller's display memory meanwhile, and puts them back at the end.

**The WAD** is read where Doom asks, a few sectors at a time, by the program menu's FAT16 reader over the loader's
raw sector read (`src/app/folder.c`): the firmware's file routines read a file from its start only.

**The screen.** The LCD is set to 8 bits a pixel through its look-up table, which holds Doom's palette (4 bits a
colour). Doom's 320 x 200 frame is in the middle of the 640 x 240 LCD, its lines shown as 240 as Chocolate Doom's aspect
ratio correction does; a line goes to the LCD only when it changed.
