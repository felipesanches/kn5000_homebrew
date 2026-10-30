#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# sound_levels.py SOUND.WAV EVENTS.TXT [FROM TO] -- is the instrument heard while Keyfall plays (run_keyfall.sh's
# -wavwrite and keyfall_test.lua's events.txt)? Prints the level (RMS of both channels, dBFS) of each half second
# between the events "the game starts" and "game over" (or FROM to TO, emulated seconds), and the level of the stretch
# as a whole against the 5 s before the game started. With KF_NOPRESS=1 only the accompaniment sounds, so a level above
# the silence before says the instrument played what the game put into MIDI IN 1.
import math
import re
import struct
import sys
import wave

w = wave.open(sys.argv[1], 'rb')
rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
assert w.getsampwidth() == 2, '16-bit WAV expected'
data = struct.unpack(f'<{n * ch}h', w.readframes(n))

start = end = None
for line in open(sys.argv[2]):
    m = re.match(r'\s*([\d.]+) (.*)', line)
    if not m:
        continue
    t, text = float(m.group(1)), m.group(2)
    if 'the game starts' in text:
        start = t
    if text.startswith('game over after'):
        end = t
if len(sys.argv) > 4:
    start, end = float(sys.argv[3]), float(sys.argv[4])
assert start is not None and end is not None, 'no "the game starts" / "game over" in the events'


def db(a, b):
    seg = data[int(a * rate) * ch:int(b * rate) * ch]
    if not seg:
        return -200.0
    rms = math.sqrt(sum(v * v for v in seg) / len(seg))
    return 20 * math.log10(rms / 32768) if rms > 0 else -200.0


t = start
while t < end:
    print(f'{t:8.1f} s  {db(t, t + 0.5):7.1f} dBFS')
    t += 0.5
print(f'before the game ({start - 5:.1f}-{start:.1f} s): {db(start - 5, start):.1f} dBFS; '
      f'the game ({start:.1f}-{end:.1f} s): {db(start, end):.1f} dBFS')
