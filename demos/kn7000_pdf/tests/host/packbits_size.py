#!/usr/bin/env python3
# packbits_size.py PGM... -- the size of each page as the KN7000 viewer's page cache would keep it: 4 bits a pixel
# (gray >> 4, two pixels a byte), each row of 320 bytes PackBits-coded on its own; prints bytes per page.
import sys
def pgm(p):
    d = open(p, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split()); return w, h, parts[3]
def packbits(row):
    out, i, n = 0, 0, len(row)
    while i < n:
        j = i
        while j + 1 < n and row[j + 1] == row[i] and j - i < 127: j += 1
        if j > i: out += 2; i = j + 1; continue
        j = i
        while j < n and j - i < 128 and not (j + 1 < n and row[j + 1] == row[j]): j += 1
        out += 1 + (j - i); i = j
    return out
tot = 0
for p in sys.argv[1:]:
    w, h, px = pgm(p)
    size = 0
    for y in range(h):
        r = px[y * w:(y + 1) * w]
        row = bytes(((r[2 * x] >> 4) << 4) | (r[2 * x + 1] >> 4) for x in range(w // 2))
        size += packbits(row)
    tot += size
    print(f'{p}: {w}x{h}, {size} bytes ({size * 100 // (w * h // 2)}% of {w * h // 2})')
print('total', tot)
