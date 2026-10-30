#!/usr/bin/env python3
"""pgmdiff.py A.pgm B.pgm [MAX_PERCENT=0.1] -- exit 0 when the two gray pictures have the same size and at most
MAX_PERCENT of their pixels differ by more than 32 (of 255); prints that share."""
import sys


def read(p):
    d = open(p, 'rb').read()
    parts, i = [], 0
    while len(parts) < 4:
        while d[i:i + 1].isspace(): i += 1
        j = i
        while not d[j:j + 1].isspace(): j += 1
        parts.append(d[i:j]); i = j
    assert parts[0] == b'P5'
    w, h = int(parts[1]), int(parts[2])
    return w, h, d[i + 1:i + 1 + w * h]


wa, ha, a = read(sys.argv[1])
wb, hb, b = read(sys.argv[2])
limit = float(sys.argv[3]) if len(sys.argv) > 3 else 0.1
if (wa, ha) != (wb, hb):
    print(f'size {wa}x{ha} vs {wb}x{hb}'); sys.exit(1)
n = sum(1 for x, y in zip(a, b) if abs(x - y) > 32)
share = 100 * n / (wa * ha)
print(f'{share:.4f}%')
sys.exit(0 if share <= limit else 1)
