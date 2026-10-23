#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# dsp_regions.py OUT -- which of the DSP's regions differ from what KN7FM uploads? fm_test.lua writes OUT/dsp_mem.txt
# (the DSP's PM and DM words when the program failed its readback); this compares each upload of dsp.s DspPrepare
# (fm_dsp.py's images, the built-in bank's first voice for the patch, algorithm 5's kernel at PM 0x8E00) and prints
# per region the words that differ.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import fm_model as m  # noqa: E402
import fm_dsp as d  # noqa: E402
import fm_host as h  # noqa: E402

mem = {}
for line in open(os.path.join(sys.argv[1], 'dsp_mem.txt')):
    sp, a, v = line.split()
    mem[(sp, int(a, 16))] = int(v, 16)
w1, w2, labels, algs = d.build()
patch = h.bank().patches[0]
regions = [('code', 'pm', d.PM_CODE, [w for _, w, _ in w1]), ('code2', 'pm', d.PM_CODE2, [w for _, w, _ in w2]),
           ('kernel', 'pm', d.PM_ALG[0], [w for _, w, _ in algs[patch.get('ALG') & 31]]),
           ('sine', 'dm', d.SINE, [x & 0xFFFFFFFF for x in m.SIN_TABLE]), ('statics', 'dm', d.STATICS, m.STATICS),
           ('globals', 'dm', d.GLOBALS, d.initial_globals()), ('patch', 'dm', d.PATCH, d.patch_image(m.patch_words(patch)))]
for v in range(8):
    regions.append(('voice %d' % v, 'dm', d.VOICES + 64 * v, d.voice_image()))
for name, sp, base, words in regions:
    diff = [k for k, w in enumerate(words) if mem.get((sp, base + k)) != w]
    print(f'{name:8} {sp} 0x{base:04X} {len(words):4} words: ' + ('equal' if not diff else
          f'{len(diff)} differ, first at +{diff[0]}: got {mem.get((sp, base + diff[0]), 0):X}, want {words[diff[0]]:X}'))
