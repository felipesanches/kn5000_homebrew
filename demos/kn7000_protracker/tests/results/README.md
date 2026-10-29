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
| `events-list-stock.txt`                  | 2026-10-28, `PT_EDIT=2`: the folder read off the card (6 modules, sorted), PTNEW01.MOD listed when saved; 25 checks                   |
| `events-list-added-1.txt`                | 2026-10-28, `PT_EDIT=2` with PT_ADD (below): the list right; FILL IN 2 and PAD 2 failed (89 switch frames, one extra; not seen again) |
| `events-list-added-trace-2.txt` to `-5`  | the same with `PT_TRACE=1`: four runs, 25 checks each, every tap one press and one release frame                                      |
| `events-list-play5.txt`                  | `PT_MODULES=5 PT_EACH=14` with PT_ADD: INTRO.MOD, MOD.JAZ, MOD2.MOD, SPACE_~1.MOD (a long name's alias) load and play                 |
| `events-pos-direction.txt`               | 2026-10-28, `PT_EDIT=2`: MUTE DOWN 1 / 13 move POS / SMP down their lists, MUTE UP 1 / 13 back up; 29 checks                          |
| `level-gain-128-vs-16.txt`               | 2026-10-29, `level_compare.py`: INTRO.MOD at DSP_GAIN 128 and 16, RUN + 20..45 s: -18.1 dB RMS and peak                               |
| `events-gain-128.txt`, `-16.txt`         | `PT_MODULES=1 PT_EACH=30`, built with `DSP_GAIN=128` and with 16: those two sessions                                                  |
| `level-gain-16-vs-2.txt`                 | 2026-10-29: the same at DSP_GAIN 16 and 2, RUN + 25..45 s: -18.3 dB RMS, -18.0 dB peak                                                |
| `events-noplay-gain-16.txt`, `-2.txt`    | `PT_MODULES=2 PT_EACH=25` at 16 and 2: each module loads, does not play by itself, plays on LCDL 1                                    |
| `events-noplay-edit.txt`                 | 2026-10-29, `PT_EDIT=2` at DSP_GAIN 2: "loaded, not played until PLAY" and the rest; 30 checks                                        |
| `native-vs-pt-mame-before.txt`           | 2026-10-29, `PT_NATIVE=1`, the overlay of 2026-10-27: ProTracker 25.9 dB below the instrument; unit: 7.7 dB                           |
| `events-native-before.txt`               | that session                                                                                                                          |
| `native-vs-pt-mame-after.txt`            | the overlay with TG_TO_DSP 1/4 and unit 9 always mixed (kn7000_mame): -7.8 dB; CHORUS on +4.6 dB                                      |
| `native-vs-pt-mame-reverb-off.txt`       | the same with `PT_REVERB_OFF=1`: ProTracker heard (unit 9 alone), 6.3 dB below REVERB on                                              |
| `events-native-after.txt`                | that session (emulator md5 c7715771)                                                                                                  |
| `events-native-reverb-off.txt`           | that session, REVERB off (the same emulator)                                                                                          |

The PC renders are `tests/host/render <module> <out.wav> 40` (`make render`). Session pt-mame3 predates the
lateness counters; pt-mame3 and pt-mame4 predate the status line in the log.

PT_ADD in the 2026-10-28 sessions: `PT_ADD="$PF_DATA/MOD2.MOD=MOD.JAZ $PF_DATA/MOD2.MOD=Space_Debris.mod NOTES.TXT=NOTES.TXT"`
(NOTES.TXT a 7-byte text file) and `PT_LIST="INTRO.MOD MOD.JAZ MOD2.MOD SPACE_~1.MOD TABLE1.MOD TABLE2.MOD TABLE3.MOD
TABLE4.MOD"`. All of them ran KN7APP.BIN d44227d0 and PT.BIN ef822dd5 (each session's `versions.txt`).
