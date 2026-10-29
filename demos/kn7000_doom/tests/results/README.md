# Results

| file                                | what                                                                                                                | made by                                    |
|-------------------------------------|---------------------------------------------------------------------------------------------------------------------|--------------------------------------------|
| `zone-minimum-host-2026-10-29.txt`  | the smallest zone each shareware demo and E1 map runs in (host build, 32-bit): 635-953 KB, E1M6 the most            | `tests/host/sweep.sh`                      |
| `events-first-run-2026-10-29.txt`   | the first MAME run: startup, title screen, demo 1 (E1M5) for 80 s, EXIT, the borrowed RAM back intact               | `tests/mame/run_doom.sh` + `doom_test.lua` |
| `versions-first-run-2026-10-29.txt` | the emulator, binaries, WAD, floppy and card of that run                                                            | `run_doom.sh`                              |
| `startup-log-2026-10-29.png`        | that run: the startup log on the LCD                                                                                | its snapshot 2                             |
| `demo1-e1m5-2026-10-29.png`         | that run: demo 1 on E1M5                                                                                            | its snapshot 12                            |
| `events-keyscan-2026-10-29.txt`     | DOOM_KEYSCAN=1: which keys reach KN7APP's notes[] at the title screen (C3 does not: below the split point)          | `run_doom.sh` + `doom_test.lua`            |
| `events-play-2026-10-29.txt`        | DOOM_PLAY=1: the menu, New Game, E1M1 (8.8 s to load), forward, turn, fire, the fist, the automap: 9 checks, all ok | `run_doom.sh` + `doom_test.lua`            |
| `versions-play-2026-10-29.txt`      | the emulator, binaries, WAD, floppy and card of that run                                                            | `run_doom.sh`                              |
| `title-labels-2026-10-29.png`       | that run: the title screen with the soft keys' labels                                                               | its snapshot 0                             |
| `e1m1-forward-2026-10-29.png`       | that run: E1M1 after G#4 held 2 s                                                                                   | its snapshot 3                             |
| `e1m1-automap-2026-10-29.png`       | that run: the automap (E5)                                                                                          | its snapshot 5                             |
