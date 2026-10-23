#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# compare.py MSFA_REF [--n N] [--seed S] [--bank BANK.syx] -- does tools/fm_model.py sound like msfa?
#
# Renders the same note of the same DX7 voices with msfa itself (msfa_ref, built from msfa_ref.cpp: README.md) and with
# the model, and prints per voice: the correlation of the two signals, the level ratio, the error of their envelopes
# (RMS per 10 ms, in dB) and the spectral distance (the mean absolute dB difference of 2,048-point spectra every
# 46 ms, over the bins within 50 dB of each frame's loudest). Correlation is shown but not judged: an LFO or a
# detuned operator a fraction of a cycle apart decorrelates two signals that sound the same. The voices are N deterministic random ones (all 32 algorithms, every
# feedback, keyboard scaling, velocity sensitivity, both oscillator modes, LFO), or the 32 of a bank.
# The model's output is Q19 (a carrier scaled 1/32 of msfa's); it is multiplied by 32 before comparing.
import argparse
import os
import random
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import fm_model as m  # noqa: E402


def random_patch(rng, alg):
    d = bytearray()
    for op in range(6):
        r = [rng.randint(40, 99) for _ in range(4)]
        lv = [rng.randint(60, 99), rng.randint(40, 99), rng.randint(0, 99), 0]
        d += bytes(r + lv)
        d += bytes([rng.randint(20, 80), rng.randint(0, 60), rng.randint(0, 60), rng.randint(0, 3),
                    rng.randint(0, 3), rng.randint(0, 7), rng.choice([0, 0, 1, 3]), rng.randint(0, 7),
                    rng.randint(50, 99), 1 if rng.random() < 0.1 else 0, rng.randint(0, 6),
                    rng.randint(0, 50), rng.randint(0, 14)])
    d += bytes([rng.randint(50, 99) for _ in range(4)] + [50 + rng.randint(-8, 8) for _ in range(4)])
    d += bytes([alg, rng.randint(0, 7), 1, rng.randint(20, 60), rng.randint(0, 40), rng.randint(0, 20),
                rng.randint(0, 30), rng.randint(0, 1), rng.randint(0, 5), rng.randint(0, 4), 24])
    d += b'RND %02d    ' % alg
    return m.Patch(m.clamp_voice(d[:155]))


def spec_dist(a, b, n=2048):
    d = []
    w = np.hanning(n)
    for i in range(0, min(len(a), len(b)) - n, n):
        fa = 20 * np.log10(np.abs(np.fft.rfft(a[i:i + n] * w)) + 1e-3)
        fb = 20 * np.log10(np.abs(np.fft.rfft(b[i:i + n] * w)) + 1e-3)
        top = max(fa.max(), fb.max())
        if top < 60:
            continue
        k = (fa > top - 50) | (fb > top - 50)
        d.append(np.abs(fa[k] - fb[k]).mean())
    return float(np.mean(d)) if d else float('nan')


def env_db(x, w=441):
    n = len(x) // w
    r = np.sqrt((x[:n * w].reshape(n, w).astype(float) ** 2).mean(axis=1)) + 1e-3
    return 20 * np.log10(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('msfa_ref')
    ap.add_argument('--n', type=int, default=32)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--bank')
    ap.add_argument('--note', type=int, default=60)
    ap.add_argument('--vel', type=int, default=100)
    ap.add_argument('--nolfo', action='store_true', help='LFO depths and sensitivities to 0 (operators alone)')
    a = ap.parse_args()
    rng = random.Random(a.seed)
    if a.bank:
        patches = m.Bank.from_sysex(open(a.bank, 'rb').read()).patches
    else:
        patches = [random_patch(rng, k % 32) for k in range(a.n)]
    if a.nolfo:
        for p in patches:
            for f in ('LPMD', 'LAMD', 'LPMS'):
                p.set(f, 0)
            for k in range(1, 7):
                p.set_op(k, 'AMS', 0)
    hold, total = 44100, 66150
    rows = []
    with tempfile.TemporaryDirectory() as td:
        vf = os.path.join(td, 'v.bin')
        for i, p in enumerate(patches):
            open(vf, 'wb').write(bytes(p.data))
            ref = np.frombuffer(subprocess.run([a.msfa_ref, vf, str(a.note), str(a.vel), str(hold), str(total)],
                                               check=True, capture_output=True).stdout, '<i4').astype(float)
            mod = np.array(m.render(p, [(0.0, 'on', 0, a.note, a.vel), ((hold + 63) // 64 * 64 / m.FS, 'off', 0,
                                                                         a.note, 0)], total / m.FS, nvoices=1),
                           float) * 32
            n = min(len(ref), len(mod))
            ref, mod = ref[:n], mod[:n]
            corr = float(np.corrcoef(ref, mod)[0, 1]) if ref.std() > 0 and mod.std() > 0 else float('nan')
            lr = 20 * np.log10((np.sqrt((mod ** 2).mean()) + 1e-9) / (np.sqrt((ref ** 2).mean()) + 1e-9))
            er, em = env_db(ref), env_db(mod)
            live = er > er.max() - 60
            env_err = float(np.sqrt(((er - em)[live] ** 2).mean())) if live.any() else float('nan')
            rows.append((i, p.name, p.get('ALG') + 1, p.get('FB'), corr, lr, env_err, spec_dist(ref, mod)))
            print('%3d %-10s alg %2d fb %d  corr %6.3f  level %+6.2f dB  envelope %5.2f dB  spectrum %5.2f dB'
                  % rows[-1], flush=True)
    c = np.array([r[4] for r in rows])
    e = np.array([r[6] for r in rows])
    l = np.array([r[5] for r in rows])
    sd = np.array([r[7] for r in rows])
    print('voices %d: correlation median %.3f min %.3f; level median %+.2f dB, worst %+.2f dB; envelope error median '
          '%.2f dB, worst %.2f dB; spectral distance median %.2f dB, worst %.2f dB'
          % (len(rows), np.nanmedian(c), np.nanmin(c), np.median(l), l[np.argmax(abs(l))], np.nanmedian(e),
             np.nanmax(e), np.nanmedian(sd), np.nanmax(sd)))


if __name__ == '__main__':
    main()
