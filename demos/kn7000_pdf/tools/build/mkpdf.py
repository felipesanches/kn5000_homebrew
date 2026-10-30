#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU Affero General Public License v3 or later
#
# mkpdf.py IN.elf OUT.BIN -- PDF.BIN as KN7APP.BIN reads it (src/common/pfhost.h, ABI 14): the main image (the ELF's
# loaded bytes from the header's base, in ARENA2) and after it, in the file, each other region's loaded bytes as a
# segment (src/kn7/pdf.ld). Fills in the segment table after the header (address, bytes, CRC-32 of each), the header's
# segment count (reserved[0], +0x30), its size (+8) and the CRC-32 of the main image from +0x40 to its end (+0x28), as
# mkgame.py does for one image. Checks the magic, the ABI, the table, and that the image, the segments and the bss lie in
# the regions KN7APP hands over (kn7.h) and do not meet.
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ABI = int(re.search(r'#define PFHOST_ABI (\d+)', open(os.path.join(HERE, '../../src/common/pfhost.h')).read()).group(1))
TABLE = int(re.search(r'#define PDF_PROGRAM (0x[0-9a-fA-F]+)', open(os.path.join(HERE, '../../src/common/pfhost.h')).read()).group(1), 16)
MAX_SEGMENTS = int(re.search(r'#define PF_MAX_SEGMENTS (\d+)', open(os.path.join(HERE, '../../src/common/pfhost.h')).read()).group(1))
MAGIC = 0x4D474650
# the regions (kn7.h) a program may be in, on programs 893 and 941
REGIONS = {'ARENA2': (0x5031C000, 0x5037A000), 'GAME': (0x502E8000, 0x5031B000), 'ARENA4': (0x503BC000, 0x503FF000),
           'ARENA1': (0x502C8000, 0x502E7F00), 'ARENA3': (0x50298000, 0x502B7400), 'ARENA5': (0x50123000, 0x50148000),
           'ARENA6': (0x50175000, 0x50180000), 'ARENA7': (0x4C03DF20, 0x4C03FFF0)}


def region_of(lo, hi):
    for name, (a, b) in REGIONS.items():
        if a <= lo and hi <= b:
            return name
    return None


def sections(elf):
    """(name, addr, bytes or None for NOBITS) of each allocated section"""
    assert elf[:4] == b'\x7fELF' and elf[4] == 1 and elf[5] == 1, 'not a 32-bit little-endian ELF'
    shoff, = struct.unpack_from('<I', elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', elf, 0x2E)
    hdrs = [struct.unpack_from('<10I', elf, shoff + i * shentsize) for i in range(shnum)]
    strtab = hdrs[shstrndx]
    out = []
    for name, typ, flags, addr, off, size, *_ in hdrs:
        if not flags & 2 or size == 0:  # SHF_ALLOC
            continue
        n = elf[strtab[4] + name:elf.index(b'\0', strtab[4] + name)].decode()
        out.append((n, addr, None if typ == 8 else elf[off:off + size]))  # SHT_NOBITS
    return out


src, out = sys.argv[1], sys.argv[2]
secs = sections(open(src, 'rb').read())
loaded = [(a, d) for n, a, d in secs if d is not None]
header = next(d for n, a, d in secs if n == '.header')
magic, abi, _, table, bss_lo, bss_hi, init_lo, init_hi, end, entry, _, base = struct.unpack_from('<12I', header, 0)
assert magic == MAGIC and abi == ABI and table == TABLE, f'not PDF.BIN (magic {magic:#x} abi {abi} table {table:#x})'
assert region_of(bss_lo, bss_hi), f'bss [{bss_lo:#x}, {bss_hi:#x}) outside the regions'
# each region's loaded bytes, from its first loaded section to its last (gaps zero)
by_region = {}
for a, d in loaded:
    r = region_of(a, a + len(d))
    assert r, f'section at {a:#x} ({len(d)} bytes) outside the regions'
    by_region.setdefault(r, []).append((a, d))
images = {}
for r, parts in by_region.items():
    lo = min(a for a, d in parts)
    hi = max(a + len(d) for a, d in parts)
    img = bytearray(hi - lo)
    for a, d in parts:
        img[a - lo:a - lo + len(d)] = d
    images[r] = (lo, img)
    assert not (lo < bss_hi and bss_lo < hi), f'{r} over the bss'
main_lo, main = images.pop(region_of(base, base + 0x40))
assert main_lo == base, f'the main image starts at {main_lo:#x}, not at the header ({base:#x})'
assert base + 0x40 <= entry < base + len(main) or any(lo <= entry < lo + len(i) for lo, i in images.values()), 'entry'
segs = sorted(images.items(), key=lambda kv: kv[1][0])
assert len(segs) <= MAX_SEGMENTS, f'{len(segs)} segments, the table holds {MAX_SEGMENTS}'
for i, (r, (lo, img)) in enumerate(segs):
    struct.pack_into('<4I', main, 0x40 + 16 * i, lo, len(img), zlib.crc32(bytes(img)) & 0xffffffff, 0)
struct.pack_into('<I', main, 0x30, len(segs))
struct.pack_into('<I', main, 8, len(main))
crc = zlib.crc32(bytes(main[0x40:])) & 0xffffffff
struct.pack_into('<I', main, 0x28, crc)
with open(out, 'wb') as f:
    f.write(main)
    for r, (lo, img) in segs:
        f.write(img)
a2 = REGIONS['ARENA2']
print(f'{out}: main image {len(main)} bytes at {base:#x} ({a2[1] - base - len(main)} left in ARENA2), crc32 {crc:08x}')
for r, (lo, img) in segs:
    print(f'  segment {r}: {len(img)} bytes at {lo:#x} ({REGIONS[r][1] - lo - len(img)} left there)')
print(f'  bss {bss_hi - bss_lo} bytes at {bss_lo:#x}; entry {entry:#x}')
