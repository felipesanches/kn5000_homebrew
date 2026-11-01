#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU Affero General Public License v3 or later
#
# pgc2png.py [-o DIR] FILE_OR_FOLDER... -- the viewer's page files (Kxxxxnnn.PGC, beside the PDFs in the card's PDF\
# folder) as PNG images, one a page, named after the PDF and the page (BACHIANI-p001.png), beside each file or in DIR.
# A folder means its *.PGC files. Python 3 alone (no other modules), so it runs on any computer the card is put in:
#   python3 pgc2png.py -o ~/pages /run/media/$USER/KN7000_MAME/PDF
# Prints each file's header: the PDF it is from, page, size, and whether memory ran short while it was drawn.
#
# The format (src/kn7/pagecache.c): a 40-byte little-endian header (magic "KPG1", version, width, page from 0, rows,
# coded bytes, the PDF's size, MuPDF's anti-aliasing bits, flags, the PDF's name in 16 bytes), then each row of
# width / 2 bytes PackBits-coded on its own. A row is as the LCD shows it: 4 bits a pixel, the left pixel in bits 7-4,
# 0 black to 15 white -- which is a 4-bit grayscale PNG's row, so rows go into the PNG as they are.
import glob
import os
import struct
import sys
import zlib

HEADER = struct.Struct('<IHHHHIIHH16s')  # 40 bytes
MAGIC = 0x3147504B                       # "KPG1"


def unpack_row(data, at, n):
    """One PackBits-coded row of n bytes at data[at:] -> (row, the offset after it)."""
    out = bytearray()
    while len(out) < n:
        c = data[at]
        at += 1
        if c < 128:
            out += data[at:at + c + 1]
            at += c + 1
        elif c > 128:
            out += bytes([data[at]]) * (257 - c)
            at += 1
    if len(out) != n:
        raise ValueError(f'a row came out {len(out)} bytes, not {n}')
    return bytes(out), at


def png(width, rows, depth=4):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    raw = b''.join(b'\0' + r for r in rows)
    ihdr = struct.pack('>IIBBBBB', width, len(rows), depth, 0, 0, 0, 0)  # grayscale
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


def convert(path, outdir):
    data = open(path, 'rb').read()
    magic, version, width, page, nrows, nbytes, docbytes, aa, flags, doc = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError('not a page file (no "KPG1")')
    if HEADER.size + nbytes != len(data):
        raise ValueError(f'the header says {HEADER.size + nbytes} bytes, the file has {len(data)}')
    doc = doc.split(b'\0')[0].decode('latin-1')
    rows, at = [], HEADER.size
    for _ in range(nrows):
        r, at = unpack_row(data, at, width // 2)
        rows.append(r)
    name = f'{os.path.splitext(doc)[0] or "page"}-p{page + 1:03d}.png'
    out = os.path.join(outdir or os.path.dirname(path) or '.', name)
    open(out, 'wb').write(png(width, rows))
    short = ', memory ran short while it was drawn' if flags & 1 else ''
    print(f'{path}: {doc} ({docbytes} bytes) page {page + 1}, {width}x{nrows}, version {version}, '
          f'aa {aa}{short} -> {out}')


def main(args):
    outdir = None
    if args[:1] == ['-o']:
        outdir, args = args[1], args[2:]
        os.makedirs(outdir, exist_ok=True)
    if not args:
        sys.exit(__doc__ or 'pgc2png.py [-o DIR] FILE_OR_FOLDER...')
    files = []
    for a in args:
        if os.path.isdir(a):
            files += sorted(f for f in glob.glob(os.path.join(a, '*')) if f.upper().endswith('.PGC'))
        else:
            files.append(a)
    bad = 0
    for f in files:
        try:
            convert(f, outdir)
        except (ValueError, IndexError, struct.error) as e:
            print(f'{f}: {e}', file=sys.stderr)
            bad += 1
    print(f'pgc2png: {len(files) - bad} of {len(files)} page files converted')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main(sys.argv[1:])
