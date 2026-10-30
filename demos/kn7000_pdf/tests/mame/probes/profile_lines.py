#!/usr/bin/env python3
"""profile_lines.py OUT/profile.txt ELF... [-n N] -- profile.lua's PC counts summed by source line and by function
(addr2line on the ELFs: build/pdf/pdf.elf, build/app/kn7app.elf); PCs in neither are the firmware's."""
import subprocess, sys, collections
args = [a for a in sys.argv[1:] if not a.startswith('-n')]
n = 40
prof, elfs = args[0], args[1:]
rows = []
for line in open(prof).read().splitlines()[1:]:
    c, pc, *_ = line.split()
    rows.append((int(c), int(pc, 16)))
total = sum(c for c, _ in rows)
A2L = '/home/fsanches/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-addr2line'
NM = '/home/fsanches/compartilhado/tools/mn10300-elf-kn7000/bin/mn10300-elf-nm'
import bisect
funcs = []  # (address, name) of the ELFs' function symbols, for code without line information (libgcc, newlib)
for elf in elfs:
    for line in subprocess.run([NM, '-n', elf], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'Tt' and not p[2].startswith('.'):
            funcs.append((int(p[0], 16), p[2]))
funcs.sort()
def func_of(pc):
    i = bisect.bisect_right(funcs, (pc, '\uffff')) - 1
    return funcs[i][1] if i >= 0 else '??'
where = {}
for elf in elfs:
    pcs = [pc for _, pc in rows if pc not in where]
    out = subprocess.run([A2L, '-f', '-s', '-e', elf] + [hex(p) for p in pcs], capture_output=True, text=True).stdout.split('\n')
    for i, pc in enumerate(pcs):
        fn, ln = out[2 * i], out[2 * i + 1]
        if fn.startswith('.'): fn = func_of(pc)
        if fn != '??': where[pc] = (fn, ln)
byfn, byline = collections.Counter(), collections.Counter()
for c, pc in rows:
    fn, ln = where.get(pc, ('firmware', '%08X' % (pc & ~0xFFF)))
    byfn[fn] += c; byline[fn + ' ' + ln] += c
print(f'{total} samples')
for k, v in byfn.most_common(n): print(f'{v:6d} {100 * v / total:5.1f}% {k}')
print()
for k, v in byline.most_common(n): print(f'{v:6d} {100 * v / total:5.1f}% {k}')
