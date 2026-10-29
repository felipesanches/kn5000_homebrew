#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# level_compare.py OUT_A OUT_B [FROM TO] -- how much quieter or louder is session B than session A?
# Each OUT is a run_pt.sh output directory (sound.wav and events.txt), the same module played in both. Takes the
# window FROM..TO seconds after each session's RUN (default 20..45: the first module playing, its samples sent), and
# prints each session's level (RMS of all channels, dBFS) and peak, then B - A in dB. Made to check the DSP mixer's
# output gain (Makefile DSP_GAIN): 128 against 16 should read -18.1 dB.
import math
import struct
import sys
import wave


def run_at(events):
    for line in open(events):
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[1].strip() == "RUN":
            return float(parts[0])
    sys.exit(f"level_compare: no RUN in {events}")


def level(out, t0, t1):
    w = wave.open(f"{out}/sound.wav", "rb")
    rate, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
    assert width == 2, "16-bit WAV expected"
    run = run_at(f"{out}/events.txt")
    a, b = int((run + t0) * rate), min(int((run + t1) * rate), n)
    assert b > a, f"{out}: the window ends before it starts"
    w.setpos(a)
    s = struct.unpack(f"<{(b - a) * ch}h", w.readframes(b - a))
    rms = math.sqrt(sum(x * x for x in s) / len(s))
    peak = max(abs(x) for x in s)
    return 20 * math.log10(max(rms, 1e-9) / 32768), 20 * math.log10(max(peak, 1) / 32768)


t0, t1 = (float(sys.argv[3]), float(sys.argv[4])) if len(sys.argv) > 4 else (20.0, 45.0)
ra, pa = level(sys.argv[1], t0, t1)
rb, pb = level(sys.argv[2], t0, t1)
print(f"A {sys.argv[1]}: RMS {ra:6.1f} dBFS, peak {pa:6.1f} dBFS (RUN + {t0:g}..{t1:g} s)")
print(f"B {sys.argv[2]}: RMS {rb:6.1f} dBFS, peak {pb:6.1f} dBFS")
print(f"B - A: RMS {rb - ra:+.1f} dB, peak {pb - pa:+.1f} dB")
