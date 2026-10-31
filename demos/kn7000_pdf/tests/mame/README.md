# The viewer in MAME

`run_pdf.sh OUT [LUA]` runs the viewer in one MAME session, started as on the instrument: DISK MENU, STYLE CONVERT,
the owner's loader floppy, the program menu on a copy of the owner's SD card, the "PDF viewer (MuPDF)" row, RUN. It
builds the card's `PDF\` folder first (`make card-dir`: the two programs, the 14 font files, `MANIFEST.TXT` and the PDFs
in `TEST_PDFS`, by default the ProTracker manual and the LilyPond score `Bachianinha_No1_duas_vozes.pdf`).

    tests/mame/run_pdf.sh ~/compartilhado/tmp/pdf-mame          # pdf_test.lua
    PDF_FILE=BACHIANI.PDF tests/mame/run_pdf.sh ~/compartilhado/tmp/pdf-mame-score

OUT gets `events.txt` (what the script did and checked, with the emulated time), `snap/` (the snapshots it took),
`run.log` (MAME's output), `versions.txt` (the emulator's, the programs', the floppy's and the card's checksums, and
the PDFs'), `syms.txt` (the programs' symbols, which the script reads the viewer's state through) and `sound.wav`.
OUT is scratch (`~/compartilhado/tmp/...`); the card and floppy copies in it are deleted at the end (`KEEP_SD=1` keeps
the card).

`pdf_test.lua` (its header lists every step): the list up and the PDFs in it; a file opened (R1) and its first page
drawn, with C4 held on the keybed meanwhile and found in KN7APP's notes (the instrument plays while a page draws); the
TEMPO/PROGRAM knob (MAME's adjuster) turned and the page scrolled; R2 (the viewer's) kept from the firmware's switch
bitmap and VARIATION 1 (the firmware's) in it; the wheel on past the page's end to the next page; PAGE UP back; EXIT
to the list; EXIT held, the end. Each check is a `CHECK ok` or `CHECK FAIL` line in `events.txt`, and the last lines
count them.

| variable   | default              | what                                                     |
|------------|----------------------|----------------------------------------------------------|
| `PDF_FILE` | the first listed     | the file to open, by its 8.3 name on the card            |
| `PDF_TURN` | 4                    | detents of the wheel for the scroll check                |
| `PDF_NAME` | `PDF viewer (MuPDF)` | the program menu's row                                   |
| `BIOS`     | `v13`                | the firmware: `v13` program 893 (the owner's), `v16` 941 |
| `TIMEOUT`  | 3600                 | seconds of host time before MAME is stopped              |
| `MAKEVARS` |                      | variables for `make` (`TEST_PDFS=...`)                   |

The other scripts, each run as `run_pdf.sh OUT SCRIPT.lua`:

| script      | answers                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
|-------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `quick.lua` | a short session (about 2.5 minutes of host time): the list, the first file opened, its first page or `$PDF_QUICK_S` s, EXIT held; with `KEEP_SD=1`, `tools/card/fatget.py OUT/sd_after.img PDF/PDFLOG.TXT` reads the log the viewer saved                                                                                                                                                                                                                        |
| `hang.lua`  | whether KN7APP's watchdog (`src/app/crash.c`) finds a viewer that stops yielding: with the first page drawn it sets `hang_test` (`viewer.c`), and CHECKs that the watchdog's screen comes up and its report is saved; `events.txt` lists the 16 PCs it sampled (all in the viewer's loop but those that land in firmware interrupt code), `snap/` has the screen, and with `KEEP_SD=1` `tools/card/fatget.py OUT/sd_after.img 'PDF\PDFLOG.TXT'` shows the report |

Result (2026-10-30, program 893, `wd8`): the watchdog's screen up 5.6 s after `hang_test` was set, 14 of the 16 PCs in
the viewer's loop (`0x503BE444`-`0x503BE44C`), 2 in the firmware's flash, the stack taken at the first sample.
2026-10-31 (`wd10`, with the escape): the screen 5.6 s after, the report on the card 0.1 s later (16 PCs, registers,
stack, the viewer's page and stage, the last glyph: glyph 4 of the score's Emmentaler-20, FreeType's "not drawing").
