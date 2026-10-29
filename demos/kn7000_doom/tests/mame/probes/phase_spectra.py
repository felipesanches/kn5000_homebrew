#!/usr/bin/env python3
# phase_spectra.py SOUND.WAV EVENTS.TXT -- level and spectral centroid of each "<mark> start" .. "<mark> end" span of a
# MAME session's -wavwrite sound (events.txt times are emulated seconds from the session's start, as the WAV's).
import math, re, struct, sys, wave
import numpy as np
w = wave.open(sys.argv[1]); rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
x = np.frombuffer(w.readframes(n), dtype='<i2').reshape(-1, ch).astype(float).mean(axis=1)
spans, open_ = [], {}
for line in open(sys.argv[2]):
    m = re.match(r'\s*([\d.]+) (.*) (start|end)$', line.strip())
    if m:
        t, mark, kind = float(m.group(1)), m.group(2), m.group(3)
        if kind == 'start': open_[mark] = t
        elif mark in open_: spans.append((mark, open_.pop(mark), t))
for mark, a, b in spans:
    seg = x[int(a * rate):int(b * rate)]
    rms = math.sqrt((seg ** 2).mean()) if len(seg) else 0
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1 / rate)
    cen = (spec * f).sum() / spec.sum() if spec.sum() else 0
    hi = spec[f > 2000].sum() / spec.sum() if spec.sum() else 0
    print(f'{mark:24s} {a:7.2f}-{b:7.2f}  rms {20*math.log10(max(rms,1e-9)/32768):6.1f} dBFS  centroid {cen:6.0f} Hz  above 2 kHz {100*hi:4.1f} %')
