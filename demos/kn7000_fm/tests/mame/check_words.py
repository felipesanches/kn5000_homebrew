#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# check_words.py OUT -- does the MN10300 compute the DSP's words exactly as tools/fm_model.py does? fm_test.lua dumps,
# from a MAME run, the bank as the program unpacked it (bank.bin, 32 x 155 bytes), and per check point the voice
# number, the patch words the program sent (pimg_N.bin, 70 u32) and the last key-on's staged words with its key
# (stage_N.bin: key, then 18 u32). This compares:
#   * the bank with tools/fm_host.py's built-in bank (when no BANK0.SYX was on the card: pass --bank FILE else);
#   * each pimg with fm_dsp.patch_image(fm_model.patch_words(voice));
#   * each stage with fm_model.note_words(voice, key, v) for some velocity v (the keybed's is not known: the one
#     that matches is printed).
import argparse
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import fm_model as m  # noqa: E402
import fm_dsp as d  # noqa: E402
import fm_host as h  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--bank')
    a = ap.parse_args()
    bad = 0
    raw = open(os.path.join(a.out, 'bank.bin'), 'rb').read()
    got = [m.Patch(raw[k * 155:(k + 1) * 155]) for k in range(32)]
    want = (m.Bank.from_sysex(open(a.bank, 'rb').read()) if a.bank else h.bank()).patches
    nb = sum(1 for g, w in zip(got, want) if bytes(g.data) != bytes(w.data))
    print(f'bank: {32 - nb} of 32 voices as the model unpacks them')
    bad += nb
    for f in sorted(glob.glob(os.path.join(a.out, 'pimg_*.bin'))):
        b = open(f, 'rb').read()
        voice = struct.unpack('<I', b[:4])[0]
        n = d.PATCH_END - d.PATCH
        words = list(struct.unpack('<%dI' % n, b[4:4 + 4 * n]))
        ref = d.patch_image(m.patch_words(got[voice]))
        diff = [k for k in range(len(ref)) if ref[k] != words[k]]
        print(f'{os.path.basename(f)}: voice {voice + 1}: {len(ref) - len(diff)} of {len(ref)} patch words equal'
              + (f'; first differences at {diff[:6]}: got {[hex(words[k]) for k in diff[:3]]}, '
                 f'model {[hex(ref[k]) for k in diff[:3]]}' if diff else ''))
        bad += len(diff)
    for f in sorted(glob.glob(os.path.join(a.out, 'stage_*.bin'))):
        b = open(f, 'rb').read()
        voice, key = struct.unpack('<II', b[:8])
        words = list(struct.unpack('<18I', b[8:8 + 72]))
        found = None
        for vel in range(1, 128):
            p, o, s = m.note_words(got[voice], key, vel)
            if [x & 0xFFFFFFFF for x in p + o + s] == words:
                found = vel
                break
        if found:
            print(f'{os.path.basename(f)}: voice {voice + 1} key {key}: the 18 words equal note_words at velocity {found}')
        else:
            p, o, s = m.note_words(got[voice], key, 100)
            ref = [x & 0xFFFFFFFF for x in p + o + s]
            diff = [k for k in range(18) if ref[k] != words[k]]
            print(f'{os.path.basename(f)}: voice {voice + 1} key {key}: NO velocity matches; at 100 words {diff} '
                  f'differ: got {[hex(words[k]) for k in diff[:4]]}, model {[hex(ref[k]) for k in diff[:4]]}')
            bad += 1
    print('check_words: ' + ('all equal' if not bad else f'{bad} differences'))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
