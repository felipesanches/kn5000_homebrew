# KN7FM: a DX7-style FM synthesizer for the KN7000

A program for the Technics SX-KN7000 that turns it into a six-operator FM synthesizer in the manner of the Yamaha
DX7, with an editor on the instrument's own screen and panel. The sound is computed by a program of ours in the
KN7000's effects DSP (IC306, an ADSP-21065L "SHARC"), loaded through the firmware's own host-port routines as the
DSPTONE and Another World programs do. It is an application of the KN7000 program loader (`KN7LOAD.CNV`, started
from DISK MENU > STYLE CONVERT; the program menu's folder `KN7FM\`).

> **Status (2026-10-23): in development.** Done: the reference model of the engine and its check against msfa, the
> DX7 engine Dexed uses. Not yet: the DSP program, the main CPU's program, the editor, any run in MAME or on the
> instrument.

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

| memory                          | what                                     | evidence                                                                                                                    |
|---------------------------------|------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| PM 0x8102-0x82FF                | the program                              | power-on contents on the owner's unit (pass 7); DSPTONE and Another World ran there                                         |
| PM 0x8E00-0x8FFF                | the algorithm's code, two buffers        | no record loads it, no instruction refers to it                                                                             |
| DM 0x9D00-0x9EFF                | the sine table, the patch, the variables | power-on contents on the unit (pass 7)                                                                                      |
| DM 0x9B20-0x9C3F, 0x9F00-0x9FFF | the voices                               | no record loads them, no instruction refers to them (the kernel's per-unit state ends at 0x9B1F, its scrub table at 0x9C53) |

## Files

| file                                      | what                                                                                                 |
|-------------------------------------------|------------------------------------------------------------------------------------------------------|
| `tools/fm_model.py`                       | the DX7 voice formats, the main CPU's computations, the engine bit for bit; `render` a note to a WAV |
| `tests/msfa_ref/fetch.sh`, `msfa_ref.cpp` | msfa at a pinned Dexed commit; a one-note renderer built on it                                       |
| `tests/msfa_ref/compare.py`               | the model against msfa: level, envelope, spectra                                                     |
| `tests/msfa_ref/steady.py`                | the same voices made static: steady-state spectra, algorithm by algorithm                            |
| `tools/sharc_enc.py`                      | an assembler for the ADSP-21065L instructions the DSP program uses (checked against MAME's unidasm)  |

## Credits

The engine follows msfa by Raph Levien (Copyright 2012 Google Inc., Apache License 2.0), whose DX7 tables and
formulas are copied in `tools/fm_model.py` and named there; Dexed (Pascal Gauthier and contributors, GPL-3) carries
it. The DX7 voice format is Yamaha's.
