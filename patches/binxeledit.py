# (c) 2021 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later

def grayscale(r, g, b, a, colors=None, transparent=None):
    if transparent and r==255 and g==255 and b==255:
        return transparent
    else:
        return int((r + g + b) / 3.0)

def reuse_palette(r, g, b, a, colors, transparent=None):
    if transparent and r==255 and g==255 and b==255:
        return transparent
    else:
        index = int(((r + g + b) / 3.0))
        return colors[index%4]
#        if index < len(colors):
#            return colors[index%4]
#        else:
#            return index


class BinxelEdit():
    def __init__(self, raw_data):
        self.raw_data = list(raw_data)

    def write_image(self, filename=None, byte=0, bit=0, width=1, height=1, bpp=8, pixel_value=grayscale, transparent=None):
        if bpp != 8:
            sys.exit("FIXME: Only BPP=8 is currently implemented.")

        if bit != 0:
            sys.exit("FIXME: Only bit=0 is currently implemented.")

        if not filename:
            sys.exit("ERROR: You must specify an image filename when"
                     " invoking the `BinxelEdit.write_image` method.")

        from PIL import Image
        im = Image.open(filename)
        pixel_data = im.load()
        im_width, im_height = im.size
        if im_width < width:
            width = im_width
            print(f"WARNING: {filename} width ({im_width}) is less than {width}")

        if im_height < height:
            height = im_height
            print(f"WARNING: {filename} height ({im_height}) is less than {height}")

        addr = byte
        colors = set()
        for y in range(height):
            for x in range(width):
                colors.add(self.raw_data[addr])
                addr += 1
        colors = list(colors)
        print (f'There are {len(colors)} colors: {list(map(hex, colors))}')

        addr = byte
        for y in range(height):
            for x in range(width):
                r, g, b, a = pixel_data[x, y]
                self.raw_data[addr] = pixel_value(r, g, b, a, colors, transparent)
                addr += 1

