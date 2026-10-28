# Results of 2026-10-27 and 28 (MAME, program 893)

Each MAME session was `tests/mame/run_pt.sh ~/compartilhado/tmp/<session>` with the variables below. The sessions'
own folders (the sound, the snapshots) are scratch; what the README quotes is here.

| file                                     | from                                                                                                                                  |
|------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------|
| `events-pt-mame3.txt`                    | `PT_MODULES=6 PT_EACH=15`: the modules one after another (LCDR 2), each one's samples to the DSP                                      |
| `sound-pt-mame3.txt`                     | `tests/mame/sound_report.py` on pt-mame3's sound: each second's level                                                                 |
| `events-pt-mame4.txt`                    | `MODS="TABLE1.MOD MOD2.MOD" PT_MODULES=1 PT_EACH=50`: TABLE1.MOD alone                                                                |
| `events-pt-mame5.txt`                    | `MODS="INTRO.MOD MOD2.MOD" PT_MODULES=2 PT_EACH=45`: INTRO.MOD, then MOD2.MOD                                                         |
| `events-pt-mame6.txt`                    | `MODS="MOD2.MOD INTRO.MOD" PT_MODULES=1 PT_EACH=30`: MOD2.MOD, with the ticks' lateness                                               |
| `compare-2026-10-27.txt`                 | `tests/mame/compare_render.py`: the PC render against libopenmpt, and pt-mame4-6 against the PC render                                |
| `screen-2026-10-27.png`                  | a snapshot of the first session (pt-mame1), INTRO.MOD playing                                                                         |
| `events-edit-2026-10-27.txt`             | `MODS="MOD2.MOD INTRO.MOD" PT_EDIT=2 KEEP_SD=1`: the editor, 24 checks                                                                |
| `saved-PTNEW01-vs-MOD2.txt`              | `check_saved.py`: the module that session saved against the one it edited                                                             |
| `editor-2026-10-27.png`                  | the edit mode with the side columns                                                                                                   |
| `events-risk-before.txt`                 | `PT_RISK=1` before the platform kept the panel from the firmware                                                                      |
| `events-risk-after-10.txt`, `-11`, `-12` | `PT_RISK=1` after: three runs, 66 switch frames, none reached the firmware                                                            |
| `events-pt-mame8.txt`                    | `MODS="MOD2.MOD INTRO.MOD" PT_EDIT=1`: the spectrum analyzer, a mute button, the edit mode, three keys                                |
| `events-pt-mame11.txt`                   | `MODS="MOD2.MOD INTRO.MOD" PT_EDIT=1` with the rest of the controls, after the clock fixes                                            |
| `edit-2026-10-27.png`                    | pt-mame8: the edit mode after C3, E3, G3 (rows 55-57 of channel 1)                                                                    |
| `spectrum-2026-10-27.png`                | pt-mame8: the pattern playing, the spectrum analyzer                                                                                  |
| `events-list-stock.txt`                  | 2026-10-28, `PT_EDIT=2`: the folder read off the card (6 modules, sorted), PTNEW01.MOD listed when saved; 26 checks                   |
| `events-list-added-1.txt`                | 2026-10-28, `PT_EDIT=2` with PT_ADD (below): the list right; FILL IN 2 and PAD 2 failed (89 switch frames, one extra; not seen again) |
| `events-list-added-trace-2.txt` to `-5`  | the same with `PT_TRACE=1`: four runs, 26 checks each, every tap one press and one release frame                                      |
| `events-list-play5.txt`                  | `PT_MODULES=5 PT_EACH=14` with PT_ADD: INTRO.MOD, MOD.JAZ, MOD2.MOD, SPACE_~1.MOD (a long name's alias) load and play                 |
| `events-pos-direction.txt`               | 2026-10-28, `PT_EDIT=2`: MUTE DOWN 1 moves POS down the list (0 -> 1), MUTE UP 1 back up it; 28 checks                                |

The PC renders are `tests/host/render <module> <out.wav> 40` (`make render`). Session pt-mame3 predates the
lateness counters; pt-mame3 and pt-mame4 predate the status line in the log.

PT_ADD in the 2026-10-28 sessions: `PT_ADD="$PF_DATA/MOD2.MOD=MOD.JAZ $PF_DATA/MOD2.MOD=Space_Debris.mod NOTES.TXT=NOTES.TXT"`
(NOTES.TXT a 7-byte text file) and `PT_LIST="INTRO.MOD MOD.JAZ MOD2.MOD SPACE_~1.MOD TABLE1.MOD TABLE2.MOD TABLE3.MOD
TABLE4.MOD"`. All of them ran KN7APP.BIN d44227d0 and PT.BIN ef822dd5 (each session's `versions.txt`).
