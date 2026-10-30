#!/usr/bin/env python3
"""compare_modern.py PDF RENDER_DIR -- draw each page with current MuPDF (PyMuPDF) gray at the width of the PNGs
mudraw wrote (RENDER_DIR/<name>-<n>.png) and print, per page, the mean absolute difference and the share of pixels
differing by more than 64 (of 255). Writes RENDER_DIR/<name>-<n>-modern.png and -diff.png."""
import sys, os, pymupdf
pdf, d = sys.argv[1], sys.argv[2]
name = os.path.splitext(os.path.basename(pdf))[0]
doc = pymupdf.open(pdf)
for i, page in enumerate(doc, 1):
    old = pymupdf.Pixmap(os.path.join(d, f"{name}-{i}.png"))
    if old.n != 1: old = pymupdf.Pixmap(pymupdf.csGRAY, old)
    z = old.width / page.rect.width
    new = page.get_pixmap(matrix=pymupdf.Matrix(z, z), colorspace=pymupdf.csGRAY, alpha=False)
    new.save(os.path.join(d, f"{name}-{i}-modern.png"))
    w, h = min(old.width, new.width), min(old.height, new.height)
    a, b = old.samples, new.samples
    tot = big = 0
    diff = bytearray(w * h)
    for y in range(h):
        ra, rb = a[y * old.stride:y * old.stride + w], b[y * new.stride:y * new.stride + w]
        for x in range(w):
            dd = abs(ra[x] - rb[x]); tot += dd; big += dd > 64; diff[y * w + x] = 255 - dd
    pymupdf.Pixmap(pymupdf.csGRAY, w, h, bytes(diff), False).save(os.path.join(d, f"{name}-{i}-diff.png"))
    print(f"{name} p{i}: {old.width}x{old.height} vs {new.width}x{new.height} mean |d| {tot / (w * h):.2f}, >64: {100 * big / (w * h):.3f}%")
