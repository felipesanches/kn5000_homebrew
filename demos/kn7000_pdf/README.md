# A PDF viewer for the KN7000

Sheet music in PDF on the Technics SX-KN7000's LCD, while the instrument plays on: **MuPDF 1.1** (Artifex, GPL-3.0),
the "lightweight PDF viewer", run by the KN7000 program loader (`KN7LOAD.CNV`, DISK MENU > STYLE CONVERT) from the
program menu's folder `PDF\` on the SD card. The page is drawn as wide as the LCD and the TEMPO/PROGRAM wheel scrolls
it; the keybed and the rest of the panel stay the instrument's.

> **Status (2026-11-06): runs in MAME (program 893) and on Felipe's KN7000.** On the unit (program 893, 2026-10-30
> to 2026-11-01: `notes/UNIT-RUNS.md`) build 87f01d5 drew all 5 pages of the LilyPond score
> `Bachianinha_No1_duas_vozes.pdf` and all 15 of the ProTracker port's manual, saved them to the card, and reopened
> showed page 1 from the card in 818 and 621 ms. The builds since then have run in MAME only
> ([What was checked](#what-was-checked)).

Contents: [Using it](#using-it) · [The card](#the-card) · [How it works](#how-it-works) · [Memory](#memory) ·
[Building](#building) · [What was checked](#what-was-checked) · [Licences](#licences) · [Files](#files)

## Using it

DISK MENU, STYLE CONVERT, `KN7LOAD.CNV`, OK; then the program menu's "PDF viewer (MuPDF)" row, RUN. The folder's PDFs
are listed; the wheel or UP/DOWN chooses one, OPEN opens it, and its first page is drawn (15 s for a page of music in
MAME). The page fills the LCD's width; the LCD shows 240 of its lines at a time.

While the page is read, the next pages are drawn behind it: the next, the one after, the one before. Turning the wheel
past the page's end shows the next page at once when it is drawn already; a page asked for that is not drawn yet is
drawn first, and the page shown can be read and scrolled meanwhile (the status line says "drawing page N..."). Each page
drawn is saved on the card once the controls have been left alone a moment, and the next time the document is opened
its pages come from there in about a second.

| control                    | in the list | on a page                                                                 |
|----------------------------|-------------|---------------------------------------------------------------------------|
| TEMPO/PROGRAM wheel        | the file    | scrolls the page; past its end the next page, past its top the one before |
| PAGE UP, PAGE DOWN         |             | the page before, the page after                                           |
| LCD left 1 (FILES)         |             | back to the list                                                          |
| LCD left 2, 3 (PREV, NEXT) |             | the page before, the page after                                           |
| LCD left 4 (FIRST)         |             | the first page                                                            |
| LCD left 5 (LOG)           | the log     | the log (MuPDF's warnings and errors, the pages' times)                   |
| LCD right 1                | OPEN        | TOP: the page's top                                                       |
| LCD right 2, 3             | UP, DOWN    |                                                                           |
| LCD right 5                |             | END: the page's bottom                                                    |
| EXIT                       |             | back to the list                                                          |
| EXIT held 1.5 s            | the end     | the end                                                                   |

Everything else on the panel does what it always does, and the keybed plays, also while a page is drawn (the tempo
does not change while the wheel scrolls). The status line at the bottom names the file and the page; when memory ran
short while a page was drawn it says so ("it may lack something").

## The card

| file           | what                                                                                                                    |
|----------------|-------------------------------------------------------------------------------------------------------------------------|
| `KN7APP.BIN`   | the platform: firmware search, task, files, the panel shared with the firmware, the frame buffers lent (Doom's, ABI 14) |
| `PDF.BIN`      | the viewer: MuPDF 1.1, FreeType, zlib, libjpeg, jbig2dec, and `src/kn7/`                                                |
| `MANIFEST.TXT` | the name the program menu shows and its folder (Tools)                                                                  |
| `*.CFF`        | the 14 standard PDF fonts (URW's Nimbus, from MuPDF 1.1), read only by a PDF that does not embed them                   |
| `*.PDF`        | the documents: copy any PDF in (an 8.3 name, as the computer makes it)                                                  |
| `K*.PGC`       | the pages drawn, one a file (`Kxxxxnnn.PGC`: xxxx the document's name and size, nnn the page): delete them at will      |

To look at the pages drawn on a computer: `python3 tools/card/pgc2png.py -o DIR CARD/PDF` makes a PNG of each `.PGC`
(`BACHIANI-p001.png`), with Python 3 alone; KN7000's `update_sdcard.sh` brings the `.PGC` files back from the card and
converts them into `from-card/<date-time>/PDF/png/`.

## How it works

Two programs, as Doom's and ProTracker's (`src/common/pfhost.h` between them):

- **`KN7APP.BIN`** (`src/app/`, Doom's, ABI 14): the loader runs it on each converter call. It starts a task for the
  viewer and serves its file reads; it takes from the firmware only the switches the viewer names (`panelTake`: the
  ten soft keys, PAGE UP/DOWN, EXIT) and the wheel, before the firmware decodes them, and leaves it everything else; it
  keeps the firmware's drawing off; it loads PDF.BIN's image and its segments (`PfSegment`).
- **`PDF.BIN`** (`src/kn7/`, `src/mupdf/`, `src/third/`): MuPDF 1.1 with its changes behind `KN7PDF`, and the viewer.

**Drawing a page** (`src/kn7/view.c`). The page is run once into MuPDF's display list, then drawn from it 32 lines at
a time into a gray band, each band made 4 bits a pixel and coded (PackBits, a row at a time) into the page cache. When memory runs short while the list is made, each band runs the page's content again instead; a band that runs
short is drawn again half as tall.

**The page cache** (`src/kn7/pagecache.c`). The display memory the page shown does not take (the page is 4 bits a pixel,
so half of the 643 KB an 8-bit page took) is a second heap, the display heap: the coded pages (29-69 KB each, 9-23% of a
page: `tests/host/packbits_size.py`) and, up to 128 KB, MuPDF's font files and the Type 3 glyph procedures it has
loaded (a Type 3 font's glyphs are content streams in the PDF: kept until the document is closed, they are read from
the card once instead of each time a glyph is drawn). When it is full, the page used longest ago goes, one the card has
first. The cards' copies are the coded page with a header naming the document, the page, the
width and the anti-aliasing; one that does not match is not used.

**Reading the PDF** (`src/kn7/readcache.c`). The card is slow: a read costs about 5 ms and 3.9 ms a 512-byte sector
more (the unit's log), so MuPDF reads at most 1 KB at a time, and 32 KB of display memory above the display heap keep
the file's 1 KB blocks read last; a miss reads only the blocks missing, and 4 more when the reads run on. On
PROTRACK.PDF's 15 pages this, with the glyph procedures kept, takes the card's time from 427 s to 35 s
(`tests/host/kn7pdf.c`'s count and cost model at an 825 KB heap: `KN7PDF_SLOW_KB=0` reads as 87f01d5 did, 10632
reads; `KN7PDF_READ_MAX=1024 KN7PDF_RC_KB=32` as now, 1526); BACHIANI.PDF's 5 from 8.0 s to 6.0 s.

**Drawing ahead** (`src/kn7/viewer.c`). The viewer's loop serves the page asked for first, then saves a page drawn when
the controls have been left alone 1.5 s, then draws ahead. While MuPDF draws, its yield (every 20 ms) serves the wheel and
the switches (`ui()`): the page shown scrolls, a page drawn already is shown at once, and a page drawing that is no longer
wanted is stopped (`view_abort`, MuPDF's cookie). A page that does not fit in the cache is drawn straight onto the LCD.

**Showing it** (`src/kn7/viewer.c`). The page is decoded from the cache whole into display memory, 4 bits a pixel (up
to 1,004 lines at 640 wide: A4 is 906, Letter 828; the S1D13506 Hardware Functional Specification's Figure 11-1 and
section 15.3: two pixels a byte, the first in bits 7-4, fetched backwards in the 180-degree SwivelView the KN7000 uses);
the controller shows 240 lines of it from its start address, so the wheel scrolls without a pixel moving.
The labels and the status line are in the controller's ink layer (2 bits a dot over the picture), which stays still.

**The instrument meanwhile.** The viewer's task runs at the firmware's priority and MuPDF gives the CPU to the firmware
every 20 ms of its work (`fz_kn7_yield`), so the keybed sounds, the panel's buttons act and MIDI flows while a page is
drawn. The firmware's tasks give the CPU only to each other: a task below them (priority 4) ran once and never again.

**What MuPDF 1.1 needed** (each change in `src/mupdf/` behind `#ifdef KN7PDF`):

| change                                                                                                                             | where                                                        |
|------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------|
| an image decoded a band of rows at a time, reduced and made gray as it decodes; images outside a band not decoded                  | `pdf_image.c`, `draw_device.c`                               |
| a Type3 glyph's procedure loaded when it is run, shared between the codes that name it                                             | `pdf_type3.c`, `res_font.c`                                  |
| a page's parsed objects dropped once it is loaded and once it is drawn (as later MuPDF's `pdf_clear_xref`)                         | `pdf_xref.c`, `view.c`                                       |
| ToUnicode CMaps and link annotations left out; identity CMaps shared                                                               | `pdf_unicode.c`, `pdf_page.c`, `pdf_cmap_load.c`             |
| the draw device's pops no longer free the parent's picture after a push that ran out of memory                                     | `draw_device.c`                                              |
| FreeType on MuPDF's allocator, so its allocations make MuPDF free its cache; FreeType trimmed (`kn7ftoptions.h`, `kn7ftmodules.h`) | `res_font.c`                                                 |
| smaller tables: 8 colour components, the glyph cache 64 KB and 400 glyphs, the store's hash, the lexer's buffer                    | several                                                      |
| the yield hook                                                                                                                     | `pdf_interpret.c`, `dev_list.c`, `stm_read.c`, `pdf_image.c` |
| the glyph FreeType draws last and its rasterizer's state, for the watchdog's report (below)                                        | `res_font.c`, FreeType's `ftgrays.c`                         |

## On the instrument: the log and the watchdog

KN7APP.BIN keeps a log (`src/app/diag.c`): the build, the firmware found, the load, every file and page, and a heartbeat
every 5 s while a page is drawn. It goes onto the card as it grows: each 3 KB as the next part, `PDF\PDFLOG01.TXT`,
`PDFLOG02.TXT` ... (up to 50), saved at the viewer's next yield, and what came after the last part as `PDF\PDFLOG.TXT`,
saved after each page, each file and a pause in the button presses. KN7000 `update_sdcard.sh` brings them all back
and then deletes the parts from the card, so the parts there are always the last run's. When
the program's task has not yielded for 4 s, a watchdog on the firmware's 1 kHz tick (`src/app/crash.c`) samples where
it is, then makes the task leave what it was doing: the log gets the 16 PCs, the registers, the stack's words that may
be return addresses, and the viewer's own account (the page and stage, the glyph FreeType was drawing, the line its
rasterizer was on, the outline's points), and is saved; the screen shows the PCs and the stack. The program stops
there; the instrument goes on. `tests/mame/hang.lua` makes the viewer loop and checks both.

## Memory

PDF.BIN's code and data are 724 KB, more than any one free region holds, so `src/kn7/pdf.ld` spreads them over
ARENA2, the program area and ARENA4 (`ld --enable-non-contiguous-regions`); `tools/build/mkpdf.py` writes the main
image and the segments, and KN7APP.BIN reads each where it goes. The rest of the free regions (kn7.h ARENA1-7) and the
firmware's frame buffers (292 KB, lent while the firmware's drawing is off, kept meanwhile in display memory: `borrow.c`)
are one TLSF heap (`src/kn7/main.c`): **844 KB** on program 893. Current MuPDF built the same way needs 1.36 MB of code
alone (`tools/measure/`).

On the PC (`tests/host/`, the same drawing code, 32-bit), the smallest heap with which every page comes out right: the
LilyPond score 526 KB, the ProTracker manual 862 KB (its pages are many small paths, and 30 Type3 fonts). Before the
`KN7PDF` changes the manual peaked at 17 MB and the score at 2 MB.

## Building

    make              # build/KN7APP.BIN and build/PDF.BIN
    make card-dir     # build/card/PDF/: those, MANIFEST.TXT, the fonts and TEST_PDFS
    make host         # build/host/kn7pdf and build/host/mudraw: the drawing on the PC (tests/host/)
    tests/mame/run_pdf.sh ~/compartilhado/tmp/pdf-mame    # a session in MAME (tests/mame/)

It needs the mn10300-elf toolchain the Pinball port builds (GCC patched to make no `(d32,An)` operand) and the KN7000
loader's tools (technics-roms-disasm `kn7000/tools/cnv_romdump`).

## What was checked

## Licences

MuPDF 1.1 is GPL-3.0 (`src/mupdf/COPYING`) and jbig2dec AGPL-3.0, so the whole is AGPL-3.0; FreeType is used under
its FTL, zlib, libjpeg (IJG) and TLSF (BSD) under theirs (`src/UPSTREAM` lists each library's version and origin).

## Files

| path                      | what                                                                                                                                |
|---------------------------|-------------------------------------------------------------------------------------------------------------------------------------|
| `src/mupdf/`              | MuPDF 1.1 (fitz, draw, pdf), its `KN7PDF` changes                                                                                   |
| `src/third/`              | FreeType, zlib, libjpeg, jbig2dec, TLSF (`src/UPSTREAM`)                                                                            |
| `src/kn7/`                | the viewer: `viewer.c` (the screen, the controls), `view.c` (drawing a page), `main.c` (memory, files, log), `fontfile.c`, `pdf.ld` |
| `src/app/`, `src/common/` | KN7APP.BIN and the interface between the two programs                                                                               |
| `tools/build/`            | `mkpdf.py` (PDF.BIN), `cardname.py`, Doom's `check_d32.py`, `font2c.py`                                                             |
| `tools/measure/`          | why MuPDF 1.1: the code sizes of MuPDF master and 1.1 for the KN7000                                                                |
| `tests/host/`             | the drawing on the PC: memory per page, the smallest heap, against current MuPDF                                                    |
| `tests/mame/`             | the viewer in MAME: `pdf_test.lua`, and probes (profile, stack, panel)                                                              |
