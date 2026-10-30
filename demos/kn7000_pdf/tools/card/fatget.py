#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU Affero General Public License v3 or later
#
# fatget.py IMAGE PATH [OUT] -- a file out of a FAT16 card image (kn7menu's fatimg.py reads the image; PATH as the
# KN7000 names it, PDF\PDFLOG.TXT): to OUT, or to stdout. For reading what the viewer wrote on MAME's card
# (tests/mame/run_pdf.sh with KEEP_SD=1 keeps OUT/sd_after.img).
import os
import struct
import sys

sys.path.insert(0, os.path.expanduser('~/compartilhado/technics-roms-disasm/kn7000/tools/kn7menu'))
from fatimg import Fat16  # noqa: E402

img, path = sys.argv[1], sys.argv[2]
f = Fat16(img)
cl = 0
parts = path.split('\\')
for part in parts[:-1]:
    cl = struct.unpack_from('<H', f.d, f.find(cl, part) + 26)[0]
o = f.find(cl, parts[-1])
first, size = struct.unpack_from('<HI', f.d, o + 26)
data = b''.join(bytes(f.d[f.coff(c):f.coff(c) + f.cb]) for c in f.chain(first))[:size]
if len(sys.argv) > 3:
    open(sys.argv[3], 'wb').write(data)
else:
    sys.stdout.buffer.write(data)
