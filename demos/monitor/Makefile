ASL_PATH=/home/fsanches/devel/github_felipesanches/kn5000_homebrew/asl-current
ASL=$(ASL_PATH)/asl
P2BIN=$(ASL_PATH)/p2bin
MAME_ROMS=/home/fsanches/ROM_DUMPS/FSanches/kn5k-hacks/
MAME=/home/fsanches/mame/technics # This is my custom, partial build of MAME with the KN5000 driver
                                  # See: https://github.com/mamedev/mame/pull/11835
ARTWORK=/home/fsanches/devel/github_felipesanches/KN5000_MAME_artwork/

all: monitor.rom

monitor.p: monitor.asm
	$(ASL) monitor.asm -l

monitor.rom: monitor.p
	$(P2BIN) monitor.p monitor.rom
	
run: monitor.rom
	$(MAME) -rp $(MAME_ROMS) kn5000 -artpath $(ARTWORK) -window -ui_active

debug: monitor.rom
	$(MAME) -rp $(MAME_ROMS) kn5000 -artpath $(ARTWORK) -window -ui_active -debug

clean:
	rm -f monitor.rom
	rm -f monitor.p
	rm -rf cfg
