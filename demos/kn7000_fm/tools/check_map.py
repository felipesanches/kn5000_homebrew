#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# check_map.py build/kn7fm.map -- does the image fit below its variables (src/kn7fm.inc IMAGE_MAX), and do the
# variable areas of src/kn7fm.inc, in their order there, leave room for what each holds (the patch's words from
# build/dsp_equ.inc)? Stops the build when not.
import re
import sys

labels = {}
for line in open(sys.argv[1]):
    a, name = line.split()
    labels[name] = int(a, 16)
inc = open(__file__.replace('tools/check_map.py', 'src/kn7fm.inc')).read()
image_max = int(re.search(r'\.equ IMAGE_MAX,\s+(0x[0-9A-Fa-f]+)', inc).group(1), 16)
equ = dict((k, int(v, 0)) for k, v in re.findall(r'\.equ (\w+),\s+(0x[0-9A-Fa-f]+|\d+)', inc))
dsp = dict((k, int(v, 0)) for k, v in re.findall(r'\.equ (\w+),\s+(0x[0-9A-Fa-f]+|\d+)',
                                                 open(sys.argv[1].replace('kn7fm.map', 'dsp_equ.inc')).read()))
need = {'STAGEBUF': 4 * dsp['DSP_STAGE_WORDS'], 'PIMG': 4 * dsp['DSP_PATCH_WORDS'], 'PSENT': 4 * dsp['DSP_PATCH_WORDS'],
        'TMPSTR': 64, 'PATHBUF': 48, 'PREFIX': 16, 'BANK': 32 * equ['VCED_LEN'], 'FILEBUF': equ['SYX_LEN'] + 64}
order = ['FW_VARS', 'FWV_VARS', 'DSP_HOST', 'M_VARS', 'NOTES', 'VOICEV', 'STAGEBUF', 'PIMG', 'PSENT', 'TMPSTR',
         'PATHBUF', 'PREFIX', 'BANK', 'FILEBUF', 'AREA_END']
for a, b in zip(order, order[1:]):
    if equ[a] + need.get(a, 0) > equ[b]:
        sys.exit(f'check_map: {a} (0x{equ[a]:08X}, {need.get(a, 0)} bytes) runs into {b} (0x{equ[b]:08X})')
if equ['AREA_END'] > 0x502E7F00:
    sys.exit('check_map: the variables reach the launch block (LB_ADDR)')
end = labels['IMAGE_END']
if end > image_max:
    sys.exit(f'check_map: the image ends at 0x{end:08X}, past IMAGE_MAX 0x{image_max:08X}')
print(f'check_map: the image ends at 0x{end:08X} ({end - 0x502BA000} bytes), below IMAGE_MAX 0x{image_max:08X}')
