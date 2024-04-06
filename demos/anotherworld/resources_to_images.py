#!/usr/bin/env python
# More info at:
# - https://docs.google.com/document/d/16cTsiIKNgGmAu4DQSjqs78iasDQ6pHe8tT1biLsCr7g/edit#
# - https://github.com/felipesanches/kn5000_homebrew

import sys
if len(sys.argv) != 3:
    sys.exit(f"usage: {sys.argv[0]} image_resource image_out")

image_resource = sys.argv[1]
image_output = open(sys.argv[2], "r+b")
resource_data = open(image_resource, "rb").read()

for width, height in [(320, 200)]:
    for y in range(height):
        for x in range(width):
            c = 0
            for i in range(4):
                c = c << 1
                if resource_data[i*8000 + int(x/8) + y*40] & (1 << (7 - (x%8))):
                    c |= 1
            image_output.write(bytes([c]))

image_output.close()
