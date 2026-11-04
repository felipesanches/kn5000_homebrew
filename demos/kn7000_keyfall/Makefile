# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# Keyfall for the KN7000 (README.md): a piano game, notes falling onto a keyboard on the LCD, played on the keybed, run
# by the KN7000 program loader (KN7LOAD.CNV) from the program menu's folder KEYFALL\.
#   make             $(OUT)/KN7APP.BIN (the platform) and $(OUT)/KEYFALL.BIN (the game)
#   make songs       songs/*.MID from songs/*.txt (tools/songs/mksong.py)
#   make card-dir    $(OUT)/card/KEYFALL/: those, MANIFEST.TXT and the songs
#   make host        $(OUT)/host/kfcheck: the game's song reading and judging on the PC (tests/host/)
#   make check       kfcheck over every song (tests/host/README.md)
# Needs the mn10300-elf toolchain the Pinball port builds (Pinball kn7000/toolchain/build.sh: GCC patched to make no
# (d32,An) operand) and the KN7000 loader's tools (technics-roms-disasm kn7000/tools/cnv_romdump: its font, mkapp.py).
TOOLS   ?= $(HOME)/compartilhado/tools/mn10300-elf-kn7000/bin
CROSS   := $(TOOLS)/mn10300-elf-
KN7     ?= $(HOME)/compartilhado/technics-roms-disasm/kn7000
LOADER  := $(KN7)/tools/cnv_romdump
OUT     ?= build
PYTHON  ?= python3
# how long KN7APP's converter calls rest before they hand the firmware's main task back (src/app/entry.c rest)
REST_MS ?= 100
# ... and while a song plays (src/game/play.c kf_rest, through PfHost::restMs)
PLAY_REST_MS ?= 10

CFLAGS_COMMON := -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Isrc/common

# ---- KN7APP.BIN: the platform (the PDF viewer's, src/app/, ABI 15) ----
APP_CFLAGS := $(CFLAGS_COMMON) -std=c11 -ffreestanding -Isrc/app -DREST_MS=$(REST_MS)
APP_C   := $(addprefix src/app/,entry.c fwfind.c notes.c files.c task.c lcd.c game.c crash.c fwdraw.c folder.c diag.c)
APP_OBJ := $(APP_C:src/app/%.c=$(OUT)/app/%.o) $(OUT)/app/glue.o $(OUT)/app/font8x13.o

# ---- KEYFALL.BIN: the game (src/game/) ----
GAME_C  := $(addprefix src/game/,main.c smf.c song.c judge.c draw.c play.c menu.c fmt.c scores.c)
GAME_HDR := $(wildcard src/game/*.h src/common/*.h)
GAME_CFLAGS := $(CFLAGS_COMMON) -std=gnu11 -ffreestanding -Isrc/game -DKF_PLAY_REST=$(PLAY_REST_MS)
GAME_OBJ := $(GAME_C:src/game/%.c=$(OUT)/game/%.o)

SONGS_TXT := $(wildcard songs/*.txt)
SONGS_MID := $(SONGS_TXT:.txt=.MID)

all: $(OUT)/KN7APP.BIN $(OUT)/KEYFALL.BIN

$(OUT)/app/%.o: src/app/%.c src/app/app.h src/common/kn7.h src/common/pfhost.h
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(APP_CFLAGS) -I$(OUT)/app -c $< -o $@

# the build's name in the log (diag.c): git describe of the tree, rewritten only when it changes
$(OUT)/app/entry.o $(OUT)/app/crash.o: $(OUT)/app/build_id.h
# rebuilt when REST_MS changes (a stamp rewritten only then)
$(OUT)/app/entry.o: $(OUT)/app/rest_ms
$(OUT)/app/rest_ms: FORCE
	@mkdir -p $(dir $@)
	@echo $(REST_MS) | cmp -s - $@ || echo $(REST_MS) > $@
$(OUT)/app/build_id.h: FORCE
	@mkdir -p $(dir $@)
	@echo '#define BUILD_ID "'"$$(git describe --always --dirty 2>/dev/null || echo unknown)"'"' | cmp -s - $@ || \
	  echo '#define BUILD_ID "'"$$(git describe --always --dirty 2>/dev/null || echo unknown)"'"' > $@

.PHONY: FORCE
FORCE:

$(OUT)/app/glue.o: src/app/glue.S
	@mkdir -p $(dir $@)
	$(CROSS)gcc -c $< -o $@

$(OUT)/app/font8x13.c: $(LOADER)/font8x13.inc tools/build/font2c.py
	@mkdir -p $(dir $@)
	$(PYTHON) -I tools/build/font2c.py $< $@

$(OUT)/app/font8x13.o: $(OUT)/app/font8x13.c
	$(CROSS)gcc $(APP_CFLAGS) -c $< -o $@

$(OUT)/app/kn7app.elf: $(APP_OBJ) src/app/kn7app.ld
	$(CROSS)gcc -nostartfiles -T src/app/kn7app.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(OUT)/app/kn7app.map -o $@ $(APP_OBJ) -lc -lgcc
	$(PYTHON) -I tools/build/check_d32.py $(CROSS)objdump $@ || { rm -f -- $@; false; }
	$(PYTHON) -I tools/build/check_flags.py $(CROSS)objdump $@ || { rm -f -- $@; false; }

$(OUT)/KN7APP.BIN: $(OUT)/app/kn7app.elf $(LOADER)/mkapp.py
	$(CROSS)objcopy -O binary -j .header -j .text -j .rodata -j .data $< $(OUT)/app/kn7app.bin
	$(PYTHON) -I $(LOADER)/mkapp.py $(OUT)/app/kn7app.bin $@

$(OUT)/game/play.o: $(OUT)/game/play_rest
$(OUT)/game/play_rest: FORCE
	@mkdir -p $(dir $@)
	@echo $(PLAY_REST_MS) | cmp -s - $@ || echo $(PLAY_REST_MS) > $@

$(OUT)/game/%.o: src/game/%.c $(GAME_HDR)
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(GAME_CFLAGS) -c $< -o $@

$(OUT)/game/keyfall.elf: $(GAME_OBJ) src/game/keyfall.ld
	$(CROSS)gcc -nostartfiles -T src/game/keyfall.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(OUT)/game/keyfall.map \
	  -o $@ $(GAME_OBJ) -lc -lgcc
	$(PYTHON) -I tools/build/check_d32.py $(CROSS)objdump $@ || { rm -f -- $@; false; }
	$(PYTHON) -I tools/build/check_flags.py $(CROSS)objdump $@ || { rm -f -- $@; false; }

$(OUT)/KEYFALL.BIN: $(OUT)/game/keyfall.elf tools/build/mkgame.py
	$(CROSS)objcopy -O binary -j .header -j .text -j .rodata -j .init_array -j .data $< $(OUT)/game/keyfall.bin
	$(PYTHON) -I tools/build/mkgame.py $(OUT)/game/keyfall.bin $@ 81

# the songs: each songs/NAME.txt (tools/songs/README.md) made into songs/NAME.MID, which is committed too
songs: $(SONGS_MID)
songs/%.MID: songs/%.txt tools/songs/mksong.py
	$(PYTHON) -I tools/songs/mksong.py $< $@

# The KEYFALL\ folder for the program menu's card: the programs, the songs and the menu's name for it.
card-dir: $(OUT)/KN7APP.BIN $(OUT)/KEYFALL.BIN $(SONGS_MID)
	rm -rf -- "$(OUT)/card"
	@mkdir -p $(OUT)/card/KEYFALL
	cp $(OUT)/KN7APP.BIN $(OUT)/KEYFALL.BIN $(OUT)/card/KEYFALL/
	cp $(SONGS_MID) $(OUT)/card/KEYFALL/
	printf '# Keyfall: the KN7000 program menu shows this name (kn7menu/README.md)\r\nNAME=Keyfall (piano game)\r\nFOLDER=Games\r\nDESC=Play the falling notes on the keybed\r\n' > $(OUT)/card/KEYFALL/MANIFEST.TXT

# the game's song reading and judging on the PC (tests/host/README.md): the same C, 32-bit
HOST_C := src/game/smf.c src/game/song.c src/game/judge.c src/game/fmt.c tests/host/kfcheck.c
host: $(OUT)/host/kfcheck
$(OUT)/host/kfcheck: $(HOST_C) $(GAME_HDR)
	@mkdir -p $(dir $@)
	cc -m32 -O1 -g -Wall -Wextra -DKF_HOST -Isrc/common -Isrc/game -o $@ $(HOST_C)

check: $(OUT)/host/kfcheck $(SONGS_MID)
	$(OUT)/host/kfcheck $(SONGS_MID)

clean:
	test -n "$(OUT)" && rm -rf -- "$(OUT)"

.PHONY: all songs card-dir host check clean
