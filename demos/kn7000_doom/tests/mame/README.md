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

`DOOM_PLAY=1` plays: MENU, C5 three times (New Game, the episode, the skill), then once E1M1 runs (gametic moving: the
gamestate is a level's from before the map is read) G#4 held 2 s, F4 held 1 s, C5 three times, C6, E5 twice, each
CHECKed in DOOM.BIN's memory at the offsets of `offsets.txt` (`make tests/mame/offsets.txt` from `offsets.c`).
`sound_report.py SOUND.WAV [EVENTS.TXT]` (ProTracker's, unchanged: its header speaks of modules) prints each second's
level of the session's `-wavwrite` sound and counts the silent ones.

`DOOM_KEYSCAN=1` holds keys one by one at the title screen and logs what KN7APP's `notes[]` saw of each.
