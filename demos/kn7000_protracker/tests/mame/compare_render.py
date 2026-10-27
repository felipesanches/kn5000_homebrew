#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# compare_render.py [--waveform] SOUND.WAV MODULE|REFERENCE.WAV [SECONDS] -- does SOUND play the module as the reference
# does? SOUND is the MAME session's sound (run_pt.sh's -wavwrite, 48 kHz) or a rendering; the reference is the module
# rendered by libopenmpt (through ffmpeg, its subsong 0: from order 0, as ProTracker plays it; libopenmpt plays
# ProTracker modules as ProTracker 2.3 does), or a WAV (tests/host/render: what the port should give, the DSP's mixer
# modelled). Both are brought to 44,100 Hz mono. The reference's first SECONDS (default 30; less if it ends sooner:
# libopenmpt ends a song at its loop) are found in SOUND where its first 2 s correlate best, then compared a window at a
# time, each at the lag within 25 ms where it correlates best:
#   default:     loudness envelopes (the dB of each 20 ms), 5 s windows: a note at the wrong time moves a window's lag,
#                the wrong samples or volumes lower r and move the level difference. For a reference that sounds the
#                same but is made differently (libopenmpt interpolates, filters and pans; the DSP does none of that);
#   --waveform:  the waveforms themselves, 2 s windows: for a reference that should be the same sample for sample
#                (tests/host/render against the MAME session), up to the level, the timing of each tick (the replayer
#                runs by the kernel's clock: a few ms) and MAME's resampling to 48 kHz.
# Prints where the module starts in SOUND and, per window: the lag, r, and SOUND's level against the reference.
import os
import subprocess
import sys
import wave

import numpy as np

RATE = 44100


def read(path):
    w = wave.open(path, "rb")
    ch, rate = w.getnchannels(), w.getframerate()
    assert w.getsampwidth() == 2, "16-bit WAV expected"
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(float).reshape(-1, ch).mean(axis=1)
    if rate != RATE:
        t = np.arange(0, len(x) / rate, 1 / RATE)
        x = np.interp(t, np.arange(len(x)) / rate, x)
    return x


def corr(a, b):
    n = min(len(a), len(b))
    if n < 2:
        return -1.0
    a, b = a[:n] - a[:n].mean(), b[:n] - b[:n].mean()
    d = np.linalg.norm(a) * np.linalg.norm(b)
    return float(np.dot(a, b) / d) if d else 0.0


def db_env(x, frame):
    n = len(x) // frame
    rms = np.sqrt((x[:n * frame].reshape(n, frame) ** 2).mean(axis=1))
    return 20 * np.log10(np.maximum(rms, 1.0) / 32768)


args = sys.argv[1:]
waveform = args[:1] == ["--waveform"]
if waveform:
    args = args[1:]
sound, module = args[0], args[1]
seconds = float(args[2]) if len(args) > 2 else 30.0
if module.lower().endswith(".wav"):
    ref = read(module)
else:
    scratch = os.path.join(os.path.expanduser("~/compartilhado/tmp"), "pt-compare")
    os.makedirs(scratch, exist_ok=True)
    path = os.path.join(scratch, os.path.basename(module) + ".openmpt.wav")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-subsong", "0", "-i", module, "-t", str(seconds), "-ac", "1",
                    "-ar", str(RATE), "-c:a", "pcm_s16le", path], check=True)
    ref = read(path)
ref = ref[:int(seconds * RATE)]
snd = read(sound)

# where the reference starts in SOUND: its first 2 s against SOUND's loudness (10 ms frames), then to the sample
head = ref[:2 * RATE]
F = 441
re_, se_ = db_env(head, F), db_env(snd, F)
coarse = max(range(0, len(se_) - len(re_) + 1), key=lambda k: corr(re_, se_[k:k + len(re_)]))
fine = max(range(max(0, coarse * F - F), coarse * F + F), key=lambda k: corr(head, snd[k:k + len(head)]))
start = fine if waveform else coarse * F
print(f"{os.path.basename(module)}: its first {len(ref) / RATE:.1f} s start at {start / RATE:.3f} s of {os.path.basename(sound)}"
      f"{' (waveforms)' if waveform else ' (loudness envelopes)'}")
win = (2 if waveform else 5) * RATE
reach = int(0.025 * RATE)
rs = []
for w0 in range(0, len(ref) - win + 1, win):
    r_w = ref[w0:w0 + win]
    if start + w0 + reach + win > len(snd):
        break
    if waveform:
        cand = range(-reach, reach + 1)
        score = lambda k: corr(r_w, snd[start + w0 + k:start + w0 + k + win])
    else:
        cand = range(-reach, reach + 1, F)
        r_e = db_env(r_w, 882)
        score = lambda k: corr(r_e, db_env(snd[start + w0 + k:start + w0 + k + win], 882))
    k = max(cand, key=score)
    seg = snd[start + w0 + k:start + w0 + k + win]
    level = 20 * np.log10(max(np.sqrt((seg ** 2).mean()), 1.0) / max(np.sqrt((r_w ** 2).mean()), 1.0))
    r = score(k)
    rs.append(r)
    print(f"  {w0 / RATE:5.1f}-{(w0 + win) / RATE:5.1f} s: lag {k / RATE * 1000:+6.1f} ms, r {r:.3f}, level {level:+.1f} dB")
if rs:
    print(f"  r: least {min(rs):.3f}, median {float(np.median(rs)):.3f}")
