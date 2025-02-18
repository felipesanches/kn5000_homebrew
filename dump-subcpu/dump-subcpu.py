import sys
import serial
port = '/dev/ttyUSB0' # change this to what you have
#out = 'new-attempt-subcpu-boot.rom'
ser = serial.Serial(port, 38400)

data = ser.read(1)
assert ord(data) == 0xfe

total_size = 16 # string "KN5000 SOUND RAM"

# total_size = 0x20000 # subcpu boot rom

def read_bytes(addr, num_bytes):
    ser.write(bytes([addr & 0xff]))
    ser.write(bytes([(addr >> 8) & 0xff]))
    ser.write(bytes([(addr >> 16) & 0xff]))
    ser.write(bytes([0x00]))
    ser.write(bytes([num_bytes & 0xff]))
    ser.write(bytes([(num_bytes >> 8) & 0xff]))
    ser.write(bytes([0x01])) # DUMP_IT!
    data = ser.read(num_bytes)
    return reversed(data)

print("===")
print(read_bytes(0x0120E3, 16))
print("===")

sys.exit(0)

chunk_size = 0x10
total_size = 0x800
with open(out, 'wb') as f:
    for i in range(int(total_size/chunk_size)):
        print(f'{(i*chunk_size):06X}-{((i+1)*chunk_size - 1):06X}')
        data = ser.read(chunk_size)
        f.write(data)
