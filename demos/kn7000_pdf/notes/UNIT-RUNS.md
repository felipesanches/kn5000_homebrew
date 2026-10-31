# The viewer on Felipe's KN7000 (program 893)

Each run: the build on the card (KN7000 `tools/sdcard/stage.sh`), what Felipe saw, and what `PDF\PDFLOG.TXT` said
(`~/compartilhado/sdcard-stage/from-card/<date>/PDF/PDFLOG.TXT`).

| date       | build (PDF.BIN SHA-1) | file         | result                                                                                       |
|------------|-----------------------|--------------|----------------------------------------------------------------------------------------------|
| 2026-10-30 | first staged          | -            | an error after "Loading..."                                                                  |
| 2026-10-30 | 0a1b033               | BACHIANI.PDF | PDF.BIN loaded by raw reads in 6 s, CRCs right, list shown; the log ends at "drawing page 1" |
| 2026-10-30 | 57292fe (69e92e7a)    | BACHIANI.PDF | freezes; one heartbeat at 5 s ("list at line 0", heap 547 KB), then nothing: not yielding    |
| 2026-10-31 | 3d0fe47 (b9dd8506)    | BACHIANI.PDF | the watchdog's screen (photo below); the log: watchdog armed, one heartbeat, then nothing    |

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
