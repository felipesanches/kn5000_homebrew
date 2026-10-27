#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# check_bank.py AW_TOOLS SCRATCH MOD... -- does the port's sample coder (src/kn7/bank.c, built here for the PC) code a
# module's samples exactly as technics-anotherworld's dsp_bank.py does, and do they decode back (dsp_bank.decode, the
# DSP decoder's model) to the samples? For each module: its samples laid out as the port lays them out (kn7AllocSamples:
# 32 zero bytes, then each sample from a multiple of 32 bytes), coded by both, compared word for word; the checksum
# compared too. Prints one line a module and "bank: N of N match"; exit status 1 on any difference.
import os
import struct
import subprocess
import sys

aw, scratch, mods = sys.argv[1], sys.argv[2], sys.argv[3:]
sys.path.insert(0, aw)
import dsp_bank  # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
exe = os.path.join(scratch, "bank_main")
subprocess.run(["cc", "-O2", "-DBANK_HOST_TEST", "-I", os.path.join(here, "../../src/kn7"), "-o", exe,
                os.path.join(here, "bank_main.c"), os.path.join(here, "../../src/kn7/bank.c")], check=True)


def layout(mod):
    """the samples of a 31-sample MOD as the port packs them (pt2_replayer.c kn7AllocSamples), lengths clipped as
    pt2-clone's loader clips them (65534 bytes)"""
    heads = []
    for i in range(31):
        h = mod[20 + 30 * i:20 + 30 * (i + 1)]
        length, fine, vol, rep, replen = struct.unpack(">HBBHH", h[22:30])
        heads.append((length * 2, rep * 2, replen * 2))
    npat = max(mod[952:952 + 128]) + 1
    p = 1084 + npat * 1024
    img = bytearray(32)
    for length, rep, replen in heads:
        real = length
        length = min(length, 65534)
        room = length
        if replen > 2 and rep + replen > room:
            room = rep + replen
        room = min(room, 65534)
        data = mod[p:p + length]
        p += real
        start = len(img)
        img += bytes((room + 31) // 32 * 32)
        img[start:start + len(data)] = data
        if length >= 2 and rep + replen <= 2:   # modLoad clears a non-looping sample's first word
            img[start:start + 2] = b"\0\0"
    return bytes(img)


ok = 0
for path in mods:
    mod = open(path, "rb").read()
    img = layout(mod)
    words = len(img) // 4
    imgf, outf = os.path.join(scratch, "bank.img"), os.path.join(scratch, "bank.rec")
    open(imgf, "wb").write(img)
    got = subprocess.run([exe, imgf, outf], check=True, capture_output=True, text=True).stdout.split()
    c_rec = open(outf, "rb").read()
    blocks = dsp_bank.encode(img)
    py_rec = b"".join(struct.pack(f"<I{len(b)}I", len(b), *b) for b in blocks)
    ref = list(struct.unpack(f"<{words}I", img))
    same = c_rec == py_rec
    decoded = dsp_bank.decode(blocks, words) == ref
    sum_ok = int(got[2], 16) == dsp_bank.checksum(ref)
    ok += same and decoded and sum_ok
    print(f"{os.path.basename(path)}: {len(img)} bytes of samples, {len(c_rec)} coded ({100 * len(c_rec) / len(img):.1f} %), "
          f"{len(blocks)} blocks; records {'same' if same else 'DIFFER'}, decode {'ok' if decoded else 'WRONG'}, "
          f"checksum {'ok' if sum_ok else 'WRONG'}")
print(f"bank: {ok} of {len(mods)} match")
sys.exit(0 if ok == len(mods) else 1)
