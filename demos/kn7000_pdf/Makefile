# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU Affero General Public License v3 or later (MuPDF 1.1 is GPL-3.0, jbig2dec
# AGPL-3.0: README.md, "Licences")
#
# A PDF viewer for the KN7000 (README.md): MuPDF 1.1, run by the KN7000 program loader (KN7LOAD.CNV) from the program
# menu's folder PDF\.
#   make             $(OUT)/KN7APP.BIN (the platform) and $(OUT)/PDF.BIN (the viewer)
#   make card-dir    $(OUT)/card/PDF/: those, MANIFEST.TXT, the base 14 fonts and the PDFs in TEST_PDFS
#   make mame-media  $(OUT)/mame/sd.img and floppy.img: a copy of the real KN7000's card with the program menu and PDF\,
#                    and of the loader floppy (RUNNING-THE-BUILDS.md, KN7000 repo)
#   make host        $(OUT)/host/mudraw (MuPDF 1.1's own, for comparing) and $(OUT)/host/kn7pdf (the viewer's
#                    drawing, on the PC, 32-bit: tests/host/)
# Needs the mn10300-elf toolchain the Pinball port builds (Pinball kn7000/toolchain/build.sh: GCC patched to make no
# (d32,An) operand) and the KN7000 loader's tools (technics-roms-disasm kn7000/tools/cnv_romdump: its font and mkapp.py).
TOOLS   ?= $(HOME)/compartilhado/tools/mn10300-elf-kn7000/bin
CROSS   := $(TOOLS)/mn10300-elf-
OUT     ?= build
PYTHON  ?= python3
KN7     ?= $(HOME)/compartilhado/technics-roms-disasm/kn7000
LOADER  := $(KN7)/tools/cnv_romdump
MENU    := $(KN7)/tools/kn7menu
# PDFs put in the card's folder by card-dir (the ProTracker manual and a LilyPond score, as tested)
TEST_PDFS ?= $(HOME)/compartilhado/kn5000_homebrew/demos/kn7000_protracker/manual/protracker-kn7000-manual.pdf \
             $(HOME)/compartilhado/PatinhoFeio/partitura/Bachianinha_No1_duas_vozes.pdf
HOSTCC  ?= gcc -m32 -no-pie -fno-pie

# ---- MuPDF 1.1 and its libraries (src/UPSTREAM) ----
MU      := src/mupdf
FT      := src/third/freetype
FT_C    := $(addprefix $(FT)/src/base/,ftbase.c ftbbox.c ftbitmap.c ftdebug.c ftfstype.c ftgasp.c ftglyph.c ftinit.c \
             ftstroke.c ftsynth.c ftsystem.c fttype1.c) \
           $(FT)/src/cff/cff.c $(FT)/src/psaux/psaux.c $(FT)/src/psnames/psnames.c $(FT)/src/sfnt/sfnt.c \
           $(FT)/src/smooth/smooth.c $(FT)/src/truetype/truetype.c $(FT)/src/type1/type1.c
ZLIB_C  := $(addprefix src/third/zlib/,adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c)
# deflate: for the PC build's PNG output only
ZLIB_DEFLATE_C := $(addprefix src/third/zlib/,compress.c deflate.c trees.c)
JPEG_C  := $(wildcard src/third/libjpeg/j*.c)
JBIG2_C := $(addprefix src/third/jbig2dec/,jbig2.c jbig2_arith.c jbig2_arith_iaid.c jbig2_arith_int.c jbig2_generic.c \
             jbig2_halftone.c jbig2_huffman.c jbig2_hufftab.c jbig2_image.c jbig2_mmr.c jbig2_page.c \
             jbig2_refinement.c jbig2_segment.c jbig2_symbol_dict.c jbig2_text.c)
MU_C    := $(filter-out $(MU)/fitz/image_jpx.c $(MU)/fitz/memento.c,$(wildcard $(MU)/fitz/*.c)) \
           $(filter-out $(MU)/draw/draw_simple_scale.c,$(wildcard $(MU)/draw/*.c)) \
           $(filter-out $(MU)/pdf/pdf_fontfile.c,$(wildcard $(MU)/pdf/*.c))
THIRD_C := $(FT_C) $(ZLIB_C) $(JPEG_C) $(JBIG2_C)
MU_INC  := -I$(MU)/fitz -I$(MU)/pdf -Isrc/kn7 -I$(FT) -I$(FT)/include -Isrc/third/zlib -Isrc/third/libjpeg -Isrc/third/jbig2dec
MU_DEFS := -DKN7PDF -DNOCJK -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=\"kn7ftmodules.h\" \
           -DFT_CONFIG_OPTIONS_H=\"kn7ftoptions.h\" -DHAVE_STDINT_H -DNO_GZIP
MU_HDR  := $(wildcard $(MU)/fitz/*.h $(MU)/pdf/*.h)

# the base 14 fonts as the viewer reads them from its folder (src/kn7/fontfile.c): MuPDF's file, its 8.3 name
FONT_MAP := NimbusMonL-Regu:COUR NimbusMonL-Bold:COURB NimbusMonL-ReguObli:COURI NimbusMonL-BoldObli:COURBI \
            NimbusSanL-Regu:HELV NimbusSanL-Bold:HELVB NimbusSanL-ReguItal:HELVI NimbusSanL-BoldItal:HELVBI \
            NimbusRomNo9L-Regu:TIMES NimbusRomNo9L-Medi:TIMESB NimbusRomNo9L-ReguItal:TIMESI \
            NimbusRomNo9L-MediItal:TIMESBI StandardSymL:SYMBOL Dingbats:DINGBATS
FONT_FILES := $(foreach m,$(FONT_MAP),$(word 2,$(subst :, ,$(m))).CFF)
all: $(OUT)/KN7APP.BIN $(OUT)/PDF.BIN

# ---- KN7APP.BIN: the platform (Doom's, src/app/) ----
CFLAGS_COMMON := -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Isrc/common
APP_CFLAGS := $(CFLAGS_COMMON) -std=c11 -ffreestanding -Isrc/app
APP_C   := $(addprefix src/app/,entry.c fwfind.c notes.c files.c task.c lcd.c game.c crash.c fwdraw.c folder.c borrow.c diag.c)
APP_OBJ := $(APP_C:src/app/%.c=$(OUT)/app/%.o) $(OUT)/app/glue.o $(OUT)/app/font8x13.o

$(OUT)/app/%.o: src/app/%.c src/app/app.h src/common/kn7.h src/common/pfhost.h
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(APP_CFLAGS) -I$(OUT)/app -c $< -o $@

# the build's name in the log (diag.c): git describe of the tree, rewritten only when it changes
$(OUT)/app/entry.o $(OUT)/app/crash.o: $(OUT)/app/build_id.h
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

# ---- GCC's soft-fp (src/third/softfp/, src/UPSTREAM) in place of libgcc's fp-bit.c: -O2, as MuPDF's floats spend the
# CPU there. tests/softfp/test_softfp.c checks them against the PC's (make softfp-test). ----
SOFTFP  := addsf3 subsf3 mulsf3 divsf3 negsf2 eqsf2 gesf2 lesf2 unordsf2 fixsfsi fixunssfsi floatsisf floatunsisf \
           adddf3 subdf3 muldf3 divdf3 negdf2 eqdf2 gedf2 ledf2 unorddf2 fixdfsi fixunsdfsi floatsidf floatunsidf \
           extendsfdf2 truncdfsf2
SOFTFP_OBJ := $(SOFTFP:%=$(OUT)/softfp/%.o)
SOFTFP_FN := __addsf3 __subsf3 __mulsf3 __divsf3 __negsf2 __eqsf2 __nesf2 __gesf2 __gtsf2 __lesf2 __ltsf2 \
             __unordsf2 __fixsfsi __fixunssfsi __floatsisf __floatunsisf __adddf3 __subdf3 __muldf3 __divdf3 \
             __negdf2 __eqdf2 __nedf2 __gedf2 __gtdf2 __ledf2 __ltdf2 __unorddf2 __fixdfsi __fixunsdfsi __floatsidf \
             __floatunsidf __extendsfdf2 __truncdfsf2

$(OUT)/softfp/%.o: src/third/softfp/%.c $(wildcard src/third/softfp/*.h)
	@mkdir -p $(dir $@)
	$(CROSS)gcc -O2 -g -ffunction-sections -w -Isrc/third/softfp -c $< -o $@

# the same sources on the PC, each function renamed t_..., against the PC's float and double
$(OUT)/softfp-host/%.o: src/third/softfp/%.c $(wildcard src/third/softfp/*.h)
	@mkdir -p $(dir $@)
	gcc -m32 -O2 -msse2 -mfpmath=sse -w -Isrc/third/softfp $(foreach f,$(SOFTFP_FN),-D$(f)=t_$(f:__%=%)) -c $< -o $@

softfp-test: $(SOFTFP:%=$(OUT)/softfp-host/%.o) tests/softfp/test_softfp.c
	gcc -m32 -O2 -msse2 -mfpmath=sse tests/softfp/test_softfp.c $(SOFTFP:%=$(OUT)/softfp-host/%.o) -lm -o $(OUT)/test_softfp
	$(OUT)/test_softfp

# ---- PDF.BIN: MuPDF 1.1 and its libraries, and the KN7000 side (src/kn7/) ----
KN7_C   := $(addprefix src/kn7/,main.c viewer.c view.c pagecache.c fontfile.c left_out.c fmt.c) src/third/tlsf/tlsf.c
PDF_CFLAGS := -Os -g -ffunction-sections -fdata-sections -std=gnu99 -w $(MU_INC) $(MU_DEFS) -Isrc/kn7 -Isrc/common \
              -Isrc/third/tlsf
PDF_OBJ := $(MU_C:src/%.c=$(OUT)/pdf/%.o) $(THIRD_C:src/%.c=$(OUT)/pdf/%.o) $(KN7_C:src/%.c=$(OUT)/pdf/%.o) $(SOFTFP_OBJ)

# (-MMD: what each source includes, FreeType's modules' other .c files among it, in a .d beside its object)
$(OUT)/pdf/%.o: src/%.c $(MU_HDR) $(wildcard src/kn7/*.h src/common/*.h)
	@mkdir -p $(dir $@)
	$(CROSS)gcc $(PDF_CFLAGS) -MMD -MP -c $< -o $@
-include $(PDF_OBJ:.o=.d)

# jbig2dec casts an int32_t's address to int * where int32_t is long (newlib's for the MN10300; both 32 bits)
$(OUT)/pdf/third/jbig2dec/%.o: PDF_CFLAGS += -Wno-error=incompatible-pointer-types

$(OUT)/pdf/pdf.elf: $(PDF_OBJ) src/kn7/pdf.ld
	$(CROSS)gcc -nostartfiles -T src/kn7/pdf.ld -Wl,--enable-non-contiguous-regions -Wl,--gc-sections \
	  -Wl,--no-warn-rwx-segments -Wl,-Map=$(OUT)/pdf/pdf.map -o $@ $(PDF_OBJ) -lc -lm -lgcc
	$(PYTHON) -I tools/build/check_d32.py $(CROSS)objdump $@ || { rm -f -- $@; false; }
	$(PYTHON) -I tools/build/check_flags.py $(CROSS)objdump $@ || { rm -f -- $@; false; }

$(OUT)/PDF.BIN: $(OUT)/pdf/pdf.elf tools/build/mkpdf.py
	$(PYTHON) -I tools/build/mkpdf.py $< $@

# ---- the PC build (tests/host/) ----
HOST_CFLAGS := -O2 -g -fno-omit-frame-pointer -std=gnu99 -w $(MU_INC) $(MU_DEFS) -Isrc/kn7
HOST_OBJ := $(MU_C:src/%.c=$(OUT)/host/%.o) $(THIRD_C:src/%.c=$(OUT)/host/%.o) $(ZLIB_DEFLATE_C:src/%.c=$(OUT)/host/%.o)

host: $(OUT)/host/mudraw $(OUT)/host/kn7pdf $(FONT_FILES:%=$(OUT)/fonts/%)

define font_rule
$(OUT)/fonts/$(2).CFF: $(MU)/fonts/$(1).cff
	@mkdir -p $$(dir $$@)
	cp $$< $$@
endef
$(foreach m,$(FONT_MAP),$(eval $(call font_rule,$(word 1,$(subst :, ,$(m))),$(word 2,$(subst :, ,$(m))))))

KN7PDF_C := src/kn7/view.c src/kn7/fontfile.c src/kn7/left_out.c
$(OUT)/host/kn7pdf: $(OUT)/host/tests/kn7pdf.o $(KN7PDF_C:src/%.c=$(OUT)/host/%.o) $(HOST_OBJ)
	$(HOSTCC) $^ -lm -o $@

$(OUT)/host/tests/%.o: tests/host/%.c $(MU_HDR) src/kn7/view.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -c $< -o $@

$(OUT)/host/%.o: src/%.c $(MU_HDR)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -c $< -o $@

$(OUT)/host/fontdump: $(MU)/scripts/fontdump.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 $< -o $@

# MuPDF's pdf_fontfile.c includes "../generated/font_base14.h": the base 14 fonts linked in (on the KN7000 they are
# files on the card: src/kn7/)
$(OUT)/host/gen/generated/font_base14.h: $(OUT)/host/fontdump $(wildcard $(MU)/fonts/*.cff)
	@mkdir -p $(dir $@) $(OUT)/host/gen/inc
	$(OUT)/host/fontdump $@ $(wildcard $(MU)/fonts/*.cff)

$(OUT)/host/fontfile.o: $(MU)/pdf/pdf_fontfile.c $(OUT)/host/gen/generated/font_base14.h
	$(HOSTCC) $(HOST_CFLAGS) -DNODROIDFONT -I$(OUT)/host/gen/inc -c $< -o $@

$(OUT)/host/mudraw: $(OUT)/host/mupdf/apps/mudraw.o $(OUT)/host/kn7/left_out.o $(OUT)/host/fontfile.o $(HOST_OBJ)
	$(HOSTCC) $^ -lm -o $@

# The PDF\ folder for the program menu's card: the programs, the fonts and the test PDFs (8.3 names: tools/build/
# cardname.py).
card-dir: $(OUT)/KN7APP.BIN $(OUT)/PDF.BIN $(FONT_FILES:%=$(OUT)/fonts/%)
	rm -rf -- "$(OUT)/card/PDF"
	@mkdir -p $(OUT)/card/PDF
	cp $(OUT)/KN7APP.BIN $(OUT)/PDF.BIN $(FONT_FILES:%=$(OUT)/fonts/%) $(OUT)/card/PDF/
	for f in $(TEST_PDFS); do cp "$$f" "$(OUT)/card/PDF/$$($(PYTHON) -I tools/build/cardname.py "$$f")"; done
	printf '# The PDF viewer: the KN7000 program menu shows this name (kn7menu/README.md)\r\nNAME=PDF viewer (MuPDF)\r\nFOLDER=Tools\r\n' > $(OUT)/card/PDF/MANIFEST.TXT

# A card and a floppy for MAME: a copy of the real KN7000's card with the program menu as its root KN7APP.BIN and the
# PDF\ folder, and a copy of the owner's loader floppy (as Doom's mame-media).
EMU       ?= $(HOME)/compartilhado/kn7000-emulator
LOADER_FLOPPY := $(LOADER)/releases/2026-10-19/floppy/kn7load_floppy.img
mame-media: card-dir
	$(MAKE) -s -C $(MENU) OUT=$(abspath $(OUT))/mame/menu
	rm -rf -- "$(OUT)/mame/tree"
	mkdir -p $(OUT)/mame/tree
	cp $(OUT)/mame/menu/KN7APP.BIN $(OUT)/mame/tree/KN7APP.BIN
	cp -r $(OUT)/card/PDF $(OUT)/mame/tree/PDF
	cp $(EMU)/sdcard_from_real_kn7000.img $(OUT)/mame/sd.img
	chmod u+w $(OUT)/mame/sd.img
	$(PYTHON) -I $(MENU)/fatimg.py $(OUT)/mame/sd.img put $(OUT)/mame/tree > /dev/null
	rm -rf -- "$(OUT)/mame/tree"
	cp $(LOADER_FLOPPY) $(OUT)/mame/floppy.img
	chmod u+w $(OUT)/mame/floppy.img
	@echo "mame-media: $(abspath $(OUT))/mame/sd.img and floppy.img"

clean:
	test -n "$(OUT)" && rm -rf -- "$(OUT)"

.PHONY: all host card-dir mame-media clean softfp-test
