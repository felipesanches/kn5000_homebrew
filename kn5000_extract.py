# (c) 2021 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# This program loads a compressed firmware update file and
# outputs its raw, uncompressed contents.

import lzss

compressed_file = open("HKMSPRG.SLD.v10", "rb")
raw_file = open("HKMSPRG.SLD.v10.raw", "wb")

# skip the header:
expected_header = b"SLIDE4K\x00\x20\x00\x00"
header = compressed_file.read(len(expected_header))
assert header == expected_header

# and read the rest
compressed_data = compressed_file.read()

# decompress the data and save it
raw_file.write(lzss.decompress(data=compressed_data, initial_buffer_values=0x00000020))



compressed_file = open("TECHNICS.PRP", "rb")
raw_file = open("TECHNICS.PRP.raw", "wb")

compressed_data = compressed_file.read()

# decompress the data and save it
raw_file.write(lzss.decompress(data=compressed_data, initial_buffer_values=0x00000000))


compressed_file = open("DUMMY.1", "rb")
raw_file = open("DUMMY.1.raw", "wb")

compressed_data = compressed_file.read()

# decompress the data and save it
raw_file.write(lzss.decompress(data=compressed_data, initial_buffer_values=0x00000000))

