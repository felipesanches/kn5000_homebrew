# The game code on the PC

`make host` builds `build/host/doom`: Chocolate Doom's files as the KN7000 build has them (`src/choco/`, with
`CHOCO_KN7`), the KN7000 side's platform-free files (`src/kn7/net_none.c`, `left_out.c`, `i_sound_kn7.c`) and
`host.c` in place of the LCD, the panel and the card. It is a 32-bit program (`-m32`), so the game's structures are
the KN7000's sizes. No window, no sound; the PC's files.

| what           | question it answers                                                   | how                                                                                                                 |
|----------------|-----------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------|
| `host.c`       | does the game run, and in how small a zone                            | `DOOM_ZONE_KB=1100 DOOM_ZONE_PIECES=5 build/host/doom -iwad DOOM1.WAD -nosound -timedemo demo2`                     |
| `sweep.sh`     | the smallest zone (KB, to 16) each demo and each E1 map runs in       | `sweep.sh DOOM1.WAD ~/compartilhado/tmp/doom-sweep demo1 demo2 demo3 map1 ... map9`                                 |
| `zone_tags.py` | what the zone holds at its fullest, by tag (static, level, cache ...) | `DOOM_ZONE_DUMP=dump.txt DOOM_FRAMES=600 build/host/doom -iwad DOOM1.WAD -nosound -warp 1 6; zone_tags.py dump.txt` |

`host.c`'s settings: `DOOM_ZONE_KB` (default 16384), `DOOM_ZONE_PIECES` (the zone in that many pieces with 4 KB gaps,
as the KN7000's is in several regions), `DOOM_FRAMES` (end after that many frames, printing the zone's high-water mark
of blocks that cannot be purged and malloc's use), `DOOM_PPM` (a folder: every `DOOM_PPM_EVERY`-th frame as a PPM) and
`DOOM_ZONE_DUMP`.

A run of `-timedemo` ends with Chocolate Doom's `timed N gametics in M realtics` (through `I_Error`, so the exit code is
not 0); a zone too small ends with `Z_Malloc: failed on allocation of N bytes`.
