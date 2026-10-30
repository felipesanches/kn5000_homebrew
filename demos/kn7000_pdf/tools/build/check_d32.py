#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# check_d32.py OBJDUMP [--allow FUNCTION]... ELF... -- refuses a program with an instruction the KN7000 restarts on (README.md, "If it
# does not start"): a load or store whose operand is a register plus a 32-bit displacement. On the unit, probes
# PFT7 (movbu (d32,An),Dn) and PFT9 (mov (d32,An),Dn) restarted it and PFT8, the same code without one, worked. The
# base instruction set's forms with such an operand, from binutils' m10300-opc.c, are 0xFC followed by
#   0x00..0x7F  mov/movbu/movhu (d32,Am) loads and stores, Dn and An;
#   0x90..0x93  mov/movbu/movhu Dm,(d32,sp) and mov Am,(d32,sp);
#   0xB0..0xBF  mov/movbu/movhu (d32,sp) loads, Dn and An;
# and the AM33 ones, 0xFD and 0xFE followed by x0A..xDA (an AM33 instruction is refused anyway). The absolute
# forms (0xFC 0x80..0x83, 0xA0..0xAF) are used by every program that has run on the unit. The toolchain's GCC is
# patched to make none (toolchain/gcc-kn7000-no-d32.patch); this catches the rest: the libraries' assembly, and a
# compiler without the patch. Reads the executable sections (objdump -d); prints each one found, exits 1 if any.
# --allow FUNCTION lets one function have them: the crash screen's self-test (make crash-selftest).
import re, subprocess, sys

def bad(b0, b1):
    if b0 == 0xFC:
        return b1 < 0x80 or 0x90 <= b1 <= 0x93 or 0xB0 <= b1 <= 0xBF
    return b0 in (0xFD, 0xFE) and (b1 & 0x0F) == 0x0A

args = sys.argv[2:]
allow = set()
while args[:1] == ['--allow']:
    allow.add(args[1])
    args = args[2:]
found = 0
for elf in args:
    out = subprocess.run([sys.argv[1], '-d', elf], capture_output=True, text=True, check=True).stdout
    fn = '?'
    for line in out.splitlines():
        m = re.match(r'([0-9a-f]+) <(.*)>:$', line)
        if m:
            fn = m.group(2) if not m.group(2).startswith('.') else fn
            continue
        m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f]{2}) ([0-9a-f]{2}) [^\t]*\t(.*)', line)
        if m and bad(int(m.group(2), 16), int(m.group(3), 16)) and fn not in allow:
            print(f'check_d32: {elf}: 0x{m.group(1)} in {fn}: {m.group(4).strip()}')
            found += 1
print(f'check_d32: {found} register+d32 operands in {len(args)} programs' + (f' (allowed in {", ".join(sorted(allow))})' if allow else ''))
sys.exit(1 if found else 0)
