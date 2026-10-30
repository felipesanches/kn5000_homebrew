# Keyfall: a piano game for the KN7000

Notes fall down the Technics SX-KN7000's LCD onto a keyboard drawn at its bottom; the player strikes them on the
instrument's keybed as they reach it, and each key is judged by its timing, with a score, a streak and a results
screen. The songs are Standard MIDI Files on the SD card: the player chooses a part (a track, or both hands), the other
parts are the accompaniment, which the instrument plays as it plays what a sequencer sends it over MIDI. It is run by the
KN7000 program loader (`KN7LOAD.CNV`, DISK MENU > STYLE CONVERT) from the program menu's folder `KEYFALL\`, in its Games
folder, as "Keyfall (piano game)".

> **Status (2026-10-30): plays in MAME (program 893), every check passing ([What was checked](#what-was-checked)); not
> yet tried on the instrument.**

## Playing it

DISK MENU, STYLE CONVERT, `KN7LOAD.CNV`, OK; then the program menu's Games folder, "Keyfall (piano game)", RUN. The
folder's MIDI files are listed; the TEMPO/PROGRAM wheel (or UP/DOWN) chooses one, and the soft keys beside the LCD set the
game:

| control                  | the songs                                                                      | a game         | the results |
|--------------------------|--------------------------------------------------------------------------------|----------------|-------------|
| TEMPO/PROGRAM wheel      | the song                                                                       |                |             |
| LCD left 1: PART         | the part played: each track, and "Both hands" when tracks are named for each   |                | SONGS       |
| LCD left 2: TEMPO        | 100% to 50% of the song's tempo                                                |                |             |
| LCD left 3: ACCOMP       | the accompaniment (the other parts) on or off                                  |                |             |
| LCD left 4: MODE         | PLAY (judged), WAIT (the song waits at each note for its key), DEMO (it plays) |                |             |
| LCD left 5: SPEED        | how fast the notes fall (70 to 180 lines a second)                             |                |             |
| LCD right 1: PLAY        | starts the game                                                                |                | AGAIN       |
| LCD right 2, 3: UP, DOWN | the song                                                                       |                |             |
| LCD right 5: LATENCY     | ms taken off each key's time before it is judged (-40 to 100)                  |                |             |
| EXIT                     |                                                                                | stops the song | SONGS       |
| EXIT held 1.5 s          | leaves Keyfall                                                                 | leaves Keyfall |             |

Everything else on the panel does what it always does: the keybed plays the sound chosen for it (choose one while the
song list is up), and the game only reads its keys. A note's lower edge reaches the keyboard at its time; the key it
belongs to lights up just before. A key struck within 50 ms of its note's time is PERFECT, within 100 ms GOOD, within
150 ms OK; a note not struck by then is missed, and a key that strikes no note is a wrong key. A note struck scores 100,
70 or 40 times the multiplier (1 plus a tenth of the streak, at most 4). The results give the grade (S from 95%, A 85%,
B 70%, C 50%), the counts, the longest streak and how early or late the keys came on average; the best score of each
song, part and tempo is kept in `SCORES.TXT` in the folder.

The keys the game reads are those the keybed sends to MIDI OUT on channels 1-3 (RIGHT 1, RIGHT 2, LEFT; and those MIDI
IN 1 receives on them, from a keyboard plugged in there). While AUTO PLAY CHORD is on, the keys left of the split point
are the arranger's chord keys and send nothing: a right-hand part plays as it is; for a left-hand part, or both hands,
turn AUTO PLAY CHORD off first (in MAME: every key read with it off, none below C4 with it on:
`tests/results/events-keyscan-2026-10-30.txt`).

## The songs

Three public-domain pieces come with it, written out for this game (`songs/*.txt`, made into `songs/*.MID` by
`tools/songs/mksong.py`, whose README describes the format):

| file          | piece                                                                  | parts                            |
|---------------|------------------------------------------------------------------------|----------------------------------|
| `TWINKLE.MID` | Twinkle, Twinkle, Little Star (the French "Ah ! vous dirai-je, maman") | right hand, left hand (chords)   |
| `ODEJOY.MID`  | Ode to Joy (Beethoven, Symphony No. 9), in G major                     | right hand, bass, strings, drums |
| `MINUETG.MID` | Minuet in G major, BWV Anh. 114 (Petzold), its first part              | right hand, left hand            |

**Adding songs.** Copy any Standard MIDI File (format 0 or 1) into the card's `KEYFALL\` folder with an 8.3 name ending
in `.MID`. Every track's notes on one channel are a part, named by the track's name; a track named "Right hand" (or
"RH", "Melody") is offered first, and a "Left hand" ("LH") beside it makes "Both hands" a choice. System exclusive
messages in the file are left out (a GM reset would change the instrument's settings).

## How it works

Two programs, as the PDF viewer's, Doom's and ProTracker's (`src/common/pfhost.h` between them):

- **`KN7APP.BIN`** (`src/app/`, the PDF viewer's at ABI 15): the loader runs it on each converter call. It starts a task
  for the game and serves its file reads; it takes from the firmware only the switches the game names (the ten soft
  keys and EXIT) and the wheel, and leaves it everything else; it keeps the firmware's drawing off. ABI 15 adds the
  keybed's presses and releases with the millisecond each was found (`keyEvents`, from the MIDI rings as Another World
  reads them) and MIDI messages into MIDI IN 1's ring (`midiIn`); at the end it sends Reset All Controllers and All
  Notes Off on every channel the game used.
- **`KEYFALL.BIN`** (`src/game/`): the song reading (`smf.c`), the chart and the accompaniment (`song.c`), the judging
  (`judge.c`), the game (`play.c`), the screens (`menu.c`, `draw.c`), the best scores (`scores.c`). Its image and data
  are in the program area (GAME_BASE, 45 KB of 204 KB), the songs in ARENA2.

**The accompaniment** goes into MIDI IN 1's receive ring, as if it came from the MIDI IN socket, as its time comes:
the instrument's MIDI parts PART 4-16 receive channels 4-16 (the owner's manual, "Part Setting"), so a song's channels 1-3
(the keybed's own parts) move to channels it leaves free, and channel 10 stays the drums. Each channel starts with a GM2
bank select (MSB 121, drums 120), its program, volume 100, pan centre and expression 127.

**The screen** is 8 bits a pixel through the LCD controller's look-up table (the PDF viewer's set-up), the soft keys'
labels in its ink layer. Display memory is slow, so only what changes is drawn: the lanes are cut into segments at every
key's edge, and each frame only the rows a note's edges crossed in its lane, and a beat line's old and new row, are drawn
again (`play.c`).

## The manual

`manual/keyfall-kn7000-manual.pdf` (6 pages, in the style of the instrument's Operating Instructions, as ProTracker's)
is printed from `manual/keyfall-kn7000-manual.html` by `manual/build.sh` (headless Chromium); its pictures are the LCD
cut out of the MAME snapshots by `manual/mkimg.py`.

## What was checked

In MAME (program 893), each through the program menu as on the instrument (`tests/mame/`, the runs in
`tests/results/README.md`): a song played by the script striking MAME's keybed at the chart's times, its results'
counts checked against what was struck (Twinkle 42 of 42 PERFECT; notes left out MISSED, stray keys WRONG KEY); WAIT
and DEMO; the accompaniment alone heard (-36.8 dBFS against silence) and never read as keys; every key from C2 to C7
read with AUTO PLAY CHORD off, only C4 and up with it on. With the 10 ms converter rest while a song plays, the keys
reached the game 26-48 ms after the script struck them and the firmware took the accompaniment 0.3-17 ms after the game
put it in (with 100 ms: 23-146 and 1-178). On the PC: `make check` (`tests/host/`). Not yet tried on the instrument.

## Building

    make              # build/KN7APP.BIN and build/KEYFALL.BIN
    make songs        # songs/*.MID from songs/*.txt
    make card-dir     # build/card/KEYFALL/: the programs, the songs, MANIFEST.TXT
    make check        # the song reading and judging on the PC (tests/host/)
    tests/mame/run_keyfall.sh ~/compartilhado/tmp/keyfall-mame    # a session in MAME (tests/mame/)

It needs the mn10300-elf toolchain the Pinball port builds (GCC patched to make no `(d32,An)` operand: `check_d32.py`
runs after each link) and the KN7000 loader's tools (technics-roms-disasm `kn7000/tools/cnv_romdump`).

## Files

| path           | what                                                                             |
|----------------|----------------------------------------------------------------------------------|
| `src/game/`    | the game                                                                         |
| `src/app/`     | KN7APP.BIN, the platform (the PDF viewer's, with `keyEvents` and `midiIn`)       |
| `src/common/`  | the interface between the two (`pfhost.h`) and the KN7000's memory map (`kn7.h`) |
| `songs/`       | the songs, as text and as MIDI files                                             |
| `tools/songs/` | `mksong.py`: text to MIDI file                                                   |
| `tools/build/` | `mkgame.py` (KEYFALL.BIN's header), Doom's `check_d32.py`, `font2c.py`           |
| `tests/host/`  | the song reading and judging on the PC                                           |
| `tests/mame/`  | the game in MAME: `keyfall_test.lua`                                             |
