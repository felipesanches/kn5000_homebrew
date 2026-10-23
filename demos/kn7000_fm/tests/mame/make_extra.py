#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# make_extra.py OUT -- OUT/extra/BANK1.SYX for fm_test.lua's LOAD: the built-in bank (tools/fm_host.py) as a DX7
# 32-voice dump, its first voice renamed "LOADED V01" so that the load can be seen. run_fm.sh copies OUT/extra/*
# into the card's KN7FM\ folder.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import fm_host as h  # noqa: E402

b = h.bank()
b.patches[0].name = 'LOADED V01'
os.makedirs(os.path.join(sys.argv[1], 'extra'), exist_ok=True)
open(os.path.join(sys.argv[1], 'extra', 'BANK1.SYX'), 'wb').write(b.sysex())
print('make_extra: BANK1.SYX, %d bytes' % len(b.sysex()))
