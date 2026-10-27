#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# mkgame.py IN.bin OUT.BIN TABLE -- a table program as KN7APP.BIN reads it (src/common/pfhost.h PfGameHeader):
# checks the header's magic, ABI, table and link address, fills in the size (+8) and the CRC-32 of the bytes from
# +0x40 to the end (+0x28), the way the loader's mkapp.py does for KN7APP.BIN.
import os
import re
import struct
import sys
import zlib

GAME_BASE, GAME_END, MAGIC = 0x502E8000, 0x5031B000, 0x4D474650
# the ABI the platform expects, as pfhost.h has it
ABI = int(re.search(r'#define PFHOST_ABI (\d+)', open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                '../../src/common/pfhost.h')).read()).group(1))
src, out, table = sys.argv[1], sys.argv[2], int(sys.argv[3])
img = bytearray(open(src, 'rb').read())
magic, abi, _, t, bss_lo, bss_hi, init_lo, init_hi, end, entry = struct.unpack_from('<10I', img, 0)
assert magic == MAGIC and abi == ABI and t == table, f'not table {table}\'s program (magic {magic:#x} abi {abi} table {t})'
assert GAME_BASE + len(img) <= bss_lo <= bss_hi <= end <= GAME_END, 'bss or end outside the area'
assert GAME_BASE + 0x40 <= entry < GAME_BASE + len(img), 'entry'
struct.pack_into('<I', img, 8, len(img))
crc = zlib.crc32(bytes(img[0x40:])) & 0xffffffff
struct.pack_into('<I', img, 0x28, crc)
open(out, 'wb').write(img)
print(f'{out}: {len(img)} bytes, bss {bss_hi - bss_lo}, ends {end:#x} ({GAME_END - end} bytes left), '
      f'entry {entry:#x}, crc32 {crc:08x}')
