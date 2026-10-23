#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# compare_state.py OUT [--novol] [--first] -- voice 0's words in MAME against the model's, at the frames fm_capture.lua dumped
# them (OUT/voice_dumps.txt: FRAME after the frame, voice 0's 64 words, the globals), for the key-on frame that
# fits best: which word differs first, and by how much. (compare_capture.py says whether the samples agree; this
# says where they part.)
import copy
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import fm_model as m  # noqa: E402
import fm_dsp as d  # noqa: E402
import fm_host as h  # noqa: E402

out = sys.argv[1]
dumps = []
for ln in open(os.path.join(out, 'voice_dumps.txt')):
    f = ln.split()
    dumps.append((int(f[0]) - 1, [int(x, 16) for x in f[1:65]], [int(x, 16) for x in f[65:]]))
patch = h.bank().patches[0]
b = open(os.path.join(out, 'stage_0.bin'), 'rb').read()
_, key = struct.unpack('<II', b[:8])
stage = list(struct.unpack('<18I', b[8:80]))
vel = next(v for v in range(1, 128) if [x & 0xFFFFFFFF for x in sum(m.note_words(patch, key, v), [])] == stage)
FIELDS = [('PH', d.O_PH, 6), ('INC', d.O_INC, 6), ('G', d.O_G, 6), ('DG', d.O_DG, 6), ('LEVEL', d.O_LEVEL, 6),
          ('STATE', d.O_STATE, 6), ('PITCH', d.O_PITCH, 6), ('OLRS', d.O_OLRS, 6), ('PMOD', d.O_PMOD, 6),
          ('FB0', d.O_FB0, 1), ('FB1', d.O_FB1, 1), ('ACTIVE', d.O_ACTIVE, 1), ('PEGL', d.O_PEGL, 1),
          ('PEGIX', d.O_PEGIX, 1)]


def words(v):
    vals = {'PH': v.ph, 'INC': v.inc, 'G': v.g, 'DG': v.dg, 'LEVEL': v.level, 'STATE': v.state, 'PITCH': v.pitch,
            'OLRS': v.olrs, 'PMOD': v.pmod, 'FB0': [v.fb[0]], 'FB1': [v.fb[1]], 'ACTIVE': [int(v.active)],
            'PEGL': [v.peg_level], 'PEGIX': [v.peg_ix]}
    w = [0] * 64
    for name, off, n in FIELDS:
        for k in range(n):
            w[32 + off + k] = vals[name][k] & 0xFFFFFFFF
    return w


def diffs(w, ref):
    out_ = []
    for name, off, n in FIELDS:
        for k in range(n):
            a, b_ = w[32 + off + k], ref[32 + off + k]
            if a != b_:
                out_.append(f'{name}[{k}] model {a:08X} MAME {b_:08X}')
    return out_


e = m.Engine()
e.set_patch(patch)
e.vol = None if '--novol' in sys.argv else m.volume_word(m.VOL_DEFAULT)
first = dumps[0][0]
while e.frame_no < first - 140:
    e.frame()
results = []
for k in range(first - 140, first + 1):
    t = copy.deepcopy(e)
    t.key_on(0, patch, key, vel)
    res = []
    for fr, w, glob in dumps:
        while t.frame_no <= fr:
            t.frame()
        res.append(diffs(words(t.v[0]), w))
    results.append((sum(len(r) for r in res), k, res))
    e.frame()
if '--first' in sys.argv:                           # every frame dumped: where do they part first?
    results.sort()
    n, k, res = results[0]
    t = m.Engine()
    t.set_patch(patch)
    t.vol = None if '--novol' in sys.argv else m.volume_word(m.VOL_DEFAULT)
    for fr, w, glob in dumps:
        while t.frame_no <= fr:
            if t.frame_no == k:
                t.key_on(0, patch, key, vel)
            t.frame()
        r = diffs(words(t.v[0]), w)
        if r:
            print(f'key-on frame {k}: the first difference after frame {fr} (slot {fr & 63}): ' + '; '.join(r[:8]))
            break
    else:
        print(f'key-on frame {k}: every dumped frame equal')
    sys.exit(0)
results.sort()
n, k, res = results[0]
print(f'key {key} velocity {vel}; the best key-on frame {k}: {n} words differ in all')
for (fr, _, _), r in zip(dumps, res):
    print(f'  after frame {fr}: ' + ('all equal' if not r else f'{len(r)} differ: ' + '; '.join(r[:8])))
