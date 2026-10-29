#!/bin/bash
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# run_doom.sh OUT [LUA] -- Doom in one MAME session, through the program menu, as it is started on the instrument
# (doom_test.lua says what it does and checks; LUA picks another script). As ProTracker's tests/mame/run_pt.sh:
#   * a card tree: the program menu (technics-roms-disasm kn7000/tools/kn7menu, built into OUT/menu) as the root
#     KN7APP.BIN, and the DOOM\ folder (`make card-dir`: KN7APP.BIN, DOOM.BIN, MANIFEST.TXT and the WAD), copied into a
#     fresh copy of the real KN7000's SD card (kn7menu's fatimg.py put);
#   * the published overlay emulator (EMU, default ~/compartilhado/kn7000-emulator), -bios v13 (program 893, the
#     owner's; BIOS=v16 for 941), the owner's loader floppy as released, the script as -autoboot_script; the sound to
#     OUT/sound.wav.
# MAKEVARS: variables for make (WAD_CACHE=0 ...). OUT is scratch (~/compartilhado/tmp/..., never /tmp); the card and floppy copies are deleted afterwards (KEEP_SD=1
# keeps the card). The builds' symbols go to OUT/syms.txt for the script.
set -uo pipefail
EXTRA="${EXTRA:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
DOOM="$(cd "$HERE/../.." && pwd)"
EMU="${EMU:-$HOME/compartilhado/kn7000-emulator}"
KN7="${KN7:-$HOME/compartilhado/technics-roms-disasm/kn7000}"
MENU="$KN7/tools/kn7menu"
FLOPPY="$KN7/tools/cnv_romdump/releases/2026-10-19/floppy/kn7load_floppy.img"
LUA="$(realpath "${2:-$HERE/doom_test.lua}")"
BIOS="${BIOS:-v13}"
O="$(mkdir -p "$1" && cd "$1" && pwd)"
make -s -C "$DOOM" card-dir tests/mame/offsets.txt ${MAKEVARS:-} > "$O/build.log" 2>&1 || { cat "$O/build.log"; exit 1; }
make -s -C "$MENU" OUT="$O/menu" > "$O/menu.log" 2>&1 || { cat "$O/menu.log"; exit 1; }
NM=~/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-nm
{ "$NM" "$DOOM/build/app/kn7app.elf"; "$NM" "$DOOM/build/doom/doom.elf"; } > "$O/syms.txt"
rm -rf -- "${O:?}/tree" "${O:?}/snap"; mkdir -p "$O/tree" "$O/snap" "$O/cfg" "$O/nv"
cp "$O/menu/KN7APP.BIN" "$O/tree/KN7APP.BIN"
cp -r "$DOOM/build/card/DOOM" "$O/tree/DOOM"
cp "$EMU/sdcard_from_real_kn7000.img" "$O/sd.img"; chmod u+w "$O/sd.img"
python3 -I "$MENU/fatimg.py" "$O/sd.img" put "$O/tree" > "$O/fatimg.txt"
cp "$FLOPPY" "$O/floppy.img"; chmod u+w "$O/floppy.img"
{
  echo "emulator md5 $(md5sum < "$EMU/kn7000" | cut -c1-32), bios $BIOS"
  echo "KN7APP.BIN $(sha1sum < "$DOOM/build/KN7APP.BIN" | cut -c1-40), DOOM.BIN $(sha1sum < "$DOOM/build/DOOM.BIN" | cut -c1-40), menu $(sha1sum < "$O/tree/KN7APP.BIN" | cut -c1-40)"
  echo "WAD $(md5sum < "$DOOM/build/card/DOOM/DOOM1.WAD" | cut -c1-32) (md5), floppy $(sha1sum < "$FLOPPY" | cut -c1-40), card image $(sha1sum < "$EMU/sdcard_from_real_kn7000.img" | cut -c1-40)"
} > "$O/versions.txt"
( cd "$EMU" && env DOOM_OUT="$O" DOOM_SYMS="$O/syms.txt" DOOM_OFFSETS="$HERE/offsets.txt" DISPLAY="${DISPLAY:-:0}" timeout -s KILL "${TIMEOUT:-3600}" \
    ./kn7000 kn7000 -bios "$BIOS" -rompath ./roms -pluginspath ./plugins -skip_gameinfo -window -nothrottle \
    -harddisk "$O/sd.img" -flop "$O/floppy.img" -wavwrite "$O/sound.wav" \
    -nvram_directory "$O/nv" -cfg_directory "$O/cfg" -snapshot_directory "$O/snap" \
    -autoboot_script "$LUA" $EXTRA ) > "$O/run.log" 2>&1
if [ -n "${KEEP_SD:-}" ]; then mv "$O/sd.img" "$O/sd_after.img"; else rm -f -- "$O/sd.img"; fi
rm -f -- "$O/floppy.img"
rm -rf -- "${O:?}/tree"
tail -5 "$O/run.log"
