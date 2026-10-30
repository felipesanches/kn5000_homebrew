#!/bin/sh
# measure_src.sh MUPDF_DIR NEW_MUPDF_DIR OUT -- like measure_old.sh for the source/{fitz,pdf} layout (MuPDF 1.2-1.x):
# compile source/fitz and source/pdf -Os for the KN7000; text sizes and failures.
M=${1:?}; N=${2:?}; O=${3:?}
T=$HOME/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-
mkdir -p "$O"; : > "$O/fail"; : > "$O/err"
INC="-I$M/include -I$M/generated -I$M/source/fitz -I$N/thirdparty/freetype/include -I$N/scripts/freetype -I$N/thirdparty/zlib
 -I$N/thirdparty/libjpeg -I$N/scripts/libjpeg -I$N/thirdparty/jbig2dec"
for d in fitz pdf; do for f in "$M"/source/$d/*.c; do
  case $f in *jpx*|*memento.c|*-js*.c|*js-*.c|*output-*|*svg*|*html*|*epub*|*writer*|*-write*|*tessocr*) continue;; esac
  $T"gcc" -Os -ffunction-sections -fdata-sections -std=gnu99 -w -Wno-error=implicit-function-declaration \
   -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -DNOCJK -DNO_CJK -DFZ_ENABLE_JS=0 -DNO_ICC -DFZ_ENABLE_ICC=0 \
   -DTOFU -DTOFU_CJK -DNOTO_SMALL $INC -c "$f" -o "$O/$d-$(basename "$f" .c).o" 2>>"$O/err" || echo "$f" >> "$O/fail"
done; ls "$O"/$d-*.o >/dev/null 2>&1 && ${T}size -t "$O"/$d-*.o | tail -1 | awk -v d=$d '{print d, "text", $1, "data", $2}'; done
echo "failed: $(cat "$O/fail" | xargs -n1 basename 2>/dev/null | tr '\n' ' ')"
