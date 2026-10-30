#!/bin/sh
# measure_mupdf.sh MUPDF_DIR OUT_DIR -- compile MuPDF's fitz + pdf (minimal config) and its thirdparty
# (freetype, zlib, libjpeg, jbig2dec) with the KN7000's mn10300-elf-gcc -Os; prints .text+.data+.rodata per group.
M=${1:?mupdf dir}; O=${2:?out dir}
CC=$HOME/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-gcc
SZ=$HOME/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-size
mkdir -p "$O"
DEFS="-DFZ_ENABLE_XPS=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_HTML=0 -DFZ_ENABLE_FB2=0
 -DFZ_ENABLE_MOBI=0 -DFZ_ENABLE_EPUB=0 -DFZ_ENABLE_OFFICE=0 -DFZ_ENABLE_TXT=0 -DFZ_ENABLE_MD=0 -DFZ_ENABLE_HTML_ENGINE=0
 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -DFZ_ENABLE_ICC=0 -DFZ_ENABLE_JPX=0
 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_HYPHEN=0 -DFZ_ENABLE_SPOT_RENDERING=0
 -DFZ_PLOTTERS_RGB=0 -DFZ_PLOTTERS_CMYK=0 -DFZ_PLOTTERS_N=0 -DTOFU -DTOFU_CJK -DTOFU_BASE14 -DTOFU_SYMBOL -DTOFU_EMOJI
 -DTOFU_HISTORIC -DTOFU_SIL -DNO_CJK -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=\"slimftmodules.h\"
 -DFT_CONFIG_OPTIONS_H=\"slimftoptions.h\" -DHAVE_STDINT_H -DOPJ_HAVE_STDINT_H -DHAVE_LIBJPEG"
INC="-I$M/include -I$M/scripts/freetype -I$M/thirdparty/freetype/include -I$M/thirdparty/zlib -I$M/thirdparty/libjpeg
 -I$M/scripts/libjpeg -I$M/thirdparty/jbig2dec -I$M/generated -I$M/source/fitz"
for g in fitz pdf; do for f in "$M"/source/$g/*.c; do echo "$g $f"; done; done > "$O/list"
sed -n 's,^FREETYPE_SRC += ,freetype ,p' "$M/Makelists" >> "$O/list"
for f in adler32 compress crc32 deflate inffast inflate inftrees trees uncompr zutil; do echo "zlib thirdparty/zlib/$f.c"; done >> "$O/list"
grep -o 'jbig2[a-z_]*\.c' "$M/Makelists" | sort -u | sed 's,^,jbig2 thirdparty/jbig2dec/,' >> "$O/list"
grep -o 'thirdparty/libjpeg/j[a-z0-9]*\.c' "$M/Makelists" | sort -u | sed 's,^,jpeg ,' >> "$O/list"
: > "$O/fail"
while read g f; do case $f in /*) p=$f;; *) p=$M/$f;; esac
  o=$O/$g-$(basename "$p" .c).o
  $CC -Os $CFLAGS_EXTRA -std=gnu99 -w -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types $DEFS $INC -c "$p" -o "$o" 2>>"$O/err" || echo "$g $p" >> "$O/fail"
done < "$O/list"
for g in fitz pdf freetype zlib jbig2 jpeg; do ls "$O"/$g-*.o >/dev/null 2>&1 || continue
  $SZ -t "$O"/$g-*.o | tail -1 | awk -v g=$g '{print g, "text", $1, "data", $2, "bss", $3}'; done
echo "failed: $(wc -l < "$O/fail")"
