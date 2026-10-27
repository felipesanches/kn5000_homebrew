# ProTracker for the KN7000

The Amiga's ProTracker 2.3D on the Technics SX-KN7000: its main screen on the instrument's LCD, its replayer playing
modules through the effects DSP, and the panel and keybed as its keys. It is a port of **pt2-clone** by Olav Sørensen
(8bitbubsy), the faithful C reimplementation of ProTracker 2.3D (BSD 3-Clause, `src/pt2/LICENSE`). It runs as an
application of the KN7000 program loader (`KN7LOAD.CNV`, started from DISK MENU > STYLE CONVERT), in the program
menu's folder `PROTRACK\`, as Another World, KN7FM and Pinball Fantasies do.

The test modules are the soundtrack of Pinball Fantasies (Digital Illusions, 1992/1994): `INTRO.MOD`
("Steelchambers2"), `MOD2.MOD` ("adrenaline") and the four tables' `TABLE1.MOD` .. `TABLE4.MOD`, all ProTracker
modules ("M.K.", 31 samples). They come from the player's own copy of the game and are not in this repository.

Status: see [What was checked](#what-was-checked).

Contents: [The card](#the-card) · [Playing](#playing) · [How it works](#how-it-works) ·
[What differs from the Amiga](#what-differs-from-the-amiga) · [Building](#building) ·
[What was checked](#what-was-checked) · [Files](#files)

## The card

| file           | what                                                                                                   |
|----------------|--------------------------------------------------------------------------------------------------------|
| `KN7APP.BIN`   | the platform: firmware search, task, files, panel, wheel, keybed, the DSP's mixer (the Pinball port's) |
| `PT.BIN`       | ProTracker: pt2-clone's replayer, loader and main screen, and the KN7000 side (`src/kn7/`)             |
| `MANIFEST.TXT` | the name the program menu shows ("ProTracker 2.3D") and its folder                                     |
| `MODS.TXT`     | the modules, one file name a line, in the order LCDR 1 and 2 step through them                         |
| `*.MOD`        | the modules (8.3 names), any ProTracker, NoiseTracker or Soundtracker module with 4 channels           |

Without `MODS.TXT` the program looks for the six Pinball Fantasies modules by name.

## Playing

DISK MENU, STYLE CONVERT, `KN7LOAD.CNV`, OK; then the program menu's "ProTracker 2.3D" row, RUN. The first module of
`MODS.TXT` is loaded and plays. ProTracker's screen is in the middle of the LCD; the labels beside it name the soft
keys left and right of the display.

| control                | what it does (ProTracker's own key)                                                                |
|------------------------|----------------------------------------------------------------------------------------------------|
| LCDL 1                 | PLAY: the song from the current position (right Amiga)                                             |
| LCDL 2                 | PATTERN: the current pattern, over and over (right Alt)                                            |
| LCDL 3                 | STOP                                                                                               |
| LCDL 4                 | EDIT: stop, and the edit mode on or off (space)                                                    |
| LCDL 5                 | the quadrascope or the spectrum analyzer (clicking the visualizer)                                 |
| LCDR 1, LCDR 2         | the module before, the module after, in `MODS.TXT` (Disk Op.)                                      |
| LCDR 3, LCDR 4         | the sample before, the sample after                                                                |
| LCDR 5                 | every channel muted, or none                                                                       |
| START/STOP             | PLAY, or STOP while playing                                                                        |
| PAGE UP, PAGE DOWN     | the song position back, on (while the song plays the replayer goes there); in the edit mode: a row |
| TEMPO/PROGRAM wheel    | the row (stopped), the sample (playing)                                                            |
| MUTE UP 1-4            | channel 1-4 muted or not (the mute buttons at the top right)                                       |
| MUTE DOWN 1-4          | the cursor to channel 1-4                                                                          |
| the keybed             | the current sample at that note, on the cursor's channel; in the edit mode, into the pattern too   |
| EXIT, held 1.5 s       | the end; then the firmware's message, EXIT, and NO at "Continue Next?"                             |

The keybed's C2 is ProTracker's C-1, and its three octaves are ProTracker's three. Keys above and below fold into them
by octaves. The instrument's own sound for the keys is off while ProTracker runs.

## How it works

- **KN7APP.BIN** (`src/app/`, C) is the Pinball Fantasies port's platform, with the table menu and the card-file
  sample banks left out:
  - the loader calls it on every converter call, on the firmware's main task;
  - each call does one step and answers request 5, which keeps the firmware decoding the panel and the keybed;
  - it finds the firmware's routines by their code, loads Another World's eight-voice sample mixer into the effects
    DSP, and starts a task of its own, which reads `PT.BIN` to `0x502E8000` and calls it.

  New for ProTracker is ABI 10's `dspSend`: the program codes a sample bank in RAM, and the converter calls send it
  through the host port, a slice of work a call.
- **PT.BIN** (`src/pt2/`, `src/kn7/`) is pt2-clone's replayer, module loader, tables, text output, pattern view and
  bitmaps, built for the MN10300 behind `PT2_KN7` (`src/kn7/pt2_kn7.h`). The parts of pt2-clone that need SDL, a mouse
  or an audio thread are replaced:
  - **Paula** (`paula.c`): the replayer writes Paula's registers as on the Amiga. Each write becomes a command of the
    DSP mixer's voices 4-7. A note's sample and its loop go as one command: start, end, and the loop's length.
  - **The samples** (`bank.c`): each sample starts on a whole 32-byte boundary, the layout the DSP's SDRAM keeps. They
    are coded with Another World's lossless code (`dsp_bank.py`, about 79 % of their bytes for these modules), and
    the DSP decodes them into SDRAM. The DSP's checksum confirms them.
  - **Time** (`main.c`): the replayer ticks by the kernel's millisecond counter and timer 4, 2.5/BPM seconds a tick
    (50 a second at 125 BPM, as the CIA timer does it). After each tick, Paula's changes go to the DSP.
  - **The screen** (`ui.c`, `video.c`): pt2-clone draws ProTracker's screen into a 320 x 240 buffer of 8-bit pixels.
    The LCD controller (an S1D13806) shows it at 8 bits a pixel, its look-up table holding ProTracker's 12-bit colours.
    Only the 16-bit words that changed are written to the LCD, and the writes stop every 16 rows for the ticks that
    are due.

Memory: `PT.BIN` takes about 79 KB with its variables, from `0x502E8000`. The rest is what KN7APP hands it (the
Pinball port's ARENA1-7, kn7.h):
- the largest region (ARENA2, 376 KB) holds the module's file while it is parsed, then its samples coded for the DSP;
- the next largest (ARENA4, 268 KB) holds the samples;
- the rest is the heap (the screen, the bitmaps, the patterns).

So a module may have up to 376 KB in its file and 268 KB of samples.

## What differs from the Amiga

- **The pattern shows 13 rows, not 15.** The LCD has 240 lines and ProTracker's screen 255. The pattern cursor and the
  VU meters move up with the middle row; the meters are 41 lines tall instead of 48, their bars scaled.
- **Paula's sound:** the DSP's mix is mono and unfiltered. The Amiga's channels 1 and 4 are left, 2 and 3 right, and its
  low-pass filters (the A500's, and the "LED" filter E0x switches) are not modelled.
- **Starts on a word:** the DSP's voice starts on a whole word of four samples, so a loop starting on an odd word plays
  up to three samples from before its start, once.
- **A loop set while a sample plays** (a new sample number without a note, or a loop changed in the middle): Paula
  takes it when the sample reaches its end. The DSP cannot be told in advance, so the program follows each voice's
  position and starts the DSP voice on the new loop when the old part has ended, up to one tick late.
- **EFx (invert loop)** changes the samples in the CPU's RAM, as on the Amiga, but not the DSP's copy, so it is not
  heard. **E8x** (Karplus-Strong) is off, as in pt2-clone by default.
- **Not part of the port:** the disk op. (modules come from `MODS.TXT`), saving, the sample editor, the position
  editor, the edit ops., the about screen, sampling, MOD2WAV. PowerPacker and XPK packed modules are not unpacked.

## Building

    make                # build/KN7APP.BIN and build/PT.BIN
    make card-dir       # build/card/PROTRACK/, the modules from PF_DATA (default ~/compartilhado/Projeto_Pinball/data)
    make test-bank      # the sample coder against dsp_bank.py (tests/bank/)

It needs:
- the Pinball port's toolchain: `mn10300-elf` GCC 14, patched to make no `(d32,An)` operand, which the KN7000 restarts
  on (Pinball `kn7000/toolchain/build.sh`);
- the loader's tools (technics-roms-disasm `kn7000/tools/cnv_romdump`: its font and `mkapp.py`);
- Another World's DSP tools (technics-anotherworld `kn7000/tools`: the mixer and its coder);
- MAME's `unidasm`.

To run it in MAME: `tests/mame/run_pt.sh OUT` (see `tests/mame/`).

## What was checked

(to be filled in by the first MAME session)

## Files

| path                | what                                                                                          |
|---------------------|-----------------------------------------------------------------------------------------------|
| `PROVENANCE.md`     | where the copied sources come from, and their licences                                        |
| `src/pt2/`          | pt2-clone's files the port uses, changed only behind `PT2_KN7`                                |
| `src/kn7/`          | the KN7000 side of PT.BIN: main, Paula, the sample bank, the LCD, the screen, the keybed      |
| `src/kn7/shim/`     | stand-ins for the pt2-clone headers of the parts left out                                     |
| `src/app/`          | KN7APP.BIN, the platform (the Pinball port's, ABI 10)                                         |
| `src/common/`       | the KN7000 and the platform's interface (`kn7.h`, `pfhost.h`)                                 |
| `tools/build/`      | the build's helpers (the Pinball port's): the DSP mixer to C, the font, the checks            |
| `tests/bank/`       | the sample coder against `dsp_bank.py`, on the PC                                             |
| `tests/mame/`       | a MAME session through the program menu, and its sound report                                 |
