# (c) 2021 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# This program loads a compressed firmware update file and
# outputs its raw, uncompressed contents.
#
# https://archive.org/details/technics-kn5000-system-update-disks

import lzss
for version in [7, 8, 9, 10]:
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

