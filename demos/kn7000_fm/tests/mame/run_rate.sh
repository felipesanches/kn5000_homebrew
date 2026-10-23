#!/bin/bash
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# run_rate.sh OUT FOLDER NAME -- the effects DSP's frame rate in MAME (rate.lua) while the program menu's program in
# FOLDER (a built card-dir folder: build/card/KN7FM, or DSPTONE's) runs; NAME is its MANIFEST name. EXTRA=-nodrc
# runs MAME's SHARC (and every CPU's) interpreter; RATE_KEYS="C4 E4 ..." holds keybed keys while measuring. OUT/rate.txt. OUT is scratch (~/compartilhado/tmp/...).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
EMU="${EMU:-$HOME/compartilhado/kn7000-emulator}"
KN7="${KN7:-$HOME/compartilhado/technics-roms-disasm/kn7000}"
MENU="$KN7/tools/kn7menu"
FLOPPY="$KN7/tools/cnv_romdump/releases/2026-10-19/floppy/kn7load_floppy.img"
EXTRA="${EXTRA:-}"
O="$(mkdir -p "$1" && cd "$1" && pwd)"
SRC="$(cd "$2" && pwd)"
make -s -C "$MENU" OUT="$O/menu" > "$O/menu.log" 2>&1 || { cat "$O/menu.log"; exit 1; }
rm -rf -- "${O:?}/tree"; mkdir -p "$O/tree" "$O/cfg" "$O/nv"
cp "$O/menu/KN7APP.BIN" "$O/tree/KN7APP.BIN"
cp -r "$SRC" "$O/tree/"
cp "$EMU/sdcard_from_real_kn7000.img" "$O/sd.img"; chmod u+w "$O/sd.img"
python3 -I "$MENU/fatimg.py" "$O/sd.img" put "$O/tree" > "$O/fatimg.txt"
cp "$FLOPPY" "$O/floppy.img"; chmod u+w "$O/floppy.img"
( cd "$EMU" && env RATE_OUT="$O" RATE_NAME="$3" RATE_KEYS="${RATE_KEYS:-}" RATE_POLY_UP="${RATE_POLY_UP:-0}" DISPLAY="${DISPLAY:-:0}" timeout 3600 \
    ./kn7000 kn7000 -bios v13 -rompath ./roms -pluginspath ./plugins -skip_gameinfo -window -nothrottle \
    -harddisk "$O/sd.img" -flop "$O/floppy.img" -nvram_directory "$O/nv" -cfg_directory "$O/cfg" \
    -autoboot_script "$HERE/rate.lua" $EXTRA ) > "$O/run.log" 2>&1
rm -f -- "$O/sd.img" "$O/floppy.img"
rm -rf -- "${O:?}/tree"
cat "$O/rate.txt"
