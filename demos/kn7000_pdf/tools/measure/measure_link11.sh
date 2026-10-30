#!/bin/sh
# measure_link11.sh OLD11_OBJS NEW_LINK_OBJS MUPDF11_DIR OUT -- link link_main11.c + MuPDF 1.1 objects (measure_old.sh)
# + freetype/zlib/jpeg/jbig2 objects (measure_link.sh's dir) with --gc-sections; image size and what is unresolved.
H=$(dirname "$0"); A=${1:?}; B=${2:?}; M=${3:?}; O=${4:?}
T=$HOME/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-
${T}gcc -Os -ffunction-sections -I"$M/fitz" -I"$M/pdf" -c "$H/link_main11.c" -o "$O.main.o"
${T}gcc -Os -c "$H/link_stubs.c" -o "$O.stubs.o"
${T}gcc -Os -c "$H/link_stubs11.c" -o "$O.stubs11.o"
${T}gcc -Wl,--gc-sections -Wl,-Map="$O.map" "$O.main.o" "$O.stubs.o" "$O.stubs11.o" $(ls "$A"/*.o | grep -v draw_simple_scale) "$B"/freetype-*.o "$B"/zlib-*.o \
  "$B"/jpeg-*.o "$B"/jbig2-*.o -lm -o "$O.elf" 2> "$O.undef"
grep -o "undefined reference to \`[^']*'" "$O.undef" | sort | uniq -c | sort -rn | head -30
grep -m3 "multiple definition" "$O.undef"
${T}size "$O.elf" 2>/dev/null
