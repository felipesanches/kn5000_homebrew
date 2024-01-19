# (c) 2023,2024 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# WARNING! Do not use this script! It needs to be refactored and updated.
# The actual decoding and re-encoding algorythms that are working properly
# are currently available on the kn5000_patch.py script
#
# I'll soon reorganize this mess.
#
#
# Below is the original, slightly outdated description of this file:
# ------------------------------------------------------------------------------------
# This program loads a compressed firmware update file and
# outputs its raw, uncompressed contents.
# ------------------------------------------------------------------------------------
#
# Technics KN5000 System Update Disks:
# https://archive.org/details/technics-kn5000-system-update-disks

import sys
print("This is broken. For now, take a look at kn5000_patch.py")
sys.exit(-1)

# ------------------------------------------------------------------------------------
import lzss

for version in [5]:#, 6, 7, 8, 9, 10]:
    # TODO: automatically download from:
    #    (f'https://archive.org/download/technics-kn5000-system-update-disks/'
    #     f'Technics%20KN5000%20System%20Update%20Disks/'
    #     f'03-Extracted_contents_of_the_disks/kn5000_v{version}_disk/HKMSPRG.SLD')

    compressed_file = open(f"HKMSPRG.SLD.v{version}.compressed", "rb")
    raw_file = open(f"HKMSPRG.SLD.v{version}.raw", "wb")

    # skip the header:
    expected_header = b"SLIDE4K\x00\x20\x00\x00"
    header = compressed_file.read(len(expected_header))
    assert header == expected_header

    # and read the rest
    compressed_data = compressed_file.read()

    # decompress the data and save it
    raw_file.write(lzss.decompress(data=compressed_data)) #, initial_buffer_values=0x00000000))

    # TODO: assert the version number shows up at extracted ROM address 0xFFFFE8

    # TODO: Split the decompressed data into two:
    #       The first 2Mbytes is the PROGRAM ROM
    #       I am not sure yet what is the rest.

