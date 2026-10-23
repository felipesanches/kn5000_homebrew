#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# compare_capture.py OUT -- does the DSP in MAME produce, frame for frame, the samples tools/fm_model.py computes?
# fm_capture.lua records (FRAME after the frame, MIX) for every DSP frame of one C4 (OUT/capture.txt) and the
# key-on's words (OUT/stage_0.bin). The model starts as the engine was hooked (FRAME 0, the built-in bank's voice 1,
# every voice silent), runs to the key-on, which is tried at every frame before the first sound (the host's command
# lands on a frame the probe cannot see), and from the key-on on its samples are compared with MIX. Prints the
# key-on frame that matches and how many samples are equal; on a difference, the first ones.
import copy
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import fm_model as m  # noqa: E402
import fm_host as h  # noqa: E402

out = sys.argv[1]
cap = [tuple(map(int, ln.split())) for ln in open(os.path.join(out, 'capture.txt'))]
frames = [f - 1 for f, _ in cap]               # the frame's own number (FRAME is incremented before MIX)
mix = [x for _, x in cap]
assert all(b == a + 1 for a, b in zip(frames, frames[1:])), 'the capture skipped frames'
patch = h.bank().patches[0]
b = open(os.path.join(out, 'stage_0.bin'), 'rb').read()
_, key = struct.unpack('<II', b[:8])
stage = list(struct.unpack('<18I', b[8:80]))
vel = next(v for v in range(1, 128) if [x & 0xFFFFFFFF for x in sum(m.note_words(patch, key, v), [])] == stage)
first = next(i for i, x in enumerate(mix) if x != 0)
print(f'captured {len(mix)} frames from frame {frames[0]}; key {key} velocity {vel}; first sound at frame {frames[first]}')
e = m.Engine()
e.set_patch(patch)
lo = frames[first] - 140
while e.frame_no < lo:
    e.frame()
best = None
for k in range(lo, frames[first] + 1):
    t = copy.deepcopy(e)
    t.key_on(0, patch, key, vel)
    while t.frame_no < frames[0]:
        t.frame()
    got = []
    for i in range(len(mix)):
        got.append(t.frame())
        if got[-1] != mix[i] and i > first + 64:
            break
    n = next((i for i in range(len(got)) if got[i] != mix[i]), len(got))
    if best is None or n > best[1]:
        best = (k, n, got)
    e.frame()
k, n, got = best
print(f'best key-on frame {k}: the first {n} of {len(mix)} samples equal' + ('' if n == len(mix) else
      f'; then model {got[n:n + 4]}, MAME {mix[n:n + 4]}'))
sys.exit(0 if n == len(mix) else 1)
