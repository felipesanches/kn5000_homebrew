# Keyfall in MAME

`run_keyfall.sh OUT [LUA]` runs the game in one MAME session, started as on the instrument: DISK MENU, STYLE CONVERT,
the owner's loader floppy, the program menu on a copy of the owner's SD card, the "Keyfall (piano game)" row, RUN. It
builds the card's `KEYFALL\` folder first (`make card-dir`), and runs the published overlay emulator
(`~/compartilhado/kn7000-emulator`, `-bios v13`: program 893, the owner's).

    tests/mame/run_keyfall.sh ~/compartilhado/tmp/keyfall-mame
    KF_SONG=ODEJOY.MID KF_NOPRESS=1 tests/mame/run_keyfall.sh ~/compartilhado/tmp/keyfall-mame-acc

OUT gets `events.txt` (what the script did and checked, with the emulated time), `snap/` (the snapshots), `run.log`
(MAME's output), `versions.txt` (the emulator's, the programs', the floppy's, the card's and the songs' checksums),
`syms.txt` (the programs' symbols, through which the script reads the game's state) and `sound.wav`. OUT is scratch
(`~/compartilhado/tmp/...`); the card and floppy copies in it are deleted at the end (`KEEP_SD=1` keeps the card).

`keyfall_test.lua` (its header lists every step) chooses a song and its settings with the soft keys, starts it, reads
the chart from the game's memory and strikes each note on MAME's keybed when the game's clock reaches its time, then
CHECKs the results screen's counts against what it struck, that every key reached the game, and that every
accompaniment message went into MIDI IN 1's ring and the firmware took it. It measures meanwhile how late after its time
each struck note was judged (the input path's latency, as the game sees it) and, by write taps on the ring's header,
how long the firmware takes to read each accompaniment message the game put in.

| variable     | default                | what                                                                 |
|--------------|------------------------|----------------------------------------------------------------------|
| `KF_SONG`    | `TWINKLE.MID`          | the song, by its 8.3 name                                            |
| `KF_PART`    | 0                      | presses of PART (L1): the choices after the first                    |
| `KF_TEMPO`   | 100                    | the tempo, 100 to 50 in tens                                         |
| `KF_ACCOMP`  | 1                      | 0: the accompaniment off                                             |
| `KF_MODE`    | `PLAY`                 | `PLAY`, `WAIT` or `DEMO`                                             |
| `KF_LEAD`    | 0                      | ms before its time each note is struck                               |
| `KF_SKIP`    | 0                      | notes left out (the 6th, 16th, ...): CHECKed as missed               |
| `KF_WRONG`   | 0                      | keys struck that strike no note: CHECKed as wrong keys               |
| `KF_NOPRESS` | 0                      | 1: no key struck at all (the accompaniment alone; every note missed) |
| `KF_SNAP`    | 8                      | seconds between snapshots while the song plays                       |
| `KF_NAME`    | `Keyfall (piano game)` | the program menu's row                                               |
| `BIOS`       | `v13`                  | the firmware: `v13` program 893 (the owner's), `v16` 941             |
| `TIMEOUT`    | 3600                   | seconds of host time before MAME is stopped                          |
