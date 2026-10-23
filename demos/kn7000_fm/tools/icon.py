#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# icon.py OUT.png -- KN7FM's 32x32 icon for the program menu (technics-roms-disasm kn7000/tools/kn7menu png2icon.py
# turns it into ICON.BMP): two operator stacks on an output line, the modulators grey, the carriers blue, a
# feedback loop, as the editor draws its diagrams.
import sys

from PIL import Image, ImageDraw

img = Image.new('RGB', (32, 32), (0, 0, 0))
d = ImageDraw.Draw(img)
white, grey, blue, yellow = (230, 230, 230), (90, 90, 90), (0, 90, 170), (255, 220, 0)
for x in (5, 19):                       # two stacks: carrier at the bottom, two modulators above
    for row, c in ((0, grey), (1, grey), (2, blue)):
        y = 3 + row * 9
        d.rectangle([x, y, x + 8, y + 5], fill=c, outline=white)
        if row < 2:
            d.line([x + 4, y + 6, x + 4, y + 8], fill=white)
    d.line([x + 4, 24, x + 4, 26], fill=white)
d.line([9, 26, 23, 26], fill=white)     # the output
d.line([16, 26, 16, 29], fill=white)
d.line([28, 2, 28, 9], fill=yellow)     # feedback on the top right operator
d.line([23, 1, 28, 1], fill=yellow)
d.line([23, 9, 28, 9], fill=yellow)
img.save(sys.argv[1])
