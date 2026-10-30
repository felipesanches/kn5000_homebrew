# Which MuPDF fits

The question: how much code does a viewer need that opens a PDF and draws a page, built for the KN7000
(mn10300-elf-gcc -Os, -ffunction-sections, ld --gc-sections, newlib)? Inputs are clones in a scratch directory
(`git clone --depth 1 --branch <tag> https://github.com/ArtifexSoftware/mupdf.git`, master with its submodules
freetype zlib libjpeg jbig2dec).

| script              | answers                                                                                        |
|---------------------|------------------------------------------------------------------------------------------------|
| `measure_mupdf.sh`  | MuPDF master's fitz, pdf and libraries, each compiled with every format but PDF off: sizes     |
| `measure_link.sh`   | master linked to `link_main.c` (open, load page 0, draw it gray) with `link_stubs.c`           |
| `measure_old.sh`    | MuPDF 1.1's fitz/draw/pdf compiled: sizes                                                      |
| `measure_link11.sh` | 1.1 linked to `link_main11.c` with `link_stubs11.c` and master's libraries (from measure_link) |
| `measure_src.sh`    | the source/ layout of 1.2 on (1.4, 1.7, 1.9a) compiled: sizes                                  |

    S=~/compartilhado/tmp/pdf-kn7000
    ./measure_link.sh $S/mupdf $S/link1                              # master: text 1361633
    ./measure_old.sh $S/mupdf-1.1 $S/mupdf $S/old11
    ./measure_link11.sh $S/old11 $S/link1 $S/mupdf-1.1 $S/link11     # 1.1: text 853101

Results 2026-10-30 (text, which holds .rodata too): master linked 1,361,633 bytes; 1.1 linked 853,101 bytes, of
which FreeType 243,634, pdf 142,970, merged string constants 97,911, fitz 77,512, newlib 75,850, libjpeg 74,603,
draw 53,751, jbig2dec 53,055, zlib 15,880. Compiled, not linked: 1.1 fitz+draw+pdf 413,790; 1.4 634,814;
1.7 fitz alone 371,680.
