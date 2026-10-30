#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU Affero General Public License v3 or later
#
# cardname.py FILE -- an 8.3 name for FILE on the card (the program menu's FAT16 reader takes short names only): the
# first 8 letters and digits of its name, upper case, and its extension's first 3.
import os
import re
import sys

stem, ext = os.path.splitext(os.path.basename(sys.argv[1]))
stem = re.sub(r'[^A-Za-z0-9]', '', stem).upper()[:8] or 'FILE'
ext = re.sub(r'[^A-Za-z0-9]', '', ext).upper()[:3]
print(stem + ('.' + ext if ext else ''))
