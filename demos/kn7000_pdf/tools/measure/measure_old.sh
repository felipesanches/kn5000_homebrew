#!/bin/sh
# measure_old.sh OLD_MUPDF_DIR NEW_MUPDF_DIR OUT -- compile an old MuPDF's fitz/draw/pdf (1.1 layout) with the
# KN7000 toolchain -Os (thirdparty headers from the new tree); print text sizes per directory and failures.
M=${1:?}; N=${2:?}; O=${3:?}
T=$HOME/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-
mkdir -p "$O"; : > "$O/fail"; : > "$O/err"
INC="-I$M/fitz -I$M/pdf -I$M/xps -I$M/cbz -I$M/draw -I$N/thirdparty/freetype/include -I$N/scripts/freetype -I$N/thirdparty/zlib
 -I$N/thirdparty/libjpeg -I$N/scripts/libjpeg -I$N/thirdparty/jbig2dec -I$N/thirdparty/openjpeg/libopenjpeg"
for d in fitz draw pdf; do for f in "$M"/$d/*.c; do
  case $f in *image_jpx.c|*filt_jpxd.c|*memento.c|*arch_arm.c|*image_save.c) continue;; esac
  $T"gcc" -Os -ffunction-sections -fdata-sections -std=gnu99 -w -Wno-error=implicit-function-declaration \
   -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -DNOCJK -DNO_CJK $INC -c "$f" -o "$O/$d-$(basename "$f" .c).o" 2>>"$O/err" || echo "$f" >> "$O/fail"
done; ls "$O"/$d-*.o >/dev/null 2>&1 && ${T}size -t "$O"/$d-*.o | tail -1 | awk -v d=$d '{print d, "text", $1, "data", $2}'; done
echo "failed: $(cat "$O/fail" | xargs -n1 basename 2>/dev/null | tr '\n' ' ')"
