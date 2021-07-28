# (c) 2021 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# This program loads a compressed firmware update file and
# outputs its raw, uncompressed contents.

compressed = open("HKMSPRG.SLD.v10", "rb").read()
raw = open("HKMSPRG.SLD.v10.raw", "wb")

data_buffer = [0] * 4096
read_pointer = 0x0B # skip header bytes: "SLIDE4K", 00 20 00 00
write_pointer = 0x00

def output(data_byte):
  global write_pointer
  global data_buffer
  data_buffer[write_pointer%4096] = data_byte
  raw.write(bytes([data_byte]))
  write_pointer += 1


def fetch_byte():
  global read_pointer
  data = compressed[read_pointer]
  read_pointer += 1
  return data


while read_pointer < len(compressed):
  counter = 0
  compression_map_byte = fetch_byte()
#  print(hex(compression_map_byte))
  while counter < 8:
    is_raw = compression_map_byte & 1
    compression_map_byte >>= 1
    counter += 1

    if is_raw:
      output(fetch_byte())
    else:
      compression_word = fetch_byte()
      compression_word |= fetch_byte() << 8
      chunk_length = 3 + ((compression_word & 0x0F00) >> 8)
      chunk_address = ((compression_word & 0xF000) >> 4) | (compression_word & 0xFF)
#      print(f'chunk_length: {hex(chunk_length)}\tchunk_address: {hex(chunk_address)}')
      while chunk_length > 0:
        output(data_buffer[((chunk_address+18)%4096)])
        chunk_address += 1
        chunk_length -= 1

