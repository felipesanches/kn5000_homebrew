#!/bin/bash
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# build.sh -- the user manual's PDF, from keyfall-kn7000-manual.html (img/: the LCD from the MAME sessions of
# tests/mame, cropped to 640 x 240 and doubled; fonts/: Archivo, SIL OFL), printed by headless Chromium on A4. The
# page's script marks any page whose text does not fit (data-overflow); then no PDF is written.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
URL="file://$HERE/keyfall-kn7000-manual.html"
CHROME=(chromium --headless --disable-gpu --no-sandbox --virtual-time-budget=10000)
DOM="$("${CHROME[@]}" --dump-dom "$URL" 2>/dev/null)"
OVER="$(sed -n 's/.*data-overflow="\([^"]*\)".*/\1/p' <<< "$DOM" | head -1)"
PAGES="$(sed -n 's/.*data-pages="\([^"]*\)".*/\1/p' <<< "$DOM" | head -1)"
[ -n "$PAGES" ] || { echo "build.sh: the page's script did not run" >&2; exit 1; }
[ -z "$OVER" ] || { echo "build.sh: the text runs over on page(s) $OVER" >&2; exit 1; }
"${CHROME[@]}" --no-pdf-header-footer --print-to-pdf="$HERE/keyfall-kn7000-manual.pdf" "$URL" 2>/dev/null
pdfinfo "$HERE/keyfall-kn7000-manual.pdf" | grep -E "Pages|Page size"
[ "$(pdfinfo "$HERE/keyfall-kn7000-manual.pdf" | sed -n 's/^Pages: *//p')" = "$PAGES" ] ||
  { echo "build.sh: the PDF's pages are not the $PAGES of the HTML" >&2; exit 1; }
pdffonts "$HERE/keyfall-kn7000-manual.pdf" | grep -q Archivo || { echo "build.sh: Archivo is not in the PDF" >&2; exit 1; }
