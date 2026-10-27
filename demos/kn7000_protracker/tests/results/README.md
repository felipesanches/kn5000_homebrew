# Results of 2026-10-27 (MAME, program 893)

Each MAME session was `tests/mame/run_pt.sh ~/compartilhado/tmp/<session>` with the variables below. The sessions'
own folders (the sound, the snapshots) are scratch; what the README quotes is here.

| file                      | from                                                                                                   |
|---------------------------|--------------------------------------------------------------------------------------------------------|
| `events-pt-mame3.txt`     | `PT_MODULES=6 PT_EACH=15`: the modules one after another (LCDR 2), each one's samples to the DSP       |
| `sound-pt-mame3.txt`      | `tests/mame/sound_report.py` on pt-mame3's sound: each second's level                                  |
| `events-pt-mame4.txt`     | `MODS="TABLE1.MOD MOD2.MOD" PT_MODULES=1 PT_EACH=50`: TABLE1.MOD alone                                 |
| `events-pt-mame5.txt`     | `MODS="INTRO.MOD MOD2.MOD" PT_MODULES=2 PT_EACH=45`: INTRO.MOD, then MOD2.MOD                          |
| `events-pt-mame6.txt`     | `MODS="MOD2.MOD INTRO.MOD" PT_MODULES=1 PT_EACH=30`: MOD2.MOD, with the ticks' lateness                |
| `compare-2026-10-27.txt`  | `tests/mame/compare_render.py`: the PC render against libopenmpt, and pt-mame4-6 against the PC render |
| `screen-2026-10-27.png`   | a snapshot of the first session (pt-mame1), INTRO.MOD playing                                          |
| `events-pt-mame8.txt`     | `MODS="MOD2.MOD INTRO.MOD" PT_EDIT=1`: the spectrum analyzer, a mute button, the edit mode, three keys |
| `events-pt-mame11.txt`    | `MODS="MOD2.MOD INTRO.MOD" PT_EDIT=1` with the rest of the controls, after the clock fixes             |
| `edit-2026-10-27.png`     | pt-mame8: the edit mode after C3, E3, G3 (rows 55-57 of channel 1)                                     |
| `spectrum-2026-10-27.png` | pt-mame8: the pattern playing, the spectrum analyzer                                                   |

The PC renders are `tests/host/render <module> <out.wav> 40` (`make render`). Session pt-mame3 predates the
lateness counters; pt-mame3 and pt-mame4 predate the status line in the log.
