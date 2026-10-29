#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# sound_report.py SOUND.WAV [EVENTS.TXT] -- is ProTracker heard in the MAME session's sound (run_pt.sh's -wavwrite)?
# Prints each second's level (RMS of both channels, dBFS) and its peak; with events.txt (pt_test.lua), from the RUN on,
# and marks the seconds the script pressed something in. "silent" is below -85 dBFS, the WAV's last few steps (the
# DSP_GAIN of 2 plays modules at about -70 dBFS in MAME; it was -60 while the gain was 128). The last line counts the seconds
# from RUN + 15 s (the loader, the menu and the first module's samples) to the end, and how many of them are silent.
import struct
import sys
import math
import wave

path = sys.argv[1]
events = sys.argv[2] if len(sys.argv) > 2 else None
w = wave.open(path, "rb")
rate, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
assert width == 2, "16-bit WAV expected"
data = w.readframes(n)
samples = struct.unpack(f"<{n * ch}h", data)

run_at, marks = 0.0, {}
if events:
    for line in open(events):
        parts = line.split(None, 1)
        if len(parts) < 2:
            continue
        t, text = float(parts[0]), parts[1].strip()
        if text == "RUN":
            run_at = t
        if text.startswith("LCD"):
            marks[int(t)] = text

loud = silent = 0
for s in range(int(run_at), n // rate):
    seg = samples[s * rate * ch:(s + 1) * rate * ch]
    if not seg:
        break
    rms = math.sqrt(sum(v * v for v in seg) / len(seg))
    peak = max(abs(v) for v in seg)
    db = 20 * math.log10(rms / 32768) if rms > 0 else -120.0
    quiet = db < -85
    if s >= run_at + 15:
        silent += quiet
        loud += not quiet
    print(f"{s:5d} s  {db:7.1f} dBFS  peak {peak:5d}{'  silent' if quiet else ''}{'  ' + marks[s] if s in marks else ''}")
print(f"sound: {loud} of {loud + silent} seconds from RUN + 15 s are heard ({silent} silent)")
