ASL_PATH=/home/fsanches/devel/github_felipesanches/kn5000_homebrew/asl-current
ASL=$(ASL_PATH)/asl
P2BIN=$(ASL_PATH)/p2bin
MAME_ROMS=/home/fsanches/ROM_DUMPS/FSanches/kn5k-hacks/
MAME=/home/fsanches/mame/technics # This is my custom, partial build of MAME with the KN5000 driver
                                  # See: https://github.com/mamedev/mame/pull/11835

all: another.rom

another.p: another.asm
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

clean:
	rm -f another.rom
	rm -f another.p
	rm -rf cfg
