#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# mkimg.py SNAPSHOT.png OUT.png -- a manual picture from a MAME snapshot of tests/mame/keyfall_test.lua (snap/lcd-N.png,
# the 1000 x 750 artwork, the LCD drawn 1:1 at 180,77): the LCD's 640 x 240 cut out and doubled, dot for dot.
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert('RGB')
assert im.size == (1000, 750), f'{sys.argv[1]}: {im.size}, not the 1000 x 750 artwork'
lcd = im.crop((180, 77, 180 + 640, 77 + 240))
lcd.resize((1280, 480), Image.NEAREST).save(sys.argv[2])
