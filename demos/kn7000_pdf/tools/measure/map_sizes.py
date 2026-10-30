#!/usr/bin/env python3
"""map_sizes.py LINK.map [N] -- bytes of .text/.rodata/.data each input object keeps after --gc-sections."""
import re, sys, collections
s = collections.Counter(); m = False; pend = None
for line in open(sys.argv[1]):
    if line.startswith('Linker script and memory map'): m = True
    if not m: continue
    t = line.split()
    if pend and len(t) >= 3 and t[0].startswith('0x'):
        s[pend] += int(t[1], 16); pend = None; continue
    pend = None
    if t and re.match(r'\.(text|rodata|data)', t[0]) and line.startswith(' .'):
        if len(t) >= 4: s[t[3].split('/')[-1]] += int(t[2], 16)
        elif len(t) == 1: pend = None; pend = '?'
        if len(t) == 1: pend = 'NEXT'
for k, v in s.most_common(int(sys.argv[2]) if len(sys.argv) > 2 else 50): print(v, k)
print(sum(s.values()), 'total')
