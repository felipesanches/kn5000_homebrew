# ProTracker on the KN7000: a plan for editing

**Goal (owner, 2026-10-27):** the port must be viable and effective for editing modules, not only for playing them.
This plan covers:
- every control on the KN7000's panel, and what each does in ProTracker;
- the screen;
- the changes to the code, and how each is checked.

The first port (README.md) plays modules and has a minimal edit mode. Editing in ProTracker 2.3D needs these
operations, all on the Amiga's keyboard and mouse:

| ProTracker operation                                | Amiga                                      | needed on the KN7000                     |
|-----------------------------------------------------|--------------------------------------------|------------------------------------------|
| move the cursor: row, column, channel               | arrows, Tab, PgUp/PgDn, F6-F10             | yes                                      |
| enter notes                                         | two keyboard rows as a piano, F1/F2        | yes: the keybed is a real piano          |
| enter the sample number, effect and parameter (hex) | 0-9, A-F                                   | yes: 16 hex keys                         |
| delete a note, an effect, a whole cell              | Del, Shift+Del, Alt+Del                    | yes                                      |
| insert or delete a row (track or pattern)           | Shift+Return, Backspace                    | yes                                      |
| cut, copy, paste a track or a pattern               | F3-F5 with Shift and Alt                   | yes                                      |
| transpose a note, a track, a pattern                | Edit Op. 1                                 | yes                                      |
| edit skip (rows the cursor moves after a note)      | Alt+0-9 in the edit mode                   | yes                                      |
| effect macros                                       | Ctrl+0-9 store, Alt+0-9 recall             | yes                                      |
| play the song, the pattern; record; stop            | right Amiga, right Alt, right Shift, space | yes                                      |
| multi-channel entry; chords                         | Ctrl+M, Edit Op. 4                         | yes: the keybed plays chords             |
| the song: positions, the pattern at each, length    | the POS/PATTERN/LENGTH arrows, Pos Ed.     | yes                                      |
| tempo and speed                                     | the BPM arrows, Fxx                        | yes                                      |
| the sample: select, volume, finetune, loop          | the arrows on the left panel               | yes                                      |
| mute and solo channels                              | the mute buttons, right-click              | yes                                      |
| metronome                                           | Edit Op. 2                                 | useful                                   |
| step play (one row)                                 | Return in some versions                    | useful                                   |
| undo                                                | (pt2-clone: undo of the last change)       | yes: no mouse to repair a slip           |
| load and save modules                               | Disk Op.                                   | yes: without saving there is no editing  |
| new song (clear)                                    | CLEAR                                      | yes                                      |
| sample editor, sampling, MOD2WAV                    | the sampler screen                         | no: out of scope (no mouse, no sampling) |

## 1. The controls

The KN7000's panel (owner's manual QQTG0671 pp. 10-11; the MAME driver's `kn7000_cpanel.cpp` gives each switch's
place in the firmware's switch bitmap):
- 5 soft keys either side of the LCD;
- 16 MUTE UP/DOWN pairs under it;
- PAGE UP/DOWN, DISPLAY HOLD, EXIT;
- the TEMPO/PROGRAM wheel;
- TRANSPOSE and R1/R2 OCTAVE rockers;
- PART SELECT (LEFT, RIGHT 2, RIGHT 1) and the CONDUCTOR buttons;
- TECHNI-CHORD, SOLO;
- 18 SOUND GROUP buttons;
- 8 PART/GLOBAL EFFECT buttons;
- SEQUENCER PLAY and EASY REC, DISK, PROGRAM MENUS, SD;
- the PANEL MEMORY ring (1-8, SET, BANK VIEW, NEXT BANK);
- CUSTOM PANEL, CUSTOMIZE, FAVORITES;
- 16 RHYTHM GROUP buttons;
- AUTO PLAY CHORD, SOUND ARRANGER, MUSIC STYLIST, MUSIC STYLE ARRANGER;
- ONE TOUCH PLAY, SPLIT POINT, FADE IN/OUT, TAP TEMPO, SYNCHRO & BREAK, START/STOP;
- VARIATION 1-4, FILL IN 1-2, INTRO & ENDING 1-2;
- 6 PERFORMANCE PADS with AUTO SETTING, BANK and STOP;
- DEMO, OTHER PARTS/TG, HELP, CONTRAST;
- 4 volume sliders;
- the pitch-bend and modulation wheels;
- the sustain pedal;
- 76 keys with velocity.

The KN7000 has no DATA dial (the firmware keeps a handler for one, from the KN5000).

### The analogies

Each control is mapped so that what it does on the KN7000 suggests what it does in ProTracker:
- **The sequencer is the song.** SEQUENCER PLAY plays the song, and EASY REC is the edit mode. While a song plays, it
  is ProTracker's record mode.
- **The arranger's transport is ProTracker's transport.** START/STOP plays and stops. SYNCHRO & BREAK plays the
  pattern over and over, as a break loops.
- **The four VARIATION buttons are ProTracker's F6-F9.** They jump to rows 0, 16, 32 and 48 of the pattern. With
  SHIFT they store the current row in that button, as Shift+F6-F9 does.
- **INTRO & ENDING** go to the song's start and the pattern's last row (F10).
- **FILL IN inserts and removes rows,** as a fill-in fills a gap.
- **PERFORMANCE PADS play phrases**, so they cut, copy and paste them:
  - pads 1-3 cut, copy and paste the track;
  - pads 4-6 cut, copy and paste the pattern;
  - BANK undoes.
- **PANEL MEMORY stores and recalls,** as it does registrations:
  - 1-8 put an effect macro (command and parameter) at the cursor;
  - SET then 1-8 store the one under the cursor, as SET then a number stores a panel memory.
- **The SOUND GROUP is the hex keypad.** Its two rows of eight are the digits 0-7 and 8-F, as a calculator's two
  rows. The ninth button of each row deletes: SOUND EXPLORER deletes the field (note or effect), EW EXPANSION the
  whole cell.
- **The RHYTHM GROUP is ProTracker's numeric keypad for samples.** Its 16 buttons choose samples 1-16, and 17-31
  with SHIFT. (PT's keypad chooses samples with a high/low toggle.)
- **TRANSPOSE transposes the note at the cursor,** by a semitone or, with SHIFT, an octave. **R1/R2 OCTAVE**
  transposes the whole track, or with SHIFT the whole pattern.
- **PART SELECT moves the cursor:** LEFT left, RIGHT 1 right, RIGHT 2 to the next channel (Tab).
- **The CONDUCTOR buttons assign patterns to positions,** as the conductor assigns sounds to parts:
  - LEFT and RIGHT 1 change the pattern at the current position;
  - RIGHT 2 inserts a position, and with SHIFT deletes one.
- **The MUTE pairs under the pattern are its channels' parts:**
  - columns 5-12 lie under the four channels' tracks on the LCD, two columns each;
  - the first of each pair: UP is "part on" (unmute), DOWN "part off" (mute);
  - the second: UP takes the cursor there, DOWN solos the channel.
- **The MUTE pairs under the side columns are the left panel's arrows:**
  - columns 1-4 lie under the left side column: POS, LENGTH, BPM and SPEED, each UP and DOWN;
  - columns 13-16 lie under the right side column: SAMPLE, VOLUME, FINETUNE and the edit skip.
  - With SHIFT, 13-16 change the sample's REPEAT and REPLEN, and the metronome's speed and channel.

  This is the KN7000's own way of editing a screen: the parameter above, its UP/DOWN below (as KN7FM does).
- **The TEMPO/PROGRAM wheel scrolls the pattern,** a row a detent. With SHIFT it changes the value under the cursor:
  the note a semitone, a hex digit by one. That makes it a data wheel. Its LED blinks while SHIFT gives it to the value.
- **TAP TEMPO sets the BPM** from the taps.
- **FADE IN and FADE OUT** raise and lower the current sample's volume.
- **AUTO PLAY CHORD ON/OFF is chord entry.** A chord held on the keybed goes into the cursor's channel and the
  ones after it, on the same row. AUTO PLAY CHORD MODE is ProTracker's MULTI mode (each note to the next channel),
  and so is the GLOBAL EFFECT button named MULTI.
- **SOUND ARRANGER ON/OFF is the metronome,** a beat under the music; SET puts it on the cursor's channel.
- **ONE TOUCH PLAY plays one row** and moves on (step play).
- **SOLO solos the cursor's channel.**
- **DISK and SD open the Disk Op.**, the list of modules.
- **CUSTOMIZE opens the options:** blank zeroes, the pattern's dots, sharps or flats, the visualizer.
- **OTHER PARTS/TG** switches the quadrascope and the spectrum analyzer.
- **HELP** shows a map of these controls.
- **DISPLAY HOLD is SHIFT.** Hold it and press the other control.
- **EXIT,** pressed briefly, is Esc: it leaves the edit mode, a page or a question. Held, it ends the program.
- **PAGE UP/DOWN** move the cursor 16 rows (PgUp/PgDn), or page through a list.
- **The soft keys** are ProTracker's on-screen buttons, by page:

  | page          | LCDL 1-5                                    | LCDR 1-5                                      |
  |---------------|---------------------------------------------|-----------------------------------------------|
  | main          | PLAY, PATTERN, STOP, EDIT, RECORD           | PREV MOD, NEXT MOD, DISK OP, SAVE, CLEAR      |
  | Disk Op.      | up, down, LOAD, (page up), (page down)      | SAVE, SAVE AS NEW, -, -, back                 |
  | options       | the options, one each                       | -, -, -, -, back                              |

  A question (SAVE? CLEAR?) takes START/STOP or SET as yes and EXIT as no, as ProTracker's ask boxes take Y and N.
- **The keybed:**
  - its notes are ProTracker's: C2 is C-1, three octaves;
  - the keys above and below fold into those octaves;
  - its velocity, with the option on, writes a Cxx volume command;
  - the sustain pedal moves the cursor on by the edit skip without a note (a rest, as in the KN7000's step record);
  - the modulation wheel sets the effect parameter under the cursor, 00-FF, while the cursor is on one.
- **Left alone:**
  - the volume sliders, CONTRAST, REVERB and the other effect buttons stay the firmware's;
  - PROGRAM MENUS, DEMO, MUSIC STYLE ARRANGER, SPLIT POINT, TECHNI-CHORD, CUSTOM PANEL and FAVORITES are spare.

### Risks to check first (checked in MAME, 2026-10-27)

**Result** (`tests/mame/pt_test.lua` `PT_RISK=1`, `tests/results/events-risk-before.txt` and `events-risk-after-10.txt` to `-12.txt`):
- **Before:** TRANSPOSE did nothing to the keybed's notes, but R1/R2 OCTAVE moved them an octave. PANEL MEMORY 1
  recalled a registration: that moved them too, and it reloaded the effects DSP over the mixer, so the music stopped.
- **The fix, in the platform (KN7APP.BIN `entry.c`):** the firmware's main task decodes the panel's frames from its
  receive ring. KN7APP now reads every switch frame there first, keeps the real state for the program
  (`PfHost::panel`), and clears the switches in the frame, so the firmware sees no change. CONTRAST is left to the
  firmware. This is how the Pinball port already takes the TEMPO/PROGRAM wheel's frames.
  - It reads the ring on each converter call, and again before every return from one, waiting for a frame's second
    byte (the main task decodes the ring as soon as a call returns).
  - It also reads the ring from the program's own task at every replayer tick (ABI 11 `panelScan`).
- **After:** in three runs, all 66 switch frames were taken before the firmware decoded them. No control reached the
  firmware, and the music played on. Two earlier versions each let a press through in MAME: one returned on a frame's
  first byte, and one returned from its rest without a last look at the ring. Program 893 only, as for the wheel.

What the checks were:

The firmware keeps decoding the panel while the converter runs, though its user interface is parked on our text.
A button whose firmware action still happens would interfere. The Pinball port used START/STOP, VARIATION, the soft
keys, PAGE, OTHER PARTS and HELP with no trouble. To check, in MAME and then on the unit:
- **TRANSPOSE and R1/R2 OCTAVE:** if the firmware still transposes, the notes the keybed sends to MIDI OUT (which
  the port reads) would move. Check the note numbers before and after. If they move, either undo the shift (read the
  firmware's transpose) or map these functions elsewhere.
- **SOUND GROUP and RHYTHM GROUP:** they change the keybed's sound and the rhythm. The keybed's sound is muted while
  ProTracker runs. Check that no rhythm starts and that the right parts come back at the end.
- **The PANEL MEMORY ring:** recalling a panel memory changes the whole panel's settings. Check that SET and 1-8 do
  nothing of the firmware's while the converter runs.

## 2. The screen

The LCD is 640 x 240, and ProTracker's screen is 320 x 240 in the middle (13 pattern rows). The 160 columns either
side were labels only. They become:
- **Soft-key tags:** a word in ProTracker's own small font (8 x 5) at the outer edge, beside each soft key, and no
  more. About 50 columns a side.
- **The left column's middle, the song:** ProTracker's position editor (Pos Ed.), always shown. Each line is the
  position and its pattern, the current one marked and the playing one followed; about 26 lines.
- **The right column's middle, the samples:** all 31 samples, each line its number, name and length, the current one
  marked. ProTracker shows one sample at a time; here they can be seen while choosing with the RHYTHM GROUP.
- **The bottom of each column, the MUTE columns' parameters:** each 40-pixel column sits right above its MUTE UP/DOWN
  pair, with the parameter's name and its value (POS 012, LEN 040, BPM 125, SPD 06 | SMP 01, VOL 40, FTU +0, SKP 1).
- **HELP:** the whole LCD, a page at a time, with a drawing of the panel's sections and what each control does.
- **Questions and messages:** ProTracker's status line, as now. A question also blinks the wheel's LED.

The sides are drawn into buffers of their own and written to the LCD the way the screen is: only what changed.

## 3. The changes to the code

| #   | change                                                                                                                                                             | where                                          |
|-----|--------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------|
| 1   | a panel layer: every switch by name, press/release edges, auto-repeat (300 ms, then 60 ms), SHIFT, long press                                                      | `src/kn7/panel.c`                              |
| 2   | the control map of section 1, one table: control, page, with or without SHIFT, action                                                                              | `src/kn7/controls.c`                           |
| 3   | the cursor in all 24 columns, as ProTracker's (`cursor.pos`, `cursor.channel`); hex entry per column                                                               | `src/kn7/edit.c`                               |
| 4   | delete field and cell; insert and delete rows (track, pattern); transpose (note, track, pattern)                                                                   | `src/kn7/edit.c`                               |
| 5   | track and pattern buffers: cut, copy, paste; undo of the last change (a copy of the pattern before it)                                                             | `src/kn7/edit.c`                               |
| 6   | effect macros (8, PANEL MEMORY); the edit skip; MULTI; chord entry; record mode while playing; step play                                                           | `src/kn7/edit.c`                               |
| 7   | the song: position up/down, the pattern at a position, insert and delete positions, length; BPM, speed                                                             | `src/kn7/song.c`                               |
| 8   | the sample: select, volume, finetune, repeat, replen (with Paula's loops updated: `updatePaulaLoops`)                                                              | `src/kn7/song.c`                               |
| 9   | saving: pt2-clone's module saver into a buffer, `PfHost::save`; SAVE (the module's own file, asked first), SAVE AS NEW (`PTNEW01.MOD` and on, added to `MODS.TXT`) | `src/pt2/pt2_module_saver.c`, `src/kn7/disk.c` |
| 10  | the Disk Op.: the modules of `MODS.TXT` in a list on ProTracker's top area, the wheel to choose                                                                    | `src/kn7/disk.c`                               |
| 11  | CLEAR: an empty song, asked first                                                                                                                                  | `src/kn7/song.c`                               |
| 12  | the sides: soft-key tags, the position list, the sample list, the MUTE columns' parameters                                                                         | `src/kn7/ui.c`                                 |
| 13  | HELP pages                                                                                                                                                         | `src/kn7/help.c`                               |
| 14  | the keybed: note-on events in order (a chord or a quick repeat between two polls is not lost), velocity, the modulation wheel, pitch bend (ABI 11)                 | `src/app/notes.c`, `pfhost.h`                  |
| 15  | TAP TEMPO, the metronome, the options page                                                                                                                         | `src/kn7/controls.c`                           |
| 16  | the wheel's LED for SHIFT and questions (PfHost::wheelLed, ABI 9)                                                                                                  | `src/kn7/controls.c`                           |
| 17  | a MAME session that edits a pattern with these controls and checks the result in memory and in the saved file                                                      | `tests/mame/`                                  |
| 18  | the user manual, in PDF                                                                                                                                            | `manual/`                                      |

What stays as it is: the replayer, the loader, Paula on the DSP, the sample bank, the clock.

## 4. How it is checked

- **Every edit operation** in a scripted MAME session (`pt_test.lua`, a new `PT_EDIT=2`):
  - the controls are pressed as a player would press them;
  - the pattern is read back from PT.BIN's memory after each and compared with what ProTracker's operation gives:
    the notes, sample numbers, effects and positions expected;
  - snapshots of the screen.
- **Saving:**
  - the saved file is read from the card image after the session (`fatimg.py get`);
  - it is loaded by libopenmpt (ffmpeg) and by the port's PC build;
  - an unedited module saves back byte for byte, up to what pt2-clone's saver normalises;
  - an edited one has the edits.
- **The firmware leaving the buttons alone:** before and after each mapped button, the firmware's transpose, tempo
  and the notes' numbers (section 1, "Risks").
- **On the unit:** the owner's KN7000, after MAME. Above all: whether each control does only what the port asks
  (the firmware's reaction to buttons is what MAME can model least), and the feel of the auto-repeat and the wheel.

## 5. Order of work

**Status (2026-10-27): steps 1-6 are done and checked in MAME** (README.md, "What was checked"):
- the editor session passes 24 of 24 checks, and its saved module has exactly its edits;
- the firmware leaves the panel alone;
- the manual is `manual/protracker-kn7000-manual.pdf`.

Left for the instrument itself: the feel of the auto-repeat and the wheel, and whether every control does only what
the port asks there. In MAME the platform takes 66 of 66 switch frames first (three runs); how many it takes on the unit is not known
yet. Not done:
- the pitch-bend wheel;
- NEXT BANK, which only switches one bank of 8 macros;
- pattern-wide transpose (the track is the widest).

1. The panel layer and the control map (1, 2), with the cursor and hex entry (3): editing is possible.
2. Delete, insert, transpose, buffers, undo, macros (4-6): editing is effective.
3. The song and the sample (7, 8, 11); saving and the Disk Op. (9, 10): editing is kept.
4. The sides and HELP (12, 13).
5. The keybed's events, chords, velocity, wheels (14); TAP TEMPO, metronome, options, LED (15, 16).
6. The MAME session (17); the manual (18); the docs site; the card.
