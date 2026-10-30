#!/bin/sh
# measure_link.sh MUPDF_DIR OUT_DIR -- recompile with -ffunction-sections, link link_main.c with --gc-sections
H=$(dirname "$0"); M=${1:?}; O=${2:?}
T=$HOME/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-
[ -n "$SKIP" ] || CFLAGS_EXTRA="-ffunction-sections -fdata-sections" "$H/measure_mupdf.sh" "$M" "$O" > "$O.sizes"
${T}gcc -Os -I"$M/include" -c "$H/link_main.c" -o "$O/main.o.x"
${T}gcc -Os -c "$H/link_stubs.c" -o "$O/stubs.o.x"
${T}gcc -Wl,--gc-sections -Wl,-Map="$O.map" "$O/main.o.x" "$O/stubs.o.x" "$O"/*.o -lm -o "$O.elf" 2> "$O.undef"
grep -o "undefined reference to \`[^']*'" "$O.undef" | sort | uniq -c | sort -rn | head -30
${T}size "$O.elf" 2>/dev/null
