# (c) 2021 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# This program loads a compressed firmware update file and
# outputs its raw, uncompressed contents.

import lzss
from binxeledit import (BinxelEdit,
                        reuse_palette)
                       
def kn5000_golden_palette(r, g, b, a, colors, transparent=None):
    if transparent and r==255 and g==255 and b==255:
        return transparent
    else:
        return 0x22 + int(10 * (r + g + b) / (255*3.0))


compressed_file = open("HKMSPRG.SLD.v10", "rb")

# skip the header:
expected_header = b"SLIDE4K\x00\x20\x00\x00"
header = compressed_file.read(len(expected_header))
assert header == expected_header

# and read the rest
compressed_data = compressed_file.read()

# perform patching on decompressed data:
edit = BinxelEdit(raw_data=lzss.decompress(data=compressed_data,
                                           initial_buffer_values=0x00000000))
edit.write_image(filename="HappyHacking.png",
                 transparent=0xF7, pixel_value=kn5000_golden_palette,
                 byte=0x8FFA6, width=312, height=45, bpp=8)

edit.write_image(filename="Sanfona-1.png",
                 transparent=0xF7, pixel_value=reuse_palette,
                 byte=0x86676, width=120, height=95, bpp=8)

edit.write_image(filename="Sanfona-2.png",
                 transparent=0xF7, pixel_value=reuse_palette,
                 byte=0x892FE, width=120, height=95, bpp=8)

raw_data = bytes(edit.raw_data)
open("HKMSPRG.SLD.v10.patched.raw", "wb").write(raw_data)

# write the header and the re-compressed data to a new file:
new_file = open("HKMSPRG.SLD.v10.patched", "wb")
new_file.write(expected_header)
new_file.write(lzss.compress(data=raw_data, initial_buffer_values=0x00000000))
