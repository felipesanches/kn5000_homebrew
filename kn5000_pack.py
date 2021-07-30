# (c) 2021 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# This program loads a raw firmware update file and compresses it.

import lzss

raw = open("HKMSPRG.SLD.v10.raw", "rb").read()
compressed = open("HKMSPRG.SLD.v10.compressed", "wb")

# write the header:
compressed.write(b"SLIDE4K\x00\x20\x00\x00")

# and the compressed data
compressed.write(lzss.compress(data=raw, initial_buffer_values=0x00000020))
