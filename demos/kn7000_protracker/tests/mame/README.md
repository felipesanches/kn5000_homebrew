# ProTracker in MAME

| file                | question it answers                                                             | run                                                                          |
|---------------------|---------------------------------------------------------------------------------|------------------------------------------------------------------------------|
| `run_pt.sh`         | does ProTracker start from the program menu, load, send and play its modules?   | `tests/mame/run_pt.sh ~/compartilhado/tmp/pt/run` (variables: `pt_test.lua`) |
| `pt_test.lua`       | the session's script: the menu, RUN, the modules, the controls; `events.txt`    | (run by `run_pt.sh`)                                                         |
| `sound_report.py`   | is the session's sound heard, second by second?                                 | `sound_report.py OUT/sound.wav OUT/events.txt`                               |
| `compare_render.py` | does a sound play a module as a reference does (libopenmpt, or the PC render)?  | `compare_render.py [--waveform] SOUND.WAV MODULE-or-REF.WAV [SECONDS]`       |
| `mod_timeline.py`   | where should the song be at each second (ProTracker's sequencing rules)?        | `mod_timeline.py MODULE [SECONDS]`                                           |
| `offsets.c`         | where are the fields `pt_test.lua` reads in PT.BIN's `module_t` and `editor_t`? | its header says                                                              |

What the log's lines mean:
- **`dsp state`:** 4 the mixer hooked with no bank, 1 a bank being sent, 2 ready; `bank` is how long the last bank
  took.
- **`flushes`:** the voice commands sent.
- **`late`:** the replayer's ticks that ran less than 1, 2, 5, 10, 20 and 50 ms after they were due, and later.

Results: `tests/results/`.
