# The viewer on Felipe's KN7000 (program 893)

Each run: the build on the card (KN7000 `tools/sdcard/stage.sh`), what Felipe saw, and what `PDF\PDFLOG.TXT` said
(`~/compartilhado/sdcard-stage/from-card/<date>/PDF/PDFLOG.TXT`).

| date       | build (PDF.BIN SHA-1)  | file               | result                                                                                              |
|------------|------------------------|--------------------|-----------------------------------------------------------------------------------------------------|
| 2026-10-30 | first staged           | -                  | an error after "Loading..."                                                                         |
| 2026-10-30 | 0a1b033                | BACHIANI.PDF       | PDF.BIN loaded by raw reads in 6 s, CRCs right, list shown; the log ends at "drawing page 1"        |
| 2026-10-30 | 57292fe (69e92e7a)     | BACHIANI.PDF       | freezes; one heartbeat at 5 s ("list at line 0", heap 547 KB), then nothing: not yielding           |
| 2026-10-31 | 3d0fe47 (b9dd8506)     | BACHIANI.PDF       | the watchdog's screen (photo below); the log: watchdog armed, one heartbeat, then nothing           |
| 2026-10-31 | 64d125c (777aaab2)     | BACHIANI.PDF       | the watchdog's report (below): FreeType's negative coordinates 65536 too high                       |
| 2026-10-31 | 33b277a (c6394908 CRC) | BACHIANI.PDF       | past the old hang; then a system-error NMI in the heap walk (below)                                 |
| 2026-10-31 | d2f9a89 (fb9d856b)     | BACHIANI, PROTRACK | pages drawn; then pool 6 found overwritten with E0 bytes, and an NMI after CONTRAST DOWN (below)    |
| 2026-10-31 | 8468662                | BACHIANI, PROTRACK | no blue screen; PROTRACK's page 2 took 124 s (memory short, "no list"); seemed frozen later (below) |

## 2026-10-31: the watchdog's screen

Photo: `~/compartilhado/screenshots/kn7000/pdf/photo_5129715459115650146_y.jpg`. The 16 PCs, mapped with
`tools/debug/addr2fn.py build/pdf/pdf.elf ...` on a build of 3d0fe47 (PDF.BIN SHA-1 b9dd8506, the card's):

| PC       | function                 |
|----------|--------------------------|
| 502F421D | `gray_set_cell`+0x0      |
| 502F4221 | `gray_set_cell`+0x4      |
| 502F4224 | `gray_set_cell`+0x7      |
| 502F4281 | `gray_set_cell`+0x64     |
| 502F442A | `gray_render_line`+0x1A3 |
| 502F44CB | `gray_render_line`+0x244 |
| 502F4538 | `gray_render_line`+0x2B1 |
| 502F453C | `gray_render_line`+0x2B5 |
| 502F45C5 | `gray_render_line`+0x33E |
| 502F45EB | `gray_render_line`+0x364 |
| 502F45F5 | `gray_render_line`+0x36E |
| 502F45FD | `gray_render_line`+0x376 |
| 4C02BB2F | the firmware's library   |

15 of 16 in FreeType's smooth rasterizer (`src/third/freetype/src/smooth/ftgrays.c`): `gray_render_line`'s cell walk
(`do ... while ( ex1 != ex2 || ey1 != ey2 )`), which calls `gray_set_cell` each step, for over 5.6 s with no yield.
The same page draws in MAME in 15 s, yielding every 20 ms. On the PC (`tests/host`, 32-bit) it draws with glibc's
`MALLOC_PERTURB_=165` and `=90` byte-identical to a clean run: heap contents left over are not the difference there
(in `build/card/PDF`: `MALLOC_PERTURB_=165 ../../host/kn7pdf BACHIANI.PDF 640 32 64 OUT_DIR 831`, then `cmp` the PGMs).
The next build's watchdog report gives the line, the outline and the glyph (README, "On the instrument").

## 2026-10-31: the watchdog's report (64d125c)

`from-card/20261031-085634/PDF/PDFLOG.TXT`, photo `photo_5127479391767170558_y.jpg`. Stuck in "band" at line 0 of page
1, in `gray_render_line` again, drawing glyph 18 of "LJYFAB+C059-Bold" (matrix 13.226071 0 0 -13.226071 0.5 0):

    freetype: as FreeType's: 846 0 0 -4294968142, 32 0 (1/64)
    gray: the last line 1548,0 -> 2340,262108 (1/256 pixel)
    gray: outline: 39 points, 2 contours, flags 4; points within 63,0 617,65527 (1/64 pixel)
    gray:   513,64858 298,64871 298,64968 380,64968

FreeType's matrix entry yy is -846, printed by fmt.c as -4294968142, -(2^32 + 846); and every negative y of the
outline is 65536 too high (64858 for -678): the high word of a 64-bit result one too high whenever it should be
all ones. The cause is in the CPU manual (MN1030/MN103S Series Instruction Manual, Chapter 3, "Dangerous Code
Sequences", 2-3, cores AM30-AM32): a flag update reaches the PSW one cycle late, so an ADDC right after the ADD that
sets its carry adds the carry from before. GCC makes exactly that pair; FreeType's `FT_MulFix_64`:

    mul   d1,d0          ; product in MDR:d0
    asr   31,d0          ; the sign: C = bit 30 of the high word, 1 for a negative product
    add   32768,a0       ; low word + 0x8000
    addc  d2,d3          ; high word + carry: the asr's carry, not the add's

For a negative product the high word gets 1 + 1 too many and wraps to 0, so (hi << 16) | (lo >> 16) loses the sign.
The firmware (Panasonic's compiler) has no ADDC, SUBC, ROR or ROL at all (technics-roms-disasm kn7000
`disasm/program.asm`, 0 of 589,114 lines). `tools/build/check_flags.py` finds 520 such reads in this PDF.BIN
(FreeType's rasterizer and glyph loaders, soft-fp's double add, subtract and divide, jbig2dec), 0 in KN7APP. Fixed in
the toolchain (Pinball kn7000/toolchain/gcc-kn7000-flags.patch: a nop before every addc and subc), checked at every
link by check_flags.py, and modelled in MAME's MN10300 core (kn7000_mame).

With the delay modelled (kn7000_mame 0a37d84), MAME runs this same PDF.BIN (777aaab2) into the same hang:
`tests/mame/run_pdf.sh OUT tests/mame/quick.lua` with `EMU` a folder holding that build's `kn7000` (beside the
published emulator's `roms`, `plugins` and card image) and `KEEP_SD=1`; the log
(`tools/card/fatget.py OUT/sd_after.img 'PDF\PDFLOG.TXT'`) shows glyph 18 of C059-Bold, the matrix's yy as
-4294968142, the line 1548,0 -> 2340,262108 and the clip box as on the unit. Some outline points differ (64912 for
the unit's 64858) and the matrix's sixth decimal: on the chip an interrupt between an ADD and its ADDC empties the
pipeline, so which pairs go wrong there depends on when the interrupts come.

## 2026-10-31: a system-error NMI in the heap walk (33b277a, the toolchain fixed)

`from-card/20261031-104104/PDF/PDFLOG.TXT` ends at "drawing page 1" (56475 ms); photo
`photo_5129731191580855828_y.jpg`: the crash screen, PC 503C0F7C in PDF.BIN, NMICR 0004. `tools/debug/addr2fn.py`:
`tlsf_walk_pool`+0x1C, the instruction after `mov (4,a2),d1` (a block's size word), called from `kn7HeapStats`
(stack: 503BDC8C) from the heartbeat in `yieldNow` (503BE4D1). A block chain led out of its pool, so a block
header in the heap was overwritten; in MAME (with the flag delay modelled) every heartbeat's walk is clean. Build
d2f9a89 walks each pool itself, checking every block against the pool's bounds, and logs the first damaged one
(the pool, the block, its words) instead of following it.

## 2026-10-31: the pages draw; the borrowed frame buffers overwritten (d2f9a89)

`from-card/20261031-122545/PDF/PDFLOG.TXT`: BACHIANI.PDF's pages 1-3 in 16.1, 9.9 and 14.0 s, then PROTRACK.PDF;
Felipe pressed buttons looking for a way to scroll. At 118328 ms, while PROTRACK's page 1 was drawn:

    heap: pool 6 (500D5000-5011E000) damaged at block 181, 500FD924: size word E0E0E0E0
    heap:   500FD924: E0E0E0E0 E0E0E0E0 E0E0E0E0 E0E0E0E0

Pool 6 is the firmware's frame buffers, lent to the heap while its drawing is off (`src/app/borrow.c`). After CONTRAST
DOWN the crash screen (`photo_5129731191580855835_y.jpg`): PC 5033E870, `fz_run_display_list`+0x130, a display list
node read through the damaged memory (E0E0E0E0 on the stack). In MAME (`tests/mame/probes/contrast.lua`: CONTRAST
DOWN and UP three times each, a write tap on the borrowed range) the firmware writes nothing there and the heap stays
whole. The next build (staged with this note) logs every switch frame KN7APP sees ("switch frame ADDR DATA"),
checks every pool every 200 ms, logs the first damage with the extent of its fill value, and saves the log on damage
and 1 s after the last switch change.

## 2026-10-31: no blue screen; slow pages (8468662)

`from-card/20261031-132955/PDF/PDFLOG.TXT`: the log's middle was pushed out (the switch frames filled it). PROTRACK.PDF's
page 2 took 124522 ms: 9116 allocations refused, the page drawn a region at a time without its whole list ("no list").
Then PAGE UP/DOWN pressed while page 1 was drawn; the last heartbeat 528314 ms ("band at line 416"), no watchdog report,
no heap damage. Felipe: it seemed to freeze at some point. Asked for: the page shown readable and scrollable while
another is drawn, pages drawn ahead, and the pages drawn kept, also on the card. Done in d4329f4 (README, "The page
cache", "Drawing ahead"): the font files left the main heap for the display heap, which the page's 4 bits a pixel made.

## 2026-11-01: the page cache works on the unit (87f01d5)

`from-card/20261101-101936/PDF/PDFLOG.TXT` (its middle pushed out again: 62107 ms to 1670950 ms lost) and
`listing.txt`: the card has all 5 of BACHIANI.PDF's pages and all 15 of PROTRACK.PDF's as `.PGC` files, 29-70 KB each.
PROTRACK's pages 14 and 15 drew in 49.6 and 35.0 s and were saved in 1027 and 895 ms; pages still in the display heap
came back in 192-349 ms, pages read back from the card in 586-985 ms. Reopened, BACHIANI.PDF found "5 of 5 pages on
the card" and showed page 1 in 818 ms; PROTRACK.PDF, 15 of 15, page 1 in 621 ms. No watchdog report, no heap damage
in what the log kept. "fonts 0 KB" is right for PROTRACK.PDF: its fonts are Type 3 (`pdffonts`), glyphs drawn from
content streams, with no font file to move. BACHIANI's pages 1, 2, 3 and 5 on the unit are 56261, 46047, 56372 and
64007 bytes, the sizes MAME's run (`tests/mame/pdf_test.lua`, KEEP_SD=1) wrote. `update_sdcard.sh` now brings the
`.PGC` files back and `tools/card/pgc2png.py` makes them PNGs, to compare the two byte for byte.
