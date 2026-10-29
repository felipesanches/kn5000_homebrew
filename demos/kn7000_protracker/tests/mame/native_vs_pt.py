#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# native_vs_pt.py OUT [FROM TO] -- in one MAME session (run_pt.sh with PT_NATIVE=1), how loud is ProTracker against
# the instrument's own sound? The instrument's: from the events' "NATIVE start" to "NATIVE end" (pt_test.lua: four
# notes and a chord on the power-on panel). ProTracker's: FROM..TO seconds after RUN (default 25..45, the first module
# playing). Prints both levels (RMS of all channels, dBFS) and ProTracker minus the instrument, against the unit's:
# the owner (2026-10-29) finds the instrument comfortable at MAIN VOLUME 40-50 % and ProTracker (DSP_GAIN 2) at 70 %,
# which the driver's v^2 taper makes ProTracker 40 log10(45/70) = -7.7 dB below the instrument.
import math
import struct
import sys
import wave

out = sys.argv[1]
t0, t1 = (float(sys.argv[2]), float(sys.argv[3])) if len(sys.argv) > 3 else (25.0, 45.0)
marks = {}
for line in open(f"{out}/events.txt"):
    parts = line.split(None, 1)
    if len(parts) == 2 and parts[1].strip() in ("NATIVE start", "NATIVE end", "RUN"):
        marks[parts[1].strip()] = float(parts[0])
for k in ("NATIVE start", "NATIVE end", "RUN"):
    if k not in marks:
        sys.exit(f"native_vs_pt: no {k!r} in {out}/events.txt (PT_NATIVE=1?)")

w = wave.open(f"{out}/sound.wav", "rb")
rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
assert w.getsampwidth() == 2, "16-bit WAV expected"


def rms_db(a, b):
    a, b = int(a * rate), min(int(b * rate), n)
    w.setpos(a)
    s = struct.unpack(f"<{(b - a) * ch}h", w.readframes(b - a))
    return 20 * math.log10(max(math.sqrt(sum(x * x for x in s) / len(s)), 1e-9) / 32768)


native = rms_db(marks["NATIVE start"], marks["NATIVE end"])
pt = rms_db(marks["RUN"] + t0, marks["RUN"] + t1)
target = 40 * math.log10(45 / 70)
print(f"the instrument: RMS {native:6.1f} dBFS ({marks['NATIVE start']:.1f}..{marks['NATIVE end']:.1f} s)")
print(f"ProTracker:     RMS {pt:6.1f} dBFS (RUN + {t0:g}..{t1:g} s)")
print(f"ProTracker - the instrument: {pt - native:+.1f} dB; the unit's: {target:+.1f} dB; off by {pt - native - target:+.1f} dB")
