# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# ProTracker for the KN7000 (README.md): pt2-clone, a C port of the Amiga's ProTracker 2.3D, run by the KN7000
# program loader (KN7LOAD.CNV) from the program menu's folder PROTRACK\.
#   make             $(OUT)/KN7APP.BIN (the platform) and $(OUT)/PT.BIN (ProTracker)
#   make card-dir    $(OUT)/card/PROTRACK/: those, MANIFEST.TXT and the modules in MODS (default: the six of Pinball
#                    Fantasies, from PF_DATA)
#   make mame-media  $(OUT)/mame/sd.img and floppy.img: a copy of the real KN7000's card with the program menu and
#                    PROTRACK\, and of the loader floppy (RUNNING-THE-BUILDS.md, KN7000 repo)
#   make test-bank   the sample coder against dsp_bank.py (tests/bank/)
#   make render      build/host/render: the port's replayer and Paula on the PC (tests/host/)
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
PF_DATA ?= $(HOME)/compartilhado/Projeto_Pinball/data
MODS    ?= INTRO.MOD MOD2.MOD TABLE1.MOD TABLE2.MOD TABLE3.MOD TABLE4.MOD
# the DSP mixer's output gain (tools/build/dsp_c.py): 16, 18 dB below Another World's 128, which on the unit put a
# comfortable level at the bottom of MAIN VOLUME's travel (README.md, "How it works")
DSP_GAIN ?= 16

CFLAGS_COMMON := -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Isrc/common

# ---- KN7APP.BIN: the platform (the Pinball port's, src/app/) ----
APP_CFLAGS := $(CFLAGS_COMMON) -std=c11 -ffreestanding -Isrc/app
APP_C   := src/app/entry.c src/app/fwfind.c src/app/notes.c src/app/files.c src/app/task.c src/app/lcd.c src/app/game.c \
           src/app/dsp.c src/app/crash.c src/app/fwdraw.c src/app/folder.c
APP_OBJ := $(APP_C:src/app/%.c=$(OUT)/app/%.o) $(OUT)/app/glue.o $(OUT)/app/font8x13.o

# ---- PT.BIN: pt2-clone (src/pt2/) and the KN7000 side (src/kn7/) ----
PT_CFLAGS := $(CFLAGS_COMMON) -std=gnu11 -fno-strict-aliasing -include src/kn7/pt2_kn7.h -Isrc/kn7/shim -Isrc/kn7/shim/modloaders -Isrc/pt2 -Isrc/kn7 \
             -Wno-unused-parameter -Wno-sign-compare
PT2_C   := src/pt2/pt2_replayer.c src/pt2/pt2_module_loader.c src/pt2/pt2_tables.c src/pt2/pt2_structs.c \
           src/pt2/pt2_textout.c src/pt2/pt2_pattern_viewer.c src/pt2/pt2_bmp.c src/pt2/pt2_palette.c \
           src/pt2/modloaders/pt2_load_mod31.c src/pt2/modloaders/pt2_load_mod15.c src/pt2/pt2_module_saver.c \
           src/pt2/gfx/pt2_gfx_font.c src/pt2/gfx/pt2_gfx_tracker.c src/pt2/gfx/pt2_gfx_vumeter.c \
           src/pt2/gfx/pt2_gfx_mute_buttons.c src/pt2/gfx/pt2_gfx_patcursor.c src/pt2/gfx/pt2_gfx_spectrum.c
KN7_C   := src/kn7/main.c src/kn7/paula.c src/kn7/bank.c src/kn7/video.c src/kn7/ui.c src/kn7/edit.c src/kn7/stubs.c \
           src/kn7/panel.c src/kn7/controls.c src/kn7/song.c src/kn7/disk.c src/kn7/sides.c src/kn7/format.c
PT_OBJ  := $(PT2_C:src/%.c=$(OUT)/pt/%.o) $(KN7_C:src/%.c=$(OUT)/pt/%.o)
PT_HDR  := $(wildcard src/pt2/*.h src/kn7/*.h src/kn7/shim/*.h src/common/*.h)

all: $(OUT)/KN7APP.BIN $(OUT)/PT.BIN

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

$(OUT)/pt/%.o: src/%.c $(PT_HDR)
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(PT_CFLAGS) -c $< -o $@

$(OUT)/pt/pt.elf: $(PT_OBJ) src/kn7/pt.ld
	$(CROSS)gcc -nostartfiles -T src/kn7/pt.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(OUT)/pt/pt.map \
	  -o $@ $(PT_OBJ) -lc -lm -lgcc
	$(PYTHON) -I tools/build/check_d32.py $(CROSS)objdump $@ || { rm -f -- $@; false; }

$(OUT)/PT.BIN: $(OUT)/pt/pt.elf tools/build/mkgame.py
	$(CROSS)objcopy -O binary -j .header -j .text -j .rodata -j .init_array -j .data $< $(OUT)/pt/pt.bin
	$(PYTHON) -I tools/build/mkgame.py $(OUT)/pt/pt.bin $@ 80

# The PROTRACK\ folder for the program menu's card: the program, and the modules named by MODS from PF_DATA (without
# PF_DATA, no modules: the player copies them in by hand; the program lists the folder's *.MOD and MOD.* files).
card-dir: $(OUT)/KN7APP.BIN $(OUT)/PT.BIN
	@mkdir -p $(OUT)/card/PROTRACK
	cp $(OUT)/KN7APP.BIN $(OUT)/PT.BIN $(OUT)/card/PROTRACK/
	$(if $(PF_DATA),for m in $(MODS); do cp "$(PF_DATA)/$$m" $(OUT)/card/PROTRACK/; done)
	$(if $(OUT),rm -f -- $(OUT)/card/PROTRACK/MODS.TXT)
	printf '# ProTracker: the KN7000 program menu shows this name (kn7menu/README.md)\r\nNAME=ProTracker 2.3D\r\nFOLDER=Other apps\r\n' > $(OUT)/card/PROTRACK/MANIFEST.TXT

# A card and a floppy for MAME: a copy of the real KN7000's card with the program menu as its root KN7APP.BIN and the
# PROTRACK\ folder, and a copy of the owner's loader floppy (as the Pinball port's mame-media).
EMU       ?= $(HOME)/compartilhado/kn7000-emulator
LOADER_FLOPPY := $(LOADER)/releases/2026-10-19/floppy/kn7load_floppy.img
mame-media: card-dir
	$(MAKE) -s -C $(MENU) OUT=$(abspath $(OUT))/mame/menu
	rm -rf -- "$(OUT)/mame/tree"
	mkdir -p $(OUT)/mame/tree
	cp $(OUT)/mame/menu/KN7APP.BIN $(OUT)/mame/tree/KN7APP.BIN
	cp -r $(OUT)/card/PROTRACK $(OUT)/mame/tree/PROTRACK
	cp $(EMU)/sdcard_from_real_kn7000.img $(OUT)/mame/sd.img
	chmod u+w $(OUT)/mame/sd.img
	$(PYTHON) -I $(MENU)/fatimg.py $(OUT)/mame/sd.img put $(OUT)/mame/tree > /dev/null
	rm -rf -- "$(OUT)/mame/tree"
	cp $(LOADER_FLOPPY) $(OUT)/mame/floppy.img
	chmod u+w $(OUT)/mame/floppy.img
	@echo "mame-media: $(abspath $(OUT))/mame/sd.img and floppy.img"

# the port's replayer and Paula on the PC, with a model of the DSP's mixer (tests/host/README.md)
HOST_C := tests/host/render.c src/kn7/paula.c src/kn7/stubs.c src/pt2/pt2_replayer.c src/pt2/pt2_module_loader.c \
          src/pt2/pt2_tables.c src/pt2/pt2_structs.c src/pt2/pt2_textout.c src/pt2/modloaders/pt2_load_mod31.c \
          src/pt2/modloaders/pt2_load_mod15.c src/pt2/gfx/pt2_gfx_font.c
render: $(OUT)/host/render
$(OUT)/host/render: $(HOST_C) $(PT_HDR)
	@mkdir -p $(dir $@)
	cc -O2 -w -include src/kn7/pt2_kn7.h -Isrc/kn7/shim -Isrc/kn7/shim/modloaders -Isrc/pt2 -Isrc/kn7 -Isrc/common -o $@ $(HOST_C)

test-bank:
	mkdir -p $(HOME)/compartilhado/tmp/pt-bank
	$(PYTHON) -I tests/bank/check_bank.py $(AW_TOOLS) $(HOME)/compartilhado/tmp/pt-bank $(MODS:%=$(PF_DATA)/%)

clean:
	test -n "$(OUT)" && rm -rf -- "$(OUT)"

.PHONY: all card-dir mame-media render test-bank clean
