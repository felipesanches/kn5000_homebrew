#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# offsets.py DUMP -- the constants of tests/mame/offsets.c, from `objdump -s` of its object (each in a section
# .rodata.NAME of its own, 4 bytes little-endian), as lines "NAME VALUE" for doom_test.lua.
import re
import sys

name = None
for line in open(sys.argv[1]):
    m = re.match(r'Contents of section \.rodata\.(\w+):', line)
    if m:
        name = m.group(1)
        continue
    m = re.match(r'\s*0000 ([0-9a-f]{8})', line)
    if m and name:
        print(name, int.from_bytes(bytes.fromhex(m.group(1)), 'little'))
        name = None
