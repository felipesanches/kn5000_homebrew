#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# check_map.py build/kn7fm.map -- does the image fit below its variables (src/kn7fm.inc IMAGE_MAX)? Stops the build
# when it does not.
import re
import sys

labels = {}
for line in open(sys.argv[1]):
    a, name = line.split()
    labels[name] = int(a, 16)
inc = open(__file__.replace('tools/check_map.py', 'src/kn7fm.inc')).read()
image_max = int(re.search(r'\.equ IMAGE_MAX,\s+(0x[0-9A-Fa-f]+)', inc).group(1), 16)
end = labels['IMAGE_END']
if end > image_max:
    sys.exit(f'check_map: the image ends at 0x{end:08X}, past IMAGE_MAX 0x{image_max:08X}')
print(f'check_map: the image ends at 0x{end:08X} ({end - 0x502BA000} bytes), below IMAGE_MAX 0x{image_max:08X}')
