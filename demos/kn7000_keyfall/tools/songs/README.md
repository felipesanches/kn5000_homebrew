# Writing songs for Keyfall

`mksong.py IN.txt OUT.MID` makes a Standard MIDI File (format 1, 480 ticks a quarter note) from a song written as text;
`make songs` does it for every `songs/*.txt` (the `.MID` files are committed beside them). Any Standard MIDI File plays
in the game too: this is only a way to write one by hand.

    title: Ode to Joy
    composer: Ludwig van Beethoven (1824)
    tempo: 100
    time: 4/4

    track: Right hand
    channel: 1
    program: 0
    len: /4
    vel: 90
    B4 B4 C5 D5 | D5 C5 B4 A4 | G4 G4 A4 B4 | B4/4. A4/8 A4/2 |

The first lines are the song's: its title, its composer, its tempo in quarter notes a minute, its time signature (the
bars are checked against it). Then each `track:` (its name is the part's name in the game) with its MIDI channel (10 for
drums), its General MIDI program (0 piano, 32 acoustic bass, 48 strings), the length of a note written without one
(default `/4`), its velocity (default 90), and its notes: a name and an octave (C4 is middle C, 60) or a MIDI number.

| written               | means                                                                                     |
|-----------------------|-------------------------------------------------------------------------------------------|
| `C4` `F#4` `Bb3` `60` | a note of the default length                                                              |
| `C4/8` `C4/2` `C4/1`  | an eighth, a half, a whole note (`/16`, `/32` too)                                        |
| `C4/4.`               | dotted: half as long again                                                                |
| `[C3 E3 G3]/2`        | a chord                                                                                   |
| `R/4`                 | a rest                                                                                    |
| `C4/4!60`             | velocity 60 for that note                                                                 |
| `( ... )*8`           | the notes between the brackets 8 times                                                    |
| `\|`                  | a bar line: each bar must fill the time signature (the first and the last may be shorter) |
| `# ...`               | a comment, on a line of its own (`#` elsewhere is a sharp)                                |

The game names a part by its track's name: a track named with "right", "RH" or "melody" is the part offered first, and
when one track is named for the right hand and one for the left (or "LH"), the game offers both hands together as well.
