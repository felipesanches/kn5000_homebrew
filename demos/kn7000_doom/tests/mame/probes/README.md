# Probes for the music (not built into Doom yet)

Each runs as `tests/mame/run_doom.sh OUT probes/<script>.lua` (it starts Doom to find KN7APP's `fw.midiIn1`).

| script             | question it answers                                                                                                                | result (tests/results/)         |
|--------------------|------------------------------------------------------------------------------------------------------------------------------------|---------------------------------|
| `gm_probe.lua`     | do program changes, GM2 banks (MSB 121/120) and GM System On/Off put into MIDI IN 1's ring change the sound on part 4 and part 10? | `gm2-bank-probe-2026-10-29.txt` |
| `midi_latency.lua` | how soon the firmware takes bytes put into MIDI IN 1's ring while Doom renders                                                     | `midi-latency-2026-10-29.txt`   |
| `phase_spectra.py` | level and spectral centroid of each marked span of a session's sound (`phase_spectra.py sound.wav events.txt`)                     |                                 |

Findings: channel 10 already plays drums in the instrument's own (NX) mode; GM System On/Off changed nothing heard in
MAME, and the programs' spectra differ too little in MAME to tell whether GM2 banks select GM sounds (the unit must
say). The ring is read only when the program task yields, once a frame: 0.6 to 159 ms, median 55 ms. Music played
from Doom's task would be late by up to a frame; it needs the task to yield more often, or another timer.
