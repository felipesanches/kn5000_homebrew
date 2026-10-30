#!/usr/bin/env python3
"""compare_pgm.py PDF PGM_DIR -- each page kn7pdf wrote (PGM_DIR/<n>.pgm, n from 1) against current MuPDF's drawing of
it (PyMuPDF, gray, the same width): per page the mean absolute difference and the share of pixels differing by more
than 64 of 255; writes PGM_DIR/<n>-diff.png (white where they agree). Exit status 1 when a page's share exceeds
--max (percent, default 2)."""
import sys, os, argparse, pymupdf
ap = argparse.ArgumentParser()
ap.add_argument("pdf"); ap.add_argument("dir"); ap.add_argument("--max", type=float, default=2.0)
a = ap.parse_args()
doc = pymupdf.open(a.pdf)
bad = 0
for i, page in enumerate(doc, 1):
    p = os.path.join(a.dir, f"{i}.pgm")
    if not os.path.exists(p): print(f"page {i}: missing"); bad += 1; continue
    old = pymupdf.Pixmap(p)
    z = old.width / page.rect.width
    new = page.get_pixmap(matrix=pymupdf.Matrix(z, z), colorspace=pymupdf.csGRAY, alpha=False)
    w, h = min(old.width, new.width), min(old.height, new.height)
    A, B = old.samples, new.samples
    tot = big = 0; diff = bytearray(w * h)
    for y in range(h):
        ra, rb = A[y * old.stride:y * old.stride + w], B[y * new.stride:y * new.stride + w]
        for x in range(w):
            d = abs(ra[x] - rb[x]); tot += d; big += d > 64; diff[y * w + x] = 255 - d
    pymupdf.Pixmap(pymupdf.csGRAY, w, h, bytes(diff), False).save(os.path.join(a.dir, f"{i}-diff.png"))
    share = 100 * big / (w * h)
    bad += share > a.max
    print(f"page {i}: {old.width}x{old.height} vs {new.width}x{new.height} mean |d| {tot / (w * h):.2f}, >64: {share:.3f}%")
sys.exit(1 if bad else 0)
