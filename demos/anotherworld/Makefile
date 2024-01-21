ASL_PATH=/home/fsanches/devel/github_felipesanches/kn5000_homebrew/asl-current
ASL=$(ASL_PATH)/asl
P2BIN=$(ASL_PATH)/p2bin
MAME_ROMS=/home/fsanches/ROM_DUMPS/FSanches/kn5k-hacks/
MAME=/home/fsanches/mame/technics # This is my custom, partial build of MAME with the KN5000 driver
                                  # See: https://github.com/mamedev/mame/pull/11835

all: another.rom

another.p: another.asm assets
	$(ASL) another.asm

another.rom: another.p
	$(P2BIN) another.p another.rom

install:
	#echo "Please make a backup of your original hd-ae5000_v2_01i.ic4 ROM, then edit the Makefile's make install rule"
	# uncomment this:
	# ln -s `pwd`/another.rom $(MAME_ROMS)/kn5000/hd-ae5000_v2_01i.ic4
	
run: another.rom
	$(MAME) -rp $(MAME_ROMS) kn5000 -window -extension hdae5000

debug: another.rom
	$(MAME) -rp $(MAME_ROMS) kn5000 -window -debug -extension hdae5000

assets: resources/resource-0x17.bin resources/resource-0x53.bin
	touch intro_palettes.bin
	touch another_world_logo.bin
	touch other_bitmap.bin
	python resources_to_image_and_palette.py resources/resource-0x53.bin another_world_logo.bin resources/resource-0x17.bin intro_palettes.bin
	python resources_to_image_and_palette.py resources/resource-0x49.bin other_bitmap.bin resources/resource-0x17.bin intro_palettes.bin

clean:
	rm -f another.rom
	rm -f another.p
	rm -rf cfg
