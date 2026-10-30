#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# mkgame.py IN.bin OUT.BIN TABLE -- a program as KN7APP.BIN reads it (src/common/pfhost.h PfGameHeader): checks the
# header's magic, ABI and table, that the image (read to the header's base, +0x2C) and the bss lie where a program may
# be (GAME_BASE to GAME_END, or ARENA2) and do not meet, fills in the size (+8) and the CRC-32 of the bytes from +0x40 to
# the end (+0x28), the way the loader's mkapp.py does for KN7APP.BIN.
import os
import re
import struct
import sys
import zlib

GAME_BASE, GAME_END, ARENA2_LO, ARENA2_HI, MAGIC = 0x502E8000, 0x5031B000, 0x5031C000, 0x5037A000, 0x4D474650
# the ABI the platform expects, as pfhost.h has it
ABI = int(re.search(r'#define PFHOST_ABI (\d+)', open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                '../../src/common/pfhost.h')).read()).group(1))


def in_area(lo, hi):
    return lo <= hi and ((GAME_BASE <= lo and hi <= GAME_END) or (ARENA2_LO <= lo and hi <= ARENA2_HI))


src, out, table = sys.argv[1], sys.argv[2], int(sys.argv[3])
img = bytearray(open(src, 'rb').read())
magic, abi, _, t, bss_lo, bss_hi, init_lo, init_hi, end, entry, _, base = struct.unpack_from('<12I', img, 0)
assert magic == MAGIC and abi == ABI and t == table, f'not table {table}\'s program (magic {magic:#x} abi {abi} table {t})'
assert in_area(base, base + len(img)), f'the image ({base:#x}, {len(img)} bytes) outside the area'
assert in_area(bss_lo, bss_hi) and bss_hi <= end, 'bss or end outside the area'
assert bss_hi <= base or bss_lo >= base + len(img), 'bss over the image'
assert base + 0x40 <= entry < base + len(img), 'entry'
struct.pack_into('<I', img, 8, len(img))
crc = zlib.crc32(bytes(img[0x40:])) & 0xffffffff
struct.pack_into('<I', img, 0x28, crc)
open(out, 'wb').write(img)
print(f'{out}: {len(img)} bytes at {base:#x} ({ARENA2_HI - base - len(img) if base >= ARENA2_LO else GAME_END - base - len(img)} '
      f'bytes left there), bss {bss_hi - bss_lo} at {bss_lo:#x}, entry {entry:#x}, crc32 {crc:08x}')
