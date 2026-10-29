#!/bin/bash
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# sweep.sh WAD SCRATCH TEST... -- the smallest zone (in KB, to 16 KB) each test runs in, with the host build
# (`make host`): demoN plays demo N with -timedemo to its end; mapN warps to E1MN and draws 600 frames standing still.
# SCRATCH is a folder for Chocolate Doom's configuration (~/compartilhado/tmp/..., never /tmp).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
DOOM="$HERE/../../build/host/doom"
WAD="$(realpath "$1")"; mkdir -p "$2"; cd "$2" || exit 1; shift 2
ok() { # $1 test, $2 kb
  case $1 in
    demo*) DOOM_ZONE_KB=$2 timeout 120 "$DOOM" -iwad "$WAD" -nosound -timedemo "$1" 2>&1 | grep -q "^timed" ;;
    map*)  DOOM_ZONE_KB=$2 DOOM_FRAMES=600 timeout 120 "$DOOM" -iwad "$WAD" -nosound -warp 1 "${1#map}" 2>&1 | grep -q "^drew" ;;
  esac
}
for t in "$@"; do
  lo=64; hi=8192
  ok "$t" $hi || { echo "$t: fails at $hi KB"; continue; }
  while [ $((hi - lo)) -gt 16 ]; do mid=$(((lo + hi) / 2)); if ok "$t" $mid; then hi=$mid; else lo=$mid; fi; done
  echo "$t: $hi KB"
done
