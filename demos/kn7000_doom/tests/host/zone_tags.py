#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# zone_tags.py DUMP -- a zone block list (Z_FileDumpHeap, as tests/host/host.c writes it with DOOM_ZONE_DUMP) summed by
# tag: how many blocks and bytes of each (z_zone.h: 1 STATIC, 2 SOUND, 3 MUSIC, 4 FREE, 5 LEVEL, 6 LEVSPEC, 7 PURGELEVEL,
# 8 CACHE).
import re
import sys

NAMES = {1: "STATIC", 2: "SOUND", 3: "MUSIC", 4: "FREE", 5: "LEVEL", 6: "LEVSPEC", 7: "PURGELEVEL", 8: "CACHE"}
blocks, sizes = {}, {}
for line in open(sys.argv[1]):
    m = re.match(r"block:\S+\s+size:\s*(\d+)\s+user:\S+\s+tag:\s*(\d+)", line)
    if m:
        size, tag = int(m.group(1)), int(m.group(2))
        blocks[tag] = blocks.get(tag, 0) + 1
        sizes[tag] = sizes.get(tag, 0) + size
print("| tag        | blocks | bytes   |")
print("|------------|--------|---------|")
for tag in sorted(sizes):
    print(f"| {NAMES.get(tag, tag):<10} | {blocks[tag]:>6} | {sizes[tag]:>7} |")
