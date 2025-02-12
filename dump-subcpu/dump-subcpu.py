import serial
port = '/dev/ttyUSB0' # change this to what you have
out = 'subcpu-boot-ff7800.rom'
ser = serial.Serial(port, 38400)

data = ser.read(1)
assert ord(data) == 0xfe

#chunk_size = 1
#total_size = 16 # string "KN5000 SOUND RAM"

# chunk_size = 0x100
# total_size = 0x20000 # subcpu boot rom

chunk_size = 0x10
total_size = 0x800
with open(out, 'wb') as f:
	for i in range(int(total_size/chunk_size)):
		print(f'{(i*chunk_size):06X}-{((i+1)*chunk_size - 1):06X}')
		data = ser.read(chunk_size)
		f.write(data)
