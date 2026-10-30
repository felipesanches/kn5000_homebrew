# Keyfall's song reading and judging on the PC

`make check` builds `build/host/kfcheck` from the game's own `smf.c`, `song.c`, `judge.c` and `fmt.c` (32-bit, as on the
KN7000) and runs it over every `songs/*.MID`.

Question it answers: does the game read each song as written (its parts, notes, beats, length), are the charts and the
accompaniment's messages in time order and off the keybed's channels 1-3, and does the judging give what it should to
four scripted players: a perfect one (all PERFECT, 100%, S), one 70 ms late (all GOOD, mean offset 70 ms), one who
strikes a wrong key before every fourth note (the wrong keys counted, the notes still PERFECT), one who misses every
third note (the misses counted). For each part choice the menu offers, at 100% and 50% tempo. It exits 1 when an
expectation fails; the last line says `all checks ok` or `SOME CHECKS FAILED`.

    make check                                 # every song
    build/host/kfcheck path/to/OTHER.MID       # any Standard MIDI File
