#!/bin/sh
# heap_min.sh KN7PDF FILE.PDF SCRATCH_DIR [WIDTH=640] [BAND=32] -- the smallest heap (KB, to 16 KB) with which kn7pdf draws
# every page of FILE.PDF exactly as it does with no limit (the PGMs compared byte for byte), the store unlimited
# (MuPDF frees what it caches when an allocation fails). A page drawn differently, or an error, counts as not enough.
K=${1:?kn7pdf}; F=${2:?pdf}; S=${3:?scratch}; W=${4:-640}; B=${5:-32}
mkdir -p "$S/ref" "$S/try"
"$K" "$F" "$W" "$B" 1048576 "$S/ref" > "$S/ref.txt" || { echo "no limit: errors"; exit 1; }
ok() {  # ok KB: 0 when all pages come out the same
  rm -f -- "$S"/try/*.pgm
  "$K" "$F" "$W" "$B" 1048576 "$S/try" "$1" > "$S/try.txt" 2>&1 || return 1
  for p in "$S"/ref/*.pgm; do cmp -s "$p" "$S/try/$(basename "$p")" || return 1; done
}
lo=64; hi=$(awk '/worst heap peak/ {print $4 + 16}' "$S/ref.txt")
ok "$hi" || { echo "not even at $hi KB"; exit 1; }
while [ $((hi - lo)) -gt 16 ]; do mid=$(((lo + hi) / 2)); if ok "$mid"; then hi=$mid; else lo=$mid; fi; done
echo "$(basename "$F"): smallest heap $hi KB (unlimited peak $(awk '/worst heap peak/ {print $4}' "$S/ref.txt") KB)"
