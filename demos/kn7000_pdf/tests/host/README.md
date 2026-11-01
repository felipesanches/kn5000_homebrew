# The viewer's drawing on the PC

The same drawing code as on the KN7000 (`src/kn7/view.c`, MuPDF 1.1 with its `KN7PDF` changes), built 32-bit like the
instrument's (`make host`), with MuPDF's memory counted. It answers how much heap a document needs and whether the
pages come out right, before anything runs in MAME.

| file                | answers                                                                                                             |
|---------------------|---------------------------------------------------------------------------------------------------------------------|
| `kn7pdf.c`          | each page drawn as the viewer draws it; per page the heap's peak, the time, the bands, and whether memory ran short |
| `heap_min.sh`       | the smallest heap with which every page still comes out as with no limit                                            |
| `pgmdiff.py`        | whether two pages differ (over 0.1% of pixels by more than 32 of 255): `heap_min.sh`'s test                         |
| `compare_pgm.py`    | each page against current MuPDF's drawing of it (PyMuPDF): the mean difference and the share over 64                |
| `compare_modern.py` | the same for MuPDF 1.1's own `mudraw` PNGs (`make host` builds it)                                                  |
| `packbits_size.py`  | each page as the KN7000's page cache keeps it (4 bits a pixel, PackBits a row at a time): its bytes                 |

    make host
    build/host/kn7pdf FILE.PDF [WIDTH=640] [BAND_ROWS=32] [STORE_KB=64] [OUT_DIR|-] [HEAP_KB]
    tests/host/heap_min.sh build/host/kn7pdf FILE.PDF ~/compartilhado/tmp/pdf-heapmin
    python3 tests/host/compare_pgm.py FILE.PDF OUT_DIR        # needs PyMuPDF

`kn7pdf`'s environment:

| variable             | effect                                                                                                                                                         |
|----------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `KN7PDF_SITES=1`     | the bytes in use at the highest peak (with `HEAP_KB`: at the first allocation refused) by where they were allocated (`addr2line -f -e build/host/kn7pdf ADDR`) |
| `KN7PDF_SITES_END=1` | with `KN7PDF_SITES`: what stays in use after the last page instead                                                                                             |
| `KN7PDF_BIG=1`       | a backtrace of every allocation over 1 MB                                                                                                                      |
| `KN7PDF_SLOW_KB=N`   | the slow memory the Type 3 glyph procedures may be kept in (`fz_kn7_slow_try`), counting what the font files take: default 128, the viewer's `FONT_BUDGET`    |

Under each page's line a second one counts what the KN7000 pays for most: the file's reads (each a request to the
card there, about 50 ms; `read()` wrapped at the link), the glyphs drawn (not found in MuPDF's glyph cache), the times
the glyph cache was emptied, and the Type 3 glyph procedures loaded from the file (`fz_kn7_t3_loads`).

A heap limit (`HEAP_KB`) refuses an allocation that would take the heap in use over it, as the instrument's heap does;
MuPDF then frees what its store holds and tries again, as it does there. The "slow memory" (embedded fonts' files,
`fz_kn7_slow_malloc`, display memory on the KN7000) is counted apart and not limited. FreeType's allocations go through
MuPDF's allocator (`KN7PDF` in `res_font.c`), so they count too.

An AddressSanitizer build of the same: `make host OUT=build-asan HOSTCC="gcc -m32 -fsanitize=address
-fno-omit-frame-pointer"`, then `ASAN_OPTIONS=detect_leaks=0 build-asan/host/kn7pdf FILE.PDF 640 32 1048576 OUT_DIR
HEAP_KB` at falling `HEAP_KB`: what memory running out does to MuPDF's paths that handle it.

`packbits_size.py OUT_DIR/*.pgm` on kn7pdf's pages of the two test PDFs (2026-10-31): 29-69 KB a page, 9-23% of the
290 KB a 640 x 906 page takes at 4 bits a pixel; BACHIANI.PDF's five pages 46-64 KB, PROTRACK.PDF's fifteen 29-69 KB.
