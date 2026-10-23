#!/bin/bash
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# run_fm.sh OUT [LUA] -- KN7FM in one MAME session, through the program menu, as Felipe would run it (fm_test.lua
# says what it plays and checks; LUA picks another script):
#   * a card tree: the program menu (technics-roms-disasm kn7000/tools/kn7menu, built into OUT/menu) as the root
#     KN7APP.BIN, and `make card-dir`'s KN7FM\ folder (plus OUT/extra/*, when there is such a folder: a BANK0.SYX to
#     load, say), copied into a fresh copy of the real KN7000's SD card (kn7menu's fatimg.py put);
#   * the published overlay emulator (EMU, default ~/compartilhado/kn7000-emulator), -bios v13 (program 893, the
#     owner's), the owner's loader floppy as released, -wavwrite OUT/run.wav, the script as -autoboot_script.
# OUT is scratch (~/compartilhado/tmp/..., never /tmp); the card and floppy copies are deleted afterwards.
set -uo pipefail
EXTRA="${EXTRA:-}"            # more MAME options (-nodrc for the interpreter)
HERE="$(cd "$(dirname "$0")" && pwd)"
FM="$(cd "$HERE/../.." && pwd)"
EMU="${EMU:-$HOME/compartilhado/kn7000-emulator}"
KN7="${KN7:-$HOME/compartilhado/technics-roms-disasm/kn7000}"
MENU="$KN7/tools/kn7menu"
FLOPPY="$KN7/tools/cnv_romdump/releases/2026-10-19/floppy/kn7load_floppy.img"
LUA="${2:-$HERE/fm_test.lua}"
O="$(mkdir -p "$1" && cd "$1" && pwd)"
make -s -C "$FM" card-dir > "$O/build.log" 2>&1 || { cat "$O/build.log"; exit 1; }
make -s -C "$MENU" OUT="$O/menu" > "$O/menu.log" 2>&1 || { cat "$O/menu.log"; exit 1; }
rm -rf -- "${O:?}/tree" "${O:?}/snap"; mkdir -p "$O/tree" "$O/snap" "$O/cfg" "$O/nv"
cp "$O/menu/KN7APP.BIN" "$O/tree/KN7APP.BIN"
cp -r "$FM/build/card/KN7FM" "$O/tree/KN7FM"
if [ -d "$O/extra" ]; then cp "$O"/extra/* "$O/tree/KN7FM/"; fi
cp "$EMU/sdcard_from_real_kn7000.img" "$O/sd.img"; chmod u+w "$O/sd.img"
python3 -I "$MENU/fatimg.py" "$O/sd.img" put "$O/tree" > "$O/fatimg.txt"
cp "$FLOPPY" "$O/floppy.img"; chmod u+w "$O/floppy.img"
rm -f -- "$O/run.wav"
{
  echo "emulator md5 $(md5sum < "$EMU/kn7000" | cut -c1-32), bios v13"
  echo "KN7FM KN7APP.BIN $(sha1sum < "$FM/build/KN7APP.BIN" | cut -c1-40), menu $(sha1sum < "$O/menu/KN7APP.BIN" | cut -c1-40)"
  echo "floppy $(sha1sum < "$FLOPPY" | cut -c1-40), card image $(sha1sum < "$EMU/sdcard_from_real_kn7000.img" | cut -c1-40)"
} > "$O/versions.txt"
( cd "$EMU" && env FM_OUT="$O" FM_DEAD=0x500066D8 FM_PANEL=0x5006BE4C DISPLAY="${DISPLAY:-:0}" timeout 3600 \
    ./kn7000 kn7000 -bios v13 -rompath ./roms -pluginspath ./plugins -skip_gameinfo -window -nothrottle \
    -harddisk "$O/sd.img" -flop "$O/floppy.img" -wavwrite "$O/run.wav" \
    -nvram_directory "$O/nv" -cfg_directory "$O/cfg" -snapshot_directory "$O/snap" \
    -autoboot_script "$LUA" $EXTRA ) > "$O/run.log" 2>&1
if [ -n "${KEEP_SD:-}" ]; then mv "$O/sd.img" "$O/sd_after.img"; else rm -f -- "$O/sd.img"; fi
rm -f -- "$O/floppy.img"
rm -rf -- "${O:?}/tree"
tail -5 "$O/run.log"
