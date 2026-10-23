#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# levels.py OUT [T_FM T_TG] -- how loud is KN7FM against the instrument's own sound, in a MAME session's recording
# (fm_test.lua's OUT/run.wav, 48 kHz)? Prints the RMS (dB of 16-bit samples) of every 2 s from 60 s on, then, for
# the first note found after T_FM (default 70 s: fm_test.lua's C4 on KN7FM) and after T_TG (default 97 s: its C4
# on the instrument after KN7FM ended), the level of the harmonics of C4 (0.1-0.6 s after the onset). In MAME the
# instrument's own note sounds under KN7FM's too (the expression KN7FM sends to mute it is not emulated), so the
# KN7FM note's 786 and 1,048 Hz are the instrument's.
import sys
import wave

import numpy as np

out = sys.argv[1]
t_fm = float(sys.argv[2]) if len(sys.argv) > 2 else 70.0
t_tg = float(sys.argv[3]) if len(sys.argv) > 3 else 97.0
w = wave.open(out + '/run.wav')
fs, ch = w.getframerate(), w.getnchannels()
x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, ch).astype(float)[:, 0]
print('RMS every 2 s: ' + '; '.join('%d s %.1f' % (t, 20 * np.log10(np.sqrt((x[int(t * fs):int((t + 2) * fs)] ** 2)
                                                                            .mean()) + 1e-9))
                                    for t in range(60, int(len(x) / fs) - 1, 2)))


def onset(t0):
    i0 = int(t0 * fs)
    env = np.abs(x[i0:i0 + 5 * fs])
    return i0 + int(np.argmax(env > env.max() * 0.05))


def spec(a, n=int(0.5 * 48000)):
    f_ = np.fft.rfftfreq(n, 1 / fs)
    db = 20 * np.log10(np.abs(np.fft.rfft(x[a:a + n] * np.hanning(n))) + 1e-9)
    return lambda hz: db[np.argmin(abs(f_ - hz)) - 2:np.argmin(abs(f_ - hz)) + 3].max()


a, b = onset(t_fm), onset(t_tg)
fa, fb = spec(a + int(0.1 * fs)), spec(b + int(0.1 * fs))
print('onsets: KN7FM %.2f s, the instrument %.2f s' % (a / fs, b / fs))
for hz in (262, 524, 786, 1048, 3400):
    print(f'{hz:5} Hz: KN7FM {fa(hz):6.1f} dB, the instrument {fb(hz):6.1f} dB')
