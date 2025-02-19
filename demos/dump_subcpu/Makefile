ASL_PATH=/home/fsanches/devel/github_felipesanches/kn5000_homebrew/asl-current
ASL=$(ASL_PATH)/asl
P2BIN=$(ASL_PATH)/p2bin

all: subcpu_send.rom subcpu_receive.rom

subcpu_send.p: subcpu_send.asm
	$(ASL) subcpu_send.asm -l

subcpu_receive.p: subcpu_receive.asm
	$(ASL) subcpu_receive.asm -l

subcpu_send.rom: subcpu_send.p
	$(P2BIN) subcpu_send.p subcpu_send.rom

subcpu_receive.rom: subcpu_receive.p
	$(P2BIN) subcpu_receive.p subcpu_receive.rom

clean:
	rm -f subcpu_send.rom
	rm -f subcpu_receive.rom
	rm -f subcpu_send.p
	rm -f subcpu_receive.p
