# (c) 2021,2024 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# WARNING! Below is the original, slightly outdated description of this file:
# ------------------------------------------------------------------------------------
# This program loads a compressed firmware update file and
# outputs both its raw, uncompressed contents as well as a recompressed file.
# ------------------------------------------------------------------------------------
#
# FIXME: Even though this is a patching script, it ended up working as an
#        extraction script as well. The extraction code should move to a dedicated
#        file and then be imported here. Similar refactoring must be done
#        to the encoding code.

patch_images = False
patch_trojan = False
dump_address = 0x00e00000 # to try dumping the first 0xa80 bytes of the Program ROM

import lzss
from binxeledit import (BinxelEdit,
                        reuse_palette)
                       
def kn5000_golden_palette(r, g, b, a, colors, transparent=None):
    if transparent and r==255 and g==255 and b==255:
        return transparent
    else:
        return 0x21 + int(0xE * (r + g + b) / (255*3.0))


for version in ["5", "6", "7", "8", "9", "10"]:
    compressed_file = open(f"HKMSPRG.SLD.v{version}", "rb")

    # skip the header:
    expected_header1 = b"SLIDE4K\x00\x20\x00\x00"
    header = compressed_file.read(len(expected_header1))
    assert header == expected_header1

    # FIXME: Instead of hardcoding these "offset_second_part" values here,
    # we should seach for the occurrence of the "SLIDE" marker on the update file.

    #if version == "4":
    #    offset_second_part = 0x
    #    subversion = "139"
    if version == "5":
        offset_second_part = 0xEB33E
        subversion = "140"
    elif version == "6":
        offset_second_part = 0xEB576
        subversion = "140"
    elif version == "7":
        offset_second_part = 0xEB898
        subversion = "141"
    elif version == "8":
        offset_second_part = 0xEBBA0
        subversion = "141"
    elif version == "9":
        offset_second_part = 0xEBBA0
        subversion = "142"
    elif version == "10":
        offset_second_part = 0xEBBA9
        subversion = "142"

    # and read the rest
    compressed_data = compressed_file.read(offset_second_part - len(expected_header1))

    raw_data = lzss.decompress(data=compressed_data, initial_buffer_values=0x00000000)

    expected_header2 = b"SLIDE4K\x00\x03\x00\x00"
    header = compressed_file.read(len(expected_header2))
    assert header == expected_header2

    compressed_data2 = compressed_file.read()
    raw_data2 = lzss.decompress(data=compressed_data2, initial_buffer_values=0x00000000)

# ------------------------------------------------------------------------------------
    # This is still experimental:
    ptr = 0x0016F330
    if patch_trojan and version == "10":
        raw_data = list(raw_data)
        raw_data[ptr + 0] = (dump_address >> 0) & 0xff
        raw_data[ptr + 1] = (dump_address >> 8) & 0xff
        raw_data[ptr + 2] = (dump_address >> 16) & 0xff
        raw_data[ptr + 3] = (dump_address >> 24) & 0xff
        raw_data = bytes(raw_data)
# ------------------------------------------------------------------------------------

    if patch_images and version == "10":
        # perform patching on decompressed data:
        raw_data = list(raw_data)
        edit = BinxelEdit(raw_data=raw_data)
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

    if patch_trojan and version == "10":
        assert list(raw_data)[ptr + 0] == ((dump_address >> 0) & 0xff)
        assert list(raw_data)[ptr + 1] == ((dump_address >> 8) & 0xff)
        assert list(raw_data)[ptr + 2] == ((dump_address >> 16) & 0xff)
        assert list(raw_data)[ptr + 3] == ((dump_address >> 24) & 0xff)

    open(f"kn5000_v{version}_program.rom", "wb").write(raw_data)
    open(f"kn5000_subprogram_v{subversion}.com", "wb").write(raw_data2)

    # write the header and the re-compressed data to a new file:
    data = lzss.compress(data=raw_data, initial_buffer_values=0x00000000)
    data2 = lzss.compress(data=raw_data2, initial_buffer_values=0x00000000)

    new_file = open(f"HKMSPRG.SLD.v{version}.patched", "wb")
    new_file.write(expected_header1)
    new_file.write(data)
    new_file.write(expected_header2)
    new_file.write(data2)
    new_file.close()

