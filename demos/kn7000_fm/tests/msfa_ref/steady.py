#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# steady.py MSFA_REF [FB] -- do the model and msfa give the same steady-state spectrum, algorithm by algorithm?
# compare.py's 32 random voices (seed 1, one per algorithm) made static: every rate 99, L2 = L3 = L1, no LFO, a flat
# pitch envelope; C4 at velocity 100; one long FFT over 0.25-1.0 s. Prints per algorithm the median and 90th
# percentile of the dB difference over the bins within 40 dB of the loudest, and the level difference. FB (0-7), when
# given, replaces every voice's feedback (algorithm 32's feedback operator is a carrier: FB 7 tests that path).
import os
import random
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm_model as m  # noqa: E402
import compare as c  # noqa: E402

rng = random.Random(1)
with tempfile.TemporaryDirectory() as td:
    vf = os.path.join(td, 'v.bin')
    for idx in range(32):
        p = c.random_patch(rng, idx)
        if len(sys.argv) > 2:
            p.set('FB', int(sys.argv[2]))
        for f in ('LPMD', 'LAMD', 'LPMS'):
            p.set(f, 0)
        for f in ('PL1', 'PL2', 'PL3', 'PL4'):
            p.set(f, 50)
        for k in range(1, 7):
            p.set_op(k, 'AMS', 0)
            for s in (1, 2, 3, 4):
                p.set_op(k, 'R%d' % s, 99)
            p.set_op(k, 'L2', p.op(k, 'L1'))
            p.set_op(k, 'L3', p.op(k, 'L1'))
        open(vf, 'wb').write(bytes(p.data))
        ref = np.frombuffer(subprocess.run([sys.argv[1], vf, '60', '100', '44100', '44100'], check=True,
                                           capture_output=True).stdout, '<i4').astype(float)
        mod = np.array(m.render(p, [(0, 'on', 0, 60, 100)], 1.0, nvoices=1), float) * 32
        a, b = ref[11025:44100], mod[11025:44100]
        w = np.hanning(len(a))
        fa = 20 * np.log10(np.abs(np.fft.rfft(a * w)) + 1e-3)
        fb = 20 * np.log10(np.abs(np.fft.rfft(b * w)) + 1e-3)
        k = fa > max(fa.max(), fb.max()) - 40
        d = np.abs(fa[k] - fb[k])
        print('alg %2d fb %d  peaks within 40 dB: median diff %.2f dB, 90%% %.2f dB, level %+.2f dB'
              % (idx + 1, p.get('FB'), np.median(d), np.percentile(d, 90),
                 20 * np.log10(np.sqrt((b ** 2).mean()) / np.sqrt((a ** 2).mean()))))
