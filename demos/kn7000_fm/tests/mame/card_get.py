#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# card_get.py IMAGE FOLDER/NAME OUT -- one file out of a FAT16 SD card image (run_fm.sh KEEP_SD=1 keeps the card
# MAME wrote as OUT/sd_after.img), through technics-roms-disasm kn7000/tools/kn7menu/fatimg.py's reader.
import os
import struct
import sys

KN7 = os.environ.get('KN7', os.path.expanduser('~/compartilhado/technics-roms-disasm/kn7000'))
sys.path.insert(0, os.path.join(KN7, 'tools', 'kn7menu'))
import fatimg  # noqa: E402

img, path, out = sys.argv[1:4]
fs = fatimg.Fat16(img)
cl = 0
parts = path.replace('\\', '/').split('/')
for k, name in enumerate(parts):
    o = fs.find(cl, name)
    if o is None:
        sys.exit(f'card_get: {name} not found')
    first = struct.unpack_from('<H', fs.d, o + 26)[0]
    size = struct.unpack_from('<I', fs.d, o + 28)[0]
    if k < len(parts) - 1:
        cl = first
        continue
    data = b''.join(bytes(fs.d[fs.coff(c):fs.coff(c) + fs.cb]) for c in fs.chain(first))[:size]
    open(out, 'wb').write(data)
    print(f'card_get: {path}, {size} bytes')
