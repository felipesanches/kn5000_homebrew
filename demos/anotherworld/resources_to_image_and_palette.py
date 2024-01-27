#!/usr/bin/env python
# More info at:
# - https://docs.google.com/document/d/16cTsiIKNgGmAu4DQSjqs78iasDQ6pHe8tT1biLsCr7g/edit#
# - https://github.com/felipesanches/kn5000_homebrew

from PIL import Image, ImageDraw

import sys
if len(sys.argv) != 5:
    sys.exit(f"usage: {sys.argv[0]} image_resource image_out palette_resource palette_out")

image_resource = sys.argv[1]
image_output = open(sys.argv[2], "r+b")
palette_resource = sys.argv[3]
palette_output = open(sys.argv[4], "r+b")

palette_data = open(palette_resource, "rb").read()
resource_data = open(image_resource, "rb").read()

print (len(palette_data))
assert len(palette_data) == 2048
def get_pal_value():
    global pal_addr
    value = palette_data[pal_addr]
    pal_addr += 1
    return value

NUM_COLORS = 16
palette = []
for paletteId in range(64):
    pal_addr = paletteId * 2*NUM_COLORS
    for c in range(NUM_COLORS):
        c1 = get_pal_value()
        c2 = get_pal_value()
        r = ((c1 & 0x0F) << 2) | ((c1 & 0x0F) >> 2)
        g = ((c2 & 0xF0) >> 2) | ((c2 & 0xF0) >> 6)
        b = ((c2 & 0x0F) >> 2) | ((c2 & 0x0F) << 2)
        palette_output.write(bytes([r, g, b]))
        if paletteId==0x0E:
            palette.append((r, g, b, 255))

for width, height in [(320, 200)]:
    image = Image.new("RGB",[width,height])
    d = ImageDraw.Draw(image)
    for y in range(height):
        for x in range(width):
            c = 0
            for i in range(4):
                c = c << 1
                if resource_data[i*8000 + int(x/8) + y*40] & (1 << (7 - (x%8))):
                    c |= 1
            d.point([x, y], palette[c])
            image_output.write(bytes([c]))

    #image.show()

image_output.close()
palette_output.close()
