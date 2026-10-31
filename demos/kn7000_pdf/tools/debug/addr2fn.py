#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# addr2fn.py ELF ADDR... -- the function each address lies in, and how far into it, from the ELF's symbols (nm):
# for the PCs and stack words of KN7APP's watchdog screen and report (src/app/crash.c). addr2line reports "mangled line
# number section" for these builds (the linker's relaxation), so this goes by symbols only. ADDR in hex, with or
# without 0x; NM (default ~/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-nm) the nm to use.
#   python3 tools/debug/addr2fn.py build/pdf/pdf.elf 502F4224 502F45EB
import bisect, os, subprocess, sys

elf, addrs = sys.argv[1], sys.argv[2:]
nm = os.environ.get('NM', os.path.expanduser('~/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-nm'))
syms = []
for line in subprocess.run([nm, '-n', elf], capture_output=True, text=True, check=True).stdout.splitlines():
    f = line.split()
    if len(f) == 3 and f[1] in 'tTwW' and not f[2].startswith('.') and not (f[2][0] == 'L' and f[2][1:2].isdigit()):
        syms.append((int(f[0], 16), f[2]))
syms.sort()
keys = [a for a, _ in syms]
for a in addrs:
    v = int(a, 16)
    i = bisect.bisect_right(keys, v) - 1
    print(f'{v:08X}  ' + (f'{syms[i][1]}+0x{v - syms[i][0]:X}' if i >= 0 else '?'))
