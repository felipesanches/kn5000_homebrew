# ProTracker in MAME

| file                | question it answers                                                             | run                                                                          |
|---------------------|---------------------------------------------------------------------------------|------------------------------------------------------------------------------|
| `run_pt.sh`         | does ProTracker start from the program menu, load, send and play its modules?   | `tests/mame/run_pt.sh ~/compartilhado/tmp/pt/run` (variables: `pt_test.lua`) |
| `pt_test.lua`       | the session's script: the menu, RUN, the modules, the controls; `events.txt`    | (run by `run_pt.sh`)                                                         |
| `sound_report.py`   | is the session's sound heard, second by second?                                 | `sound_report.py OUT/sound.wav OUT/events.txt`                               |
| `compare_render.py` | does a sound play a module as a reference does (libopenmpt, or the PC render)?  | `compare_render.py [--waveform] SOUND.WAV MODULE-or-REF.WAV [SECONDS]`       |
| `mod_timeline.py`   | where should the song be at each second (ProTracker's sequencing rules)?        | `mod_timeline.py MODULE [SECONDS]`                                           |
| `offsets.c`         | where are the fields `pt_test.lua` reads in PT.BIN's `module_t` and `editor_t`? | its header says                                                              |

The variables (`run_pt.sh` and `pt_test.lua`):
- `MODS="A.MOD B.MOD"` (the Makefile's): the modules in `PROTRACK\`, from `PF_DATA`. The program lists the folder's
  `*.MOD` and `MOD.*` files in order of name, so the first by name is the one that loads first.
- `PT_ADD="FILE=NAME ..."`: more files copied into `PROTRACK\` (a long NAME gets a VFAT name and an 8.3 alias).
- `PT_LIST`: the module list `PT_EDIT=2` checks it read (default: the six of Pinball Fantasies).
- `PT_MODULES`, `PT_EACH`: modules stepped through with LCDR 2, seconds each. `PT_EDIT=1`, `PT_EDIT=2` (the editor's
  checks), `PT_RISK=1` (does the firmware act on the panel), `PT_TRACE=1` (with `PT_EDIT=2`: each tap's switch frames,
  `time=frames:byte`, as KN7APP took them). `KEEP_SD=1` keeps the card as `sd_after.img`.

What the log's lines mean:
- **`dsp state`:** 4 the mixer hooked with no bank, 1 a bank being sent, 2 ready; `bank` is how long the last bank
  took.
- **`flushes`:** the voice commands sent.
- **`file`:** the module's file (disk.c's list).
- **`late`:** the replayer's ticks that ran less than 1, 2, 5, 10, 20 and 50 ms after they were due, and later.

Results: `tests/results/`.
