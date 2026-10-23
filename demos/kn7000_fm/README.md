# KN7FM: a DX7-style FM synthesizer for the KN7000

A program for the Technics SX-KN7000 that turns it into a six-operator FM synthesizer in the manner of the Yamaha
DX7, with an editor on the instrument's own screen and panel. The sound is computed by a program of ours in the
KN7000's effects DSP (IC306, an ADSP-21065L "SHARC"), loaded through the firmware's own host-port routines as the
DSPTONE and Another World programs do. It is an application of the KN7000 program loader (`KN7LOAD.CNV`, started
from DISK MENU > STYLE CONVERT; the program menu's folder `KN7FM\`).

> **Status (2026-10-23): runs in MAME; not yet tried on the instrument.** The engine's model is checked against
> msfa (the DX7 engine Dexed uses); the DSP program against the model, word for word, in a simulator; the main CPU's
> program in MAME (program 893, through the loader floppy and the program menu): it finds the firmware, loads and
> hooks the DSP, plays the keybed's notes on eight voices, edits, and puts the firmware's effects back when it ends.
> Not yet: a sample-exact check of the DSP's output in MAME (under way), the cycle budget on the chip, LOAD/SAVE in
> MAME, the instrument.

## What it is meant to do

- **Sound.** Eight voices of six operators, the DX7's 32 algorithms, operator feedback, four-rate/four-level
  envelopes, keyboard level and rate scaling, velocity sensitivity, ratio and fixed frequencies with fine tuning and
  detune, the pitch envelope, the LFO (six waveforms, delay, pitch and amplitude modulation), oscillator key sync,
  transpose. Played from the keybed (and MIDI IN), while the instrument's own sound for the keys is muted.
- **Patches.** DX7 voices, so the thousands of DX7 banks in circulation load as they are: a 32-voice bulk dump
  (`.SYX`, 4,104 bytes) on the SD card is read at the start and written back on SAVE. A bank of patches of our own
  is built in for when there is none.
- **Editing on the instrument's screen.** The LCD shows the algorithm as a diagram, each operator's envelope as a
  graph, and a strip of 16 parameter columns. The KN7000 has a pair of UP/DOWN buttons under each of the 16 columns
  of its screen (the part MUTE buttons): each column's buttons change its value, and the diagram and the graphs
  follow at once. The soft keys beside the screen pick the page (an operator, the voice, the LFO and pitch envelope,
  load and save); PAGE UP/DOWN step through the bank.

## The engine

`tools/fm_model.py` is the reference: every number the DSP computes is defined there first, and the DSP program is
checked against it word for word (`tools/fm_sim.py`, to come). It follows msfa (Raph Levien's DX7 engine, Google,
Apache License 2.0, as Dexed carries it), with its structure fitted to the DSP:

| part                      | how                                                                      | why                                                                                      |
|---------------------------|--------------------------------------------------------------------------|------------------------------------------------------------------------------------------|
| operator                  | a 24-bit phase, a 256-point sine with linear interpolation, times a gain | memory: the DSP has about 1,200 free words                                               |
| gain                      | moves linearly over 64 frames from one control value to the next         | msfa's own interpolation over its 64-sample blocks                                       |
| algorithms                | msfa's encoding of the 32: two buses, the output, self-feedback          | the DSP runs code generated for the patch's algorithm, swapped when it changes           |
| control                   | one operator of one voice a frame: each operator every 64 frames         | msfa's control rate exactly, so its envelope increments stay as they are                 |
| envelopes                 | msfa's, with its "static" stage times                                    | DX7 timing                                                                               |
| exp2 (gains, frequencies) | a cubic for 2^f and SCALB, in float                                      | no room for tables; 1.4e-4 relative error (0.0012 dB, 0.24 cent)                         |
| carriers                  | scaled 1/32 of msfa's                                                    | eight voices of six carriers sum without overflow; one carrier at full level is -18 dBFS |

Fixed-point arithmetic is chosen so that nothing overflows: on the chip the kernel's ALUSAT makes adds saturate,
while MAME's recompiler wraps them, so a program that relied on either would behave differently in the two.

### Checked against msfa

`tests/msfa_ref/` builds msfa itself (fetched at a pinned Dexed commit; nothing of it is committed) into a one-note
renderer, `msfa_ref`, and `compare.py` renders the same note of the same voices with msfa and with the model:

| test (note C4, velocity 100, 32 voices, one per algorithm)            | result                                                                                              |
|-----------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------|
| random voices: level                                                  | median 0.00 dB, worst -0.52 dB                                                                      |
| random voices: envelope (RMS every 10 ms)                             | median error 0.13 dB, worst 0.97 dB                                                                 |
| the same with the LFO off: envelope                                   | median 0.10 dB, worst 0.75 dB                                                                       |
| steady-state spectra (all rates 99, L1 = L2 = L3), peaks within 40 dB | median difference 0.1-1.2 dB for 31 algorithms; algorithm 5 at feedback 7, which is chaotic, 4.5 dB |
| the same, every voice at feedback 7 (`steady.py MSFA_REF 7`)          | algorithm 32, whose feedback operator is a carrier: median 0.26 dB, 90th percentile 1.35 dB         |

Waveform correlation is not a measure here: a vibrato or a detuned pair of operators a fraction of a cycle apart
decorrelates two signals with the same spectrum (voice 0: correlation -0.07, the same partials within 0.5 dB).

```sh
tests/msfa_ref/fetch.sh ~/compartilhado/tmp/kn7fm/msfa
M=~/compartilhado/tmp/kn7fm/msfa
g++ -O2 -std=c++17 -I $M/msfa -I $M -o $M/msfa_ref tests/msfa_ref/msfa_ref.cpp $M/msfa/{env,exp2,fm_core,fm_op_kernel,freqlut,lfo,pitchenv,sin}.cc
python3 tests/msfa_ref/compare.py $M/msfa_ref --n 32            # random voices
python3 tests/msfa_ref/compare.py $M/msfa_ref --n 32 --nolfo    # the same, LFO off
python3 tests/msfa_ref/steady.py $M/msfa_ref                     # steady-state spectra, algorithm by algorithm
python3 tests/msfa_ref/steady.py $M/msfa_ref 7                   # the same, all at feedback 7
```

## Where it lives in the DSP

The DSP runs the firmware's kernel, which calls ten effect units every frame; ours is called from unit 0's slot
(the panel REVERB) and unit 9's (the CHORUS), as Another World's mixer is, so it is heard with REVERB on or off.
The firmware's own effect programs are not touched and run again when the slots are put back. Memory the firmware
never uses (technics-roms-disasm `kn7000/dsp/records.tsv`: no firmware record loads there, and no DSP program
addresses it):

| memory                          | what                                                                      | evidence                                                                                                 |
|---------------------------------|---------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------|
| PM 0x8102-0x82FF, 0x8340-0x83FF | the resident program: commands, the control step, the LFO, exp2           | 0x8102-0x82FF: power-on contents on the owner's unit (pass 7); DSPTONE and Another World ran there       |
| PM 0x8E00-0x8FFF                | the algorithm's kernel, two buffers (a trampoline points at the live one) | no record loads it, no instruction refers to it                                                          |
| DM 0x9D00-0x9EFF                | 8 voices x 64 words                                                       | power-on contents on the unit (pass 7)                                                                   |
| DM 0x9B20-0x9C3F, 0x9C60-0x9CFF | the sine table and the variables; the patch's 67 words                    | no record loads them, no instruction refers to them (the kernel's per-unit state ends at 0x9B1F, 0x9C53) |
| DM 0x9F00-0x9F75                | msfa's "static" table; the key-on's staging block                         | the same                                                                                                 |

Commands: the main CPU stages a key-on's 18 words, then writes MSGR6 (bit v key-on voice v, bit 8 + v key-off);
the DSP takes them at its next frame and writes MSGR6 = 0. A kernel runs about 14 cycles an operator.

## Checked

| what                                                       | how                                                                        | result                                                                                                                                                                                                 |
|------------------------------------------------------------|----------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| every SHARC word means what it was written for             | `tools/fm_dsp.py --check`: MAME's unidasm over all 3,619 words             | all disassemble as meant                                                                                                                                                                               |
| the DSP program computes the model, word for word          | `tools/fm_sim.py --seconds 1.5`: all 32 algorithms, 8 voices, random notes | 66,150 frames per algorithm, 37-53 key events each: every sample and every voice word identical; no overflow                                                                                           |
| its cost                                                   | the same run: instructions, + 2 per branch taken                           | at most 1,252 cycles a frame, mean 669-894 by algorithm; the frame is about 1,360 at 60 MHz, less the kernel's own work: see below                                                                     |
| the main CPU computes the DSP's words exactly              | `tests/mame/check_words.py` on a MAME run's dumps                          | the bank (32 voices), the patch words (67 of 67, two voices), the key-on words (18 of 18, three notes) equal the model's                                                                               |
| it runs in MAME (program 893, the loader floppy, the menu) | `tests/mame/run_fm.sh OUT` (`fm_test.lua`)                                 | finds everything, hooks units 0 and 9, DSP at 44,1xx frames/s; C4 -15.9 dBFS, a chord on three voices; every voice ends after release; PAGE UP, the editor's pages; EXIT puts both slots back as found |

The budget is the open question for the instrument: the kernel's other eight units run in the same frame, and
how many cycles they leave is not known (the DSP's clock is not confirmed either). Eight voices may be too many
there; the engine plays `NV` voices (a DSP variable), so the main CPU can lower it.

In MAME the keybed's own sound is still heard: the expression (CC 11) KN7FM sends to mute it is not emulated
(Another World's port records the same).

## Building and running

    make            # build/KN7APP.BIN (needs technics-roms-disasm beside kn5000_homebrew, and MAME's unidasm)
    make card-dir   # build/card/KN7FM/: KN7APP.BIN, MANIFEST.TXT, ICON.BMP, for the program menu's card
    make sim        # the DSP program against the model, 0.5 s per algorithm
    tests/mame/run_fm.sh ~/compartilhado/tmp/kn7fm/run    # a MAME session (the published overlay, -bios v13)
    python3 tests/mame/check_words.py ~/compartilhado/tmp/kn7fm/run

On the card: the folder `KN7FM\` beside the menu's others; a DX7 32-voice dump named `BANK0.SYX` there is the bank
at the start (else the built-in one); `BANK1.SYX` ... `BANK9.SYX` load and save from the BANK page.

## Playing and editing

| control               | what                                                                            |
|-----------------------|---------------------------------------------------------------------------------|
| the keybed, MIDI IN 1 | the notes (any channel); the sustain pedal (CC 64)                              |
| PAGE UP / DOWN        | the next / previous voice of the bank                                           |
| LCDL 1-4, LCDR 1-2    | OP1-OP6: envelope, level, frequency; again: keyboard scaling                    |
| LCDR 3                | VOICE: algorithm, feedback, sync, transpose, the LFO; again: the pitch envelope |
| LCDR 4                | BANK: the file number, LOAD (UP), SAVE (UP twice), the voice's name             |
| MUTE UP / DOWN 1-16   | the column above: one step (held: repeats)                                      |
| EXIT held 1.5 s       | leave: the firmware's effects back, the keybed's sound back                     |

## Files

| file                                  | what                                                                                                 |
|---------------------------------------|------------------------------------------------------------------------------------------------------|
| `tools/fm_model.py`                   | the DX7 voice formats, the main CPU's computations, the engine bit for bit; `render` a note to a WAV |
| `tools/fm_dsp.py`                     | the DSP program: the resident unit and the 32 kernels; `--check` against unidasm                     |
| `tools/sharc_enc.py`                  | an assembler for the ADSP-21065L instructions the DSP program uses (checked against MAME's unidasm)  |
| `tools/fm_sim.py`                     | the DSP program run in a simulator of its instructions, against the model, frame by frame            |
| `tools/fm_host.py`                    | the main CPU's tables, the 32 diagrams, the built-in bank (`--wav` renders its voices)               |
| `tools/icon.py`, `tools/check_map.py` | the menu's icon; the memory map's check                                                              |
| `src/kn7fm.s`                         | the application: the converter's steps, the files, the task                                          |
| `src/dsp.s`, `src/fwfind.s`           | the DSP's host port and the firmware search (Another World's, copied), the engine's load             |
| `src/synth.s`                         | notes, voices, the key-on and patch words, the bank's SysEx                                          |
| `src/ui.s`                            | the screen and the panel                                                                             |
| `tests/msfa_ref/`                     | msfa at a pinned Dexed commit, a one-note renderer, the model against it (`compare.py`, `steady.py`) |
| `tests/mame/`                         | the MAME sessions (`run_fm.sh`, `fm_test.lua`, `fm_capture.lua`) and their checks                    |

## Credits

The engine follows msfa by Raph Levien (Copyright 2012 Google Inc., Apache License 2.0), whose DX7 tables and
formulas are copied in `tools/fm_model.py` and named there; Dexed (Pascal Gauthier and contributors, GPL-3) carries
it. The DX7 voice format is Yamaha's.
