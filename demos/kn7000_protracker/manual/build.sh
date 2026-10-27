#!/bin/bash
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# build.sh -- the user manual's PDF, from protracker-kn7000-manual.html (and img/: the LCD from the MAME sessions of
# tests/mame, cropped to 640 x 240 and doubled), printed by headless Chromium on A4.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
chromium --headless --disable-gpu --no-sandbox --no-pdf-header-footer \
  --print-to-pdf="$HERE/protracker-kn7000-manual.pdf" "file://$HERE/protracker-kn7000-manual.html" 2>/dev/null
pdfinfo "$HERE/protracker-kn7000-manual.pdf" | grep -E "Pages|Page size"
