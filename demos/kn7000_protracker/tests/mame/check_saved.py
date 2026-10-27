#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# check_saved.py ORIGINAL.MOD SAVED.MOD -- what did the editor change in a module it saved (pt_test.lua PT_EDIT=2:
# SAVE AS NEW writes PTNEW01.MOD; card_get.py takes it off the card image)? Prints every header field, position and
# pattern cell that differs (pattern, row, channel: note period, sample, effect, before -> after), and whether the
# sample data are the same byte for byte. Then libopenmpt (through ffmpeg) plays the saved module's first 5 s: it must
# load. Exit status 1 when the samples differ or libopenmpt cannot play it.
import struct
import subprocess
import sys

a, b = (open(p, 'rb').read() for p in sys.argv[1:3])


def parse(d):
    heads = [struct.unpack('>22sHBBHH', d[20 + 30 * i:50 + 30 * i]) for i in range(31)]
    length, order = d[950], list(d[952:1080])
    npat = max(order) + 1
    pats = d[1084:1084 + npat * 1024]
    samples = d[1084 + npat * 1024:]
    return heads, length, order, npat, pats, samples


def cell(p, i):
    b0, b1, b2, b3 = p[4 * i:4 * i + 4]
    return ((b0 & 15) << 8) | b1, (b0 & 0xF0) | (b2 >> 4), b2 & 15, b3


ha, la, oa, na, pa, sa = parse(a)
hb, lb, ob, nb, pb, sb = parse(b)
print(f"song length {la} -> {lb}" if la != lb else f"song length {la}")
for i in range(128):
    if oa[i] != ob[i]:
        print(f"position {i}: pattern {oa[i]} -> {ob[i]}")
for i in range(31):
    if ha[i] != hb[i]:
        print(f"sample {i + 1:02X} header {ha[i]} -> {hb[i]}")
for i in range(min(na, nb) * 256):
    ca, cb = cell(pa, i), cell(pb, i)
    if ca != cb:
        p, r, c = i // 256, (i % 256) // 4, i % 4
        print(f"pattern {p:2d} row {r:2d} channel {c + 1}: period {ca[0]} sample {ca[1]} effect {ca[2]:X}{ca[3]:02X}"
              f" -> period {cb[0]} sample {cb[1]} effect {cb[2]:X}{cb[3]:02X}")
same = sa == sb
print(f"sample data: {'the same' if same else 'DIFFERENT'} ({len(sb)} bytes)")
r = subprocess.run(["ffmpeg", "-loglevel", "error", "-subsong", "0", "-i", sys.argv[2], "-t", "5", "-f", "null", "-"],
                   capture_output=True, text=True)
print(f"libopenmpt: {'plays it' if r.returncode == 0 else 'FAILED ' + r.stderr.strip()}")
sys.exit(0 if same and r.returncode == 0 else 1)
