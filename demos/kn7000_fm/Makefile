# KN7FM (README.md): a DX7-style FM synthesizer for the KN7000, as a KN7LOAD.CNV application for the program menu's
# card (folder KN7FM\).
#   make            build/KN7APP.BIN, after fm_dsp.py --check (every SHARC instruction disassembles as meant,
#                   MAME's unidasm)
#   make card-dir   build/card/KN7FM/: KN7APP.BIN, MANIFEST.TXT, ICON.BMP (what the menu's card gets)
#   make sim        the DSP program against the model, all 32 algorithms (tools/fm_sim.py)
PYTHON  ?= python3
KN7     ?= ../../../technics-roms-disasm/kn7000
TOOLS   := $(KN7)/tools
LOADER  := $(TOOLS)/cnv_romdump
MENU    := $(TOOLS)/kn7menu
XAPASM  := $(PYTHON) $(TOOLS)/xapasm.py
UNIDASM ?= $(HOME)/compartilhado/mame-sony-video/unidasm
OUT     ?= build
INC     := -I src -I $(OUT) -I $(LOADER)
SRC     := src/kn7fm.s src/kn7fm.inc src/dsp.inc src/dsp.s src/fwfind.s src/synth.s src/ui.s

all: $(OUT)/KN7APP.BIN

$(OUT)/dsp_equ.inc $(OUT)/dsp_data.inc: tools/fm_dsp.py tools/sharc_enc.py tools/fm_model.py
	@mkdir -p $(OUT)
	$(PYTHON) -I tools/fm_dsp.py $(OUT) --check $(UNIDASM) --listing $(OUT)/dsp.lst

$(OUT)/fm_tables.inc $(OUT)/fm_equ.inc: tools/fm_host.py tools/fm_model.py
	@mkdir -p $(OUT)
	$(PYTHON) -I tools/fm_host.py $(OUT)

$(OUT)/kn7fm.bin: $(SRC) $(OUT)/dsp_equ.inc $(OUT)/dsp_data.inc $(OUT)/fm_tables.inc $(LOADER)/kn7cnv.inc \
                  $(LOADER)/font8x13.inc $(TOOLS)/xapasm.py $(TOOLS)/mn10300_asm.py
	$(XAPASM) src/kn7fm.s $(INC) -o $@ -m $(OUT)/kn7fm.map

$(OUT)/KN7APP.BIN: $(OUT)/kn7fm.bin $(LOADER)/mkapp.py
	$(PYTHON) -I tools/check_map.py $(OUT)/kn7fm.map
	$(PYTHON) -I $(LOADER)/mkapp.py $(OUT)/kn7fm.bin $@
	@sha1sum $@

card-dir: $(OUT)/KN7APP.BIN tools/icon.py $(MENU)/png2icon.py
	@mkdir -p $(OUT)/card/KN7FM
	cp $(OUT)/KN7APP.BIN $(OUT)/card/KN7FM/KN7APP.BIN
	printf '# KN7FM: the KN7000 program menu shows this name and icon (kn7menu/README.md)\r\nNAME=KN7FM: DX7-style FM synthesizer\r\nICON=ICON.BMP\r\n' > $(OUT)/card/KN7FM/MANIFEST.TXT
	$(PYTHON) -I tools/icon.py $(OUT)/kn7fm_icon.png
	$(PYTHON) -I $(MENU)/png2icon.py $(OUT)/kn7fm_icon.png $(OUT)/card/KN7FM/ICON.BMP

sim:
	$(PYTHON) -I tools/fm_sim.py --seconds 0.5

clean:
	test -n "$(OUT)" && rm -rf -- "$(OUT)"

.PHONY: all card-dir sim clean
