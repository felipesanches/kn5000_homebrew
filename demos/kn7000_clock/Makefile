# KN7000 uptime clock -- an SP7001 SD-card app (see README.md).
#
# Needs a checkout of kn7000_disassembly beside this repo (for the MN10300 assembler
# and the XAP/SD tools) and, in it, the vendor BASEEXT.XAP extracted from the SP7001
# update (`make -C $(DISASM_REPO) baserom/sp7001_baseext.xap`).

DISASM_REPO ?= ../../../kn7000_disassembly
PYTHON      ?= python3
BASE_XAP    ?= $(DISASM_REPO)/baserom/sp7001_baseext.xap
MODULE_AT   := 0x502B6000
# An SP7001 SD-card image (one the SP7001 setup has prepared); `make card` copies it.
SD_IMAGE    ?= ../../../technics_roms/media/sp7001_activated_sdcard.img
# A MAME build with the kn7000 driver and the `sp7001` BIOS, plus its ROM path.
MAME        ?= mame
ROMPATH     ?= ../../../technics_romsets

all: build/BASEEXT.XAP

build/font.inc: mkfont.py
	@mkdir -p build
	$(PYTHON) mkfont.py $@

build/clock.bin: clock.s build/font.inc $(DISASM_REPO)/tools/xapasm.py
	$(PYTHON) $(DISASM_REPO)/tools/xapasm.py clock.s -I build -o $@ -m build/clock.map

build/BASEEXT.XAP: build/clock.bin $(BASE_XAP) $(DISASM_REPO)/tools/xap_addon.py
	$(PYTHON) $(DISASM_REPO)/tools/xap_addon.py $(BASE_XAP) build/clock.bin $@ --at $(MODULE_AT)

# A copy of the SD-card image with the clock's BASEEXT.XAP in place of the original.
card: build/BASEEXT.XAP
	cp $(SD_IMAGE) build/kn7000_clock_sd.img
	chmod u+w build/kn7000_clock_sd.img
	$(PYTHON) $(DISASM_REPO)/tools/sd_splice.py build/kn7000_clock_sd.img 'C:\TECHNICS\DATA\BASEEXT.XAP' build/BASEEXT.XAP

# Boot the SP7001 firmware with that card; press the orange SD CARD button to start the clock.
run: card
	$(MAME) -rompath $(ROMPATH) kn7000 -bios sp7001 -harddisk build/kn7000_clock_sd.img -window

clean:
	rm -rf -- build

.PHONY: all card run clean
