from patches.binxeledit import BinxelEdit, reuse_palette

                       
def kn5000_golden_palette(r, g, b, a, colors, transparent=None):
    if transparent and r==255 and g==255 and b==255:
        return transparent
    else:
        return 0x21 + int(0xE * (r + g + b) / (255*3.0))


def replace_images__patch(raw_data):
    # perform patching on decompressed data:
    raw_data = list(raw_data)
    edit = BinxelEdit(raw_data=raw_data)
    edit.write_image(filename="patches/HappyHacking.png",
                     transparent=0xF7, pixel_value=kn5000_golden_palette,
                     byte=0x8FFA6, width=312, height=45, bpp=8)

    #edit.write_image(filename="patches/Sanfona-1.png",
    #                 transparent=0xF7, pixel_value=reuse_palette,
    #                 byte=0x86676, width=120, height=95, bpp=8)

    #edit.write_image(filename="patches/Sanfona-2.png",
    #                 transparent=0xF7, pixel_value=reuse_palette,
    #                 byte=0x892FE, width=120, height=95, bpp=8)
    raw_data = bytes(edit.raw_data)
