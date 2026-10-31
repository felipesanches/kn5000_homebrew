#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# check_flags.py OBJDUMP ELF... -- refuses a program with an instruction that reads the PSW's flags too soon after
# they were changed. The MN1030/MN103S Series Instruction Manual (Chapter 3, "Dangerous Code Sequences", 2-3 and 2-4,
# cores AM30, AM31 and AM32) says a flag update reaches the PSW one cycle late: ADDC, SUBC, ROR and ROL right after an
# instruction that updates the flags use the flags from before it, and after a write to the PSW (AND imm16,PSW,
# OR imm16,PSW, MOV Dm,PSW) they need two cycles. Branches (Bcc, Lcc) are not affected. On the unit this made
# FreeType's FT_MulFix_64 (add 32768,a0 / addc d2,d3) add the carry of the asr before it, and every negative result
# came out 65536 too high (notes/UNIT-RUNS.md, 2026-10-31); MAME does not model the delay. The toolchain's GCC puts a
# nop before each addc and subc it makes (toolchain/gcc-kn7000-flags.patch); this catches the rest: the libraries'
# assembly, and a compiler without the patch. Looks at each instruction's one or two predecessors in address order
# (objdump -d); prints each one found, exits 1 if any.
import re, subprocess, sys

READERS = {'addc', 'subc', 'ror', 'rol'}
# the manual's "flag update instructions" (ADD but ADD imm,SP; AND and OR but to the PSW; INC Dn)
UPDATERS = {'clr', 'add', 'addc', 'sub', 'subc', 'mulu', 'mul', 'div', 'divu', 'inc', 'cmp', 'and', 'or', 'xor',
            'not', 'btst', 'bset', 'bclr', 'asr', 'lsr', 'asl', 'asl2', 'ror', 'rol'}

def kind(mnem, ops):
    dest = ops.split(',')[-1].strip() if ops else ''
    if mnem in ('and', 'or') and dest == 'psw' or mnem == 'mov' and dest == 'psw':
        return 'write'   # a write to the PSW: readers need two instructions after it
    if mnem == 'add' and dest == 'sp':
        return None
    if mnem == 'inc' and not dest.startswith('d'):
        return None
    return 'update' if mnem in UPDATERS else None

found = 0
for elf in sys.argv[2:]:
    out = subprocess.run([sys.argv[1], '-d', elf], capture_output=True, text=True, check=True).stdout
    fn, last = '?', [None, None]   # the two instructions before: (kind, text)
    for line in out.splitlines():
        m = re.match(r'([0-9a-f]+) <(.*)>:$', line)
        if m:
            fn = m.group(2) if not m.group(2).startswith('.') else fn
            continue
        if line.startswith('Disassembly of section'):
            last = [None, None]
            continue
        m = re.match(r'\s*([0-9a-f]+):\t(?:[0-9a-f]{2} ?)+\s*\t(\S+)\s*(.*)', line)
        if not m:
            continue
        addr, mnem, ops = m.group(1), m.group(2), m.group(3).split(';')[0].strip()
        if mnem in READERS:
            k1, k2 = last[1], last[0]
            if k1 and k1[0] in ('update', 'write') or k2 and k2[0] == 'write':
                why = k1[1] if k1 and k1[0] else k2[1]
                print(f'check_flags: {elf}: 0x{addr} in {fn}: {mnem} {ops} right after {why}')
                found += 1
        last = [last[1], (kind(mnem, ops), f'{mnem} {ops}'.strip())]
print(f'check_flags: {found} flag reads too soon after a flag change in {len(sys.argv) - 2} programs')
sys.exit(1 if found else 0)
