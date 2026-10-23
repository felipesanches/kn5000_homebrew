#!/bin/sh
# fetch.sh DIR -- msfa (Raph Levien's DX7 engine, Google, Apache License 2.0) as Dexed carries it, at a pinned Dexed
# commit, into DIR: the files msfa_ref.cpp builds against. Nothing of msfa is committed to this repository.
set -e
REV=fea2f0f0f2c397799f59b27eb11c038cd34b22c5
D=${1:?usage: fetch.sh DIR}
mkdir -p "$D/msfa" "$D/debugger"
for f in synth.h aligned_buf.h env.cc env.h exp2.cc exp2.h fm_core.cc fm_core.h fm_op_kernel.cc fm_op_kernel.h \
         freqlut.cc freqlut.h lfo.cc lfo.h pitchenv.cc pitchenv.h sin.cc sin.h controllers.h; do
    curl -sfL -o "$D/msfa/$f" "https://raw.githubusercontent.com/asb2m10/dexed/$REV/source/msfa/$f"
done
: > "$D/debugger/Logger.h"
echo "msfa at dexed $REV in $D/msfa"
