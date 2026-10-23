#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# compare_capture.py OUT [--novol] -- does the DSP in MAME produce, frame for frame, the samples tools/fm_model.py computes?
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
e.vol = None if '--novol' in sys.argv else m.volume_word(m.VOL_DEFAULT)   # --novol: a build without the volume
while e.frame_no < frames[0]:
    e.frame()
idx = {f: i for i, f in enumerate(frames)}


def run_from(t, i0, upto):
    """t at frame frames[i0]: its samples compared with MIX from i0 -> the first index that differs (or upto)"""
    for i in range(i0, upto):
        if t.frame() != mix[i]:
            return i
    return upto


def search(t0, i0, i_lo, i_hi, act, upto):
    """t0 at index i0: the frame in [i_lo, i_hi] at which act(t) (a key-on or key-off) makes the model follow MIX
    longest"""
    best = None
    t = copy.deepcopy(t0)
    for i in range(i0, i_lo):
        t.frame()
    i = i_lo
    while i <= i_hi:
        c = copy.deepcopy(t)
        act(c)
        n = run_from(c, i, upto)
        if best is None or n > best[1]:
            best = (i, n)
        if t.frame() != mix[i]:
            break                                   # the model without the act no longer matches: stop
        i += 1
    return best


base = copy.deepcopy(e)
assert run_from(e, 0, first) == first, 'the model is not silent where MIX is'
k_on, n = search(base, 0, max(0, first - 140), first, lambda c: c.key_on(0, patch, key, vel), len(mix))
print(f'key-on at frame {frames[k_on]}: the samples equal up to frame {frames[n - 1] if n else None} '
      f'({n} of {len(mix)})')
if n < len(mix):
    t = copy.deepcopy(base)
    for i in range(0, k_on):
        t.frame()
    t.key_on(0, patch, key, vel)
    lo = max(k_on, n - 140)
    for i in range(k_on, lo):
        t.frame()
    k_off, n2 = search(t, lo, lo, n, lambda c: c.key_off(0), len(mix))
    print(f'key-off at frame {frames[k_off]}: the samples equal up to the end' if n2 == len(mix) else
          f'key-off at frame {frames[k_off]}: equal up to sample {n2} of {len(mix)}')
    n = n2
nz = sum(1 for x in mix if x)
print(f'compare_capture: {"all" if n == len(mix) else n} of {len(mix)} samples equal ({nz} not zero)')
sys.exit(0 if n == len(mix) else 1)
