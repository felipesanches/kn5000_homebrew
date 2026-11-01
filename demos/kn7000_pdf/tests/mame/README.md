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

Result of `pdf_test.lua` on BACHIANI.PDF with the page cache (2026-10-31, kn5000_homebrew ce189eb, the published
emulator kn7000_mame 2eb75c9, `cache5`): 20 checks, 0 failed. The first page in 16.8 s; the next page drawn ahead
unasked; the page shown scrolled while page 3 was drawn; past the end and PAGE UP showed the pages at once (0.0 s, from
the cache: 162-175 ms each in the viewer's log); the last page, asked for before it was drawn, shown when drawn (15.9 s,
the page TOP answering meanwhile); the pages saved on the card (420-560 ms each); the document opened again showed its
first page from the card in 0.8 s.

PROTRACK.PDF (Type 3 fonts) before and after the glyph procedures were kept, the read cache and the merged fills
(2026-11-01, the published emulator, md5 8b9865bd2e38...): `PDF_FILE=PROTRACK.PDF PDF_PROFILE_S=420 KEEP_SD=1 run_pdf.sh OUT
probes/profile.lua`, each page's time from the viewer's log on the card (`tools/card/fatget.py OUT/sd_after.img
'PDF\PDFLOG01.TXT'` ...); the first build in a worktree at 87f01d5.

| build                          | page 1 | page 2          | page 3 | page 4 | card reads, page 1 |
|--------------------------------|--------|-----------------|--------|--------|--------------------|
| 87f01d5 (the unit's, before)   | 30.3 s |                 |        |        |                    |
| 4e3fd28 (procedures kept)      | 25.0 s | 60.9 s, regions | 26.5 s |        |                    |
| 05c6c2a (and the read cache)   | 23.2 s | 59.6 s, regions | 23.7 s |        | 66 (204 KB)        |
| b2d4b5e (and the fills merged) | 23.2 s | 50.0 s, regions | 24.2 s | 15.6 s | 66 (204 KB)        |

(87f01d5's run kept no card, so only its first page's time, from events.txt.) MAME reads the card faster than the
unit: the viewer's 384948-byte image in 1834 ms, the unit 385012 bytes in 3054 ms (its PDFLOG.TXT of 2026-11-01), so
the reads saved count for more there. Page 2 still runs short of memory in MAME (the heap holds 316 KB after page 1)
and is drawn in regions; its 543 MuPDF messages were not logged. The four pages on the card match kn7pdf's drawing
with no heap limit (no pixel more than 32 of 255 apart). `pdf_test.lua` with b2d4b5e: 20 checks, 0 failed, on
PROTRACK.PDF (first page 23.9 s; the last page asked for, shown when drawn, 16.1 s; opened again, page 1 from the card
in 0.8 s) and on BACHIANI.PDF (first page 16.5 s).
