#!/bin/bash
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# get_wad.sh [DIR] -- the shareware DOOM1.WAD (v1.9, md5 f0cefca49926d00903cf57551d901abe) into DIR (default
# ~/compartilhado/Doom), from id Software's shareware release doom19s.zip on an idgames mirror: its two DEICE parts
# are one LHA archive, which 7z unpacks. The Makefile's WAD is DIR/DOOM1.WAD. Scratch in ~/compartilhado/tmp.
set -euo pipefail
DIR="${1:-$HOME/compartilhado/Doom}"
URL="${URL:-https://youfailit.net/pub/idgames/idstuff/doom/doom19s.zip}"
MD5=f0cefca49926d00903cf57551d901abe
mkdir -p "$DIR"
T="$(mktemp -d "$HOME/compartilhado/tmp/get-wad.XXXXXX")"
curl -sSfL -o "$T/doom19s.zip" "$URL"
( cd "$T" && unzip -q doom19s.zip && cat DOOMS_19.1 DOOMS_19.2 > doom19s.lzh && 7z e -y doom19s.lzh DOOM1.WAD > /dev/null )
echo "$MD5  $T/DOOM1.WAD" | md5sum -c --quiet
mv "$T/DOOM1.WAD" "$DIR/DOOM1.WAD"
rm -rf -- "${T:?}"
echo "get_wad.sh: $DIR/DOOM1.WAD"
