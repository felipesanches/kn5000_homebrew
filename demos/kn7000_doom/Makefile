# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# Doom for the KN7000 (README.md): Chocolate Doom, run by the KN7000 program loader (KN7LOAD.CNV) from the program
# menu's folder DOOM\.
#   make             $(OUT)/KN7APP.BIN (the platform) and $(OUT)/DOOM.BIN (Chocolate Doom)
#   make card-dir    $(OUT)/card/DOOM/: those, MANIFEST.TXT and the WAD (WAD, default the shareware DOOM1.WAD)
#   make mame-media  $(OUT)/mame/sd.img and floppy.img: a copy of the real KN7000's card with the program menu and
#                    DOOM\, and of the loader floppy (RUNNING-THE-BUILDS.md, KN7000 repo)
#   make host        $(OUT)/host/doom: the same game code on the PC, 32-bit, without a window (tests/host/)
# Needs the mn10300-elf toolchain the Pinball port builds (Pinball kn7000/toolchain/build.sh: GCC patched to make no
# (d32,An) operand), the KN7000 loader's tools (technics-roms-disasm kn7000/tools/cnv_romdump: its font and mkapp.py),
# and Another World's DSP tools (technics-anotherworld kn7000/tools: the mixer).
TOOLS   ?= $(HOME)/compartilhado/tools/mn10300-elf-kn7000/bin
CROSS   := $(TOOLS)/mn10300-elf-
KN7     ?= $(HOME)/compartilhado/technics-roms-disasm/kn7000
LOADER  := $(KN7)/tools/cnv_romdump
MENU    := $(KN7)/tools/kn7menu
AW_TOOLS ?= $(HOME)/compartilhado/technics-anotherworld/kn7000/tools
UNIDASM  ?= $(HOME)/compartilhado/mame-sony-video/unidasm
OUT     ?= build
PYTHON  ?= python3
# the shareware DOOM1.WAD (tools/get_wad.sh fetches it there)
WAD     ?= $(HOME)/compartilhado/Doom/DOOM1.WAD
# the DSP mixer's output gain (tools/build/dsp_c.py), as ProTracker's
DSP_GAIN ?= 2

CFLAGS_COMMON := -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Isrc/common

# ---- KN7APP.BIN: the platform (ProTracker's, src/app/) ----
APP_CFLAGS := $(CFLAGS_COMMON) -std=c11 -ffreestanding -Isrc/app
APP_C   := src/app/entry.c src/app/fwfind.c src/app/notes.c src/app/files.c src/app/task.c src/app/lcd.c src/app/game.c \
           src/app/dsp.c src/app/crash.c src/app/fwdraw.c src/app/folder.c src/app/borrow.c
APP_OBJ := $(APP_C:src/app/%.c=$(OUT)/app/%.o) $(OUT)/app/glue.o $(OUT)/app/font8x13.o

# ---- DOOM.BIN: Chocolate Doom (src/choco/) and the KN7000 side (src/kn7/) ----
CHOCO_C := $(filter-out src/choco/w_file_stdc.c src/choco/midifile.c src/choco/mus2mid.c,$(wildcard src/choco/*.c)) \
           $(filter-out src/choco/doom/doom_icon.c,$(wildcard src/choco/doom/*.c))
KN7_C   := src/kn7/main.c src/kn7/i_video.c src/kn7/i_timer.c src/kn7/i_input.c src/kn7/w_file_kn7.c \
           src/kn7/i_sound_kn7.c src/kn7/bank.c src/kn7/net_none.c src/kn7/left_out.c src/kn7/fmt.c
DOOM_INC := -include src/kn7/doom_kn7.h -Isrc/kn7 -Isrc/kn7/shim -Isrc/choco -Isrc/choco/doom
DOOM_CFLAGS := $(CFLAGS_COMMON) -std=gnu11 -fno-strict-aliasing $(DOOM_INC) \
               -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
DOOM_OBJ := $(CHOCO_C:src/%.c=$(OUT)/doom/%.o) $(KN7_C:src/%.c=$(OUT)/doom/%.o)
DOOM_HDR := $(wildcard src/choco/*.h src/choco/doom/*.h src/kn7/*.h src/kn7/shim/*.h src/common/*.h)

all: $(OUT)/KN7APP.BIN $(OUT)/DOOM.BIN

$(OUT)/app/%.o: src/app/%.c src/app/app.h src/common/kn7.h src/common/pfhost.h
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(APP_CFLAGS) -I$(OUT)/app -c $< -o $@

$(OUT)/app/dsp.o $(OUT)/app/game.o: $(OUT)/app/dsp_mixer.h

$(OUT)/app/dsp_mixer.h: tools/build/dsp_c.py $(AW_TOOLS)/dsp_mixer.py $(AW_TOOLS)/sharc_enc.py $(OUT)/app/dsp_gain
	@mkdir -p $(dir $@)
	$(PYTHON) -I tools/build/dsp_c.py $(AW_TOOLS) $@ $(UNIDASM) $(DSP_GAIN)

# rewritten only when DSP_GAIN changes, so that a new gain makes a new header
$(OUT)/app/dsp_gain: FORCE
	@mkdir -p $(dir $@)
	@echo $(DSP_GAIN) | cmp -s - $@ || echo $(DSP_GAIN) > $@

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

$(OUT)/KN7APP.BIN: $(OUT)/app/kn7app.elf $(LOADER)/mkapp.py
	$(CROSS)objcopy -O binary -j .header -j .text -j .rodata -j .data $< $(OUT)/app/kn7app.bin
	$(PYTHON) -I $(LOADER)/mkapp.py $(OUT)/app/kn7app.bin $@

$(OUT)/doom/%.o: src/%.c $(DOOM_HDR)
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(DOOM_CFLAGS) -c $< -o $@

# ProTracker's bank.c, unchanged: its encoder only (BANK_HOST_TEST leaves its ProTracker part out)
$(OUT)/doom/kn7/bank.o: src/kn7/bank.c
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(CFLAGS_COMMON) -std=gnu11 -DBANK_HOST_TEST -c $< -o $@

$(OUT)/doom/doom.elf: $(DOOM_OBJ) src/kn7/doom.ld
	$(CROSS)gcc -nostartfiles -T src/kn7/doom.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(OUT)/doom/doom.map \
	  -o $@ $(DOOM_OBJ) -lc -lm -lgcc
	$(PYTHON) -I tools/build/check_d32.py $(CROSS)objdump $@ || { rm -f -- $@; false; }

$(OUT)/DOOM.BIN: $(OUT)/doom/doom.elf tools/build/mkgame.py
	$(CROSS)objcopy -O binary -j .header -j .text -j .rodata -j .init_array -j .data $< $(OUT)/doom/doom.bin
	$(PYTHON) -I tools/build/mkgame.py $(OUT)/doom/doom.bin $@ 68

# The DOOM\ folder for the program menu's card: the program and the WAD.
card-dir: $(OUT)/KN7APP.BIN $(OUT)/DOOM.BIN
	@mkdir -p $(OUT)/card/DOOM
	cp $(OUT)/KN7APP.BIN $(OUT)/DOOM.BIN $(OUT)/card/DOOM/
	cp $(WAD) $(OUT)/card/DOOM/
	printf '# Doom: the KN7000 program menu shows this name (kn7menu/README.md)\r\nNAME=Doom (Chocolate Doom)\r\nFOLDER=Games\r\n' > $(OUT)/card/DOOM/MANIFEST.TXT

# A card and a floppy for MAME: a copy of the real KN7000's card with the program menu as its root KN7APP.BIN and the
# DOOM\ folder, and a copy of the owner's loader floppy (as ProTracker's mame-media).
EMU       ?= $(HOME)/compartilhado/kn7000-emulator
LOADER_FLOPPY := $(LOADER)/releases/2026-10-19/floppy/kn7load_floppy.img
mame-media: card-dir
	$(MAKE) -s -C $(MENU) OUT=$(abspath $(OUT))/mame/menu
	rm -rf -- "$(OUT)/mame/tree"
	mkdir -p $(OUT)/mame/tree
	cp $(OUT)/mame/menu/KN7APP.BIN $(OUT)/mame/tree/KN7APP.BIN
	cp -r $(OUT)/card/DOOM $(OUT)/mame/tree/DOOM
	cp $(EMU)/sdcard_from_real_kn7000.img $(OUT)/mame/sd.img
	chmod u+w $(OUT)/mame/sd.img
	$(PYTHON) -I $(MENU)/fatimg.py $(OUT)/mame/sd.img put $(OUT)/mame/tree > /dev/null
	rm -rf -- "$(OUT)/mame/tree"
	cp $(LOADER_FLOPPY) $(OUT)/mame/floppy.img
	chmod u+w $(OUT)/mame/floppy.img
	@echo "mame-media: $(abspath $(OUT))/mame/sd.img and floppy.img"

# the same game code on the PC (tests/host/README.md): 32-bit, as the KN7000's, with the PC's files
HOST_C := $(filter-out src/choco/midifile.c src/choco/mus2mid.c,$(wildcard src/choco/*.c)) \
          $(filter-out src/choco/doom/doom_icon.c,$(wildcard src/choco/doom/*.c)) \
          src/kn7/net_none.c src/kn7/left_out.c src/kn7/i_sound_kn7.c src/kn7/bank.c tests/host/host.c
host: $(OUT)/host/doom
$(OUT)/host/doom: $(HOST_C) $(DOOM_HDR)
	@mkdir -p $(dir $@)
	cc -m32 -O2 -g -w -fno-strict-aliasing -DBANK_HOST_TEST $(DOOM_INC) -Isrc/common -o $@ $(HOST_C) -lm

# the offsets doom_test.lua reads the player's state at (tests/mame/offsets.c)
tests/mame/offsets.txt: tests/mame/offsets.c $(DOOM_HDR)
	@mkdir -p $(OUT)
	$(CROSS)gcc $(DOOM_CFLAGS) -c $< -o $(OUT)/offsets.o
	$(CROSS)objdump -s $(OUT)/offsets.o > $(OUT)/offsets.dump
	$(PYTHON) -I tools/build/offsets.py $(OUT)/offsets.dump > $@

clean:
	test -n "$(OUT)" && rm -rf -- "$(OUT)"

.PHONY: all card-dir mame-media host clean
