# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU Affero General Public License v3 or later (MuPDF 1.1 is GPL-3.0, jbig2dec
# AGPL-3.0: README.md, "Licences")
#
# A PDF viewer for the KN7000 (README.md): MuPDF 1.1, run by the KN7000 program loader (KN7LOAD.CNV) from the program
# menu's folder PDF\.
#   make host        $(OUT)/host/mudraw (MuPDF 1.1's own, for comparing) and $(OUT)/host/kn7pdf (the viewer's
#                    drawing, on the PC, 32-bit: tests/host/)
TOOLS   ?= $(HOME)/compartilhado/tools/mn10300-elf-kn7000/bin
CROSS   := $(TOOLS)/mn10300-elf-
OUT     ?= build
PYTHON  ?= python3
HOSTCC  ?= gcc -m32

# ---- MuPDF 1.1 and its libraries (src/UPSTREAM) ----
MU      := src/mupdf
FT      := src/third/freetype
FT_C    := $(addprefix $(FT)/src/base/,ftbase.c ftbbox.c ftbitmap.c ftdebug.c ftfstype.c ftgasp.c ftglyph.c ftinit.c \
             ftstroke.c ftsynth.c ftsystem.c fttype1.c) \
           $(FT)/src/cff/cff.c $(FT)/src/cid/type1cid.c $(FT)/src/psaux/psaux.c $(FT)/src/pshinter/pshinter.c \
           $(FT)/src/psnames/psnames.c $(FT)/src/raster/raster.c $(FT)/src/sfnt/sfnt.c $(FT)/src/smooth/smooth.c \
           $(FT)/src/truetype/truetype.c $(FT)/src/type1/type1.c
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
MU_INC  := -I$(MU)/fitz -I$(MU)/pdf -I$(FT) -I$(FT)/include -Isrc/third/zlib -Isrc/third/libjpeg -Isrc/third/jbig2dec
MU_DEFS := -DNOCJK -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=\"slimftmodules.h\" \
           -DFT_CONFIG_OPTIONS_H=\"slimftoptions.h\" -DHAVE_STDINT_H
MU_HDR  := $(wildcard $(MU)/fitz/*.h $(MU)/pdf/*.h)

# ---- the PC build (tests/host/) ----
HOST_CFLAGS := -O2 -g -std=gnu99 -w $(MU_INC) $(MU_DEFS) -Isrc/kn7
HOST_OBJ := $(MU_C:src/%.c=$(OUT)/host/%.o) $(THIRD_C:src/%.c=$(OUT)/host/%.o) $(ZLIB_DEFLATE_C:src/%.c=$(OUT)/host/%.o)

host: $(OUT)/host/mudraw

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

.PHONY: host
