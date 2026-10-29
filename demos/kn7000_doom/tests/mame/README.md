# Doom in MAME

`run_doom.sh OUT [LUA]` builds the card folder, puts it with the program menu on a copy of the real KN7000's SD card,
and runs the published overlay emulator (`~/compartilhado/kn7000-emulator`, program 893 with `-bios v13`; `BIOS=v16` for
941) with the owner's loader floppy and the script. OUT is scratch (`~/compartilhado/tmp/...`).

| script          | question it answers                                                                             | how                                                           |
|-----------------|-------------------------------------------------------------------------------------------------|---------------------------------------------------------------|
| `doom_test.lua` | does Doom start from the program menu, play, and end with EXIT, giving back the RAM it borrowed | `DOOM_SECONDS=120 run_doom.sh ~/compartilhado/tmp/doom-mame1` |

`doom_test.lua` writes `OUT/events.txt`: every 2 s the converter's text, `gametic`, `gamestate` (0 a level,
1 intermission, 2 finale, 3 the title screens), `demoplayback`, episode and map, and the startup log's last line; at
the end `borrow_bad` (KN7APP.BIN's check of the borrowed RAM's copy in display memory: 0 when it came back as it went)
and the count of foreign accesses. `DOOM_TAPS=1` puts taps on the borrowed RAM (borrow.c) once the game runs and logs
every access from code that is neither KN7APP.BIN's nor DOOM.BIN's (slow: every access of the game there runs Lua).
Snapshots every `DOOM_SNAP` seconds to `OUT/snap/kn7000/`.
