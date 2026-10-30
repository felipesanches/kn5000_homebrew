/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * pdf.ld -- PDF.BIN, the viewer: more code than any one free region of the KN7000 holds (kn7.h), so its sections fill
 * ARENA2 first (the main image: the header, the segment table, then code), then the program area (GAME_BASE), ARENA4 and
 * ARENA1, each such region a segment of the file (ld --enable-non-contiguous-regions puts each input section, a
 * function or an object with -ffunction-sections -fdata-sections, in the first region it fits): the code first,
 * then the constants and data in what is left, in output sections of their own (tools/build/check_d32.py reads the
 * code's alone). The bss is at
 * ARENA1's start. KN7APP.BIN reads the main image to the header's base and each segment to its address (pfhost.h
 * PfSegment, tools/build/mkpdf.py), clears [__bss_lo, __bss_hi) and runs the constructors in [__init_lo, __init_hi)
 * before the entry. The rest of these regions, and the other regions KN7APP hands over, are the heap (main.c). (The C
 * names __bss_lo and the like are ___bss_lo here: the MN10300's C symbols take a leading underscore.) */
OUTPUT_FORMAT("elf32-mn10300")
ENTRY(_pdf_entry)
MEMORY {
  MAIN (rwx) : ORIGIN = 0x5031C000, LENGTH = 0x5037A000 - 0x5031C000  /* ARENA2 */
  GAME (rwx) : ORIGIN = 0x502E8000, LENGTH = 0x5031B000 - 0x502E8000  /* GAME_BASE..GAME_END */
  A4 (rwx)   : ORIGIN = 0x503BC000, LENGTH = 0x503FF000 - 0x503BC000  /* ARENA4 */
  A1 (rwx)   : ORIGIN = 0x502C8000, LENGTH = 0x502E7F00 - 0x502C8000  /* ARENA1 */
}
SECTIONS {
  .header : { KEEP(*(.header)) KEEP(*(.segtab)) } > MAIN
  .init_array : ALIGN(4) { ___init_lo = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array .ctors)) ___init_hi = .; } > MAIN
  .bss (NOLOAD) : ALIGN(16) { ___bss_lo = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(16); ___bss_hi = .; } > A1
  ___game_end = ___bss_hi;
  .strings : { *(.rodata.str* .rodata.*.str*) } > MAIN  /* (merged string constants change size: never spread) */
  .main_text : ALIGN(4) { *(.text .text.*) } > MAIN
  .game_text : ALIGN(4) { *(.text .text.*) } > GAME
  .a4_text : ALIGN(4) { *(.text .text.*) } > A4
  .a1_text : ALIGN(4) { *(.text .text.*) } > A1
  .main_data : ALIGN(4) { *(.rodata .rodata.* .data .data.* .sdata .sdata.*) } > MAIN
  .main_end (NOLOAD) : { . = ALIGN(16); ___image_end = .; } > MAIN
  .game_data : ALIGN(4) { *(.rodata .rodata.* .data .data.* .sdata .sdata.*) . = ALIGN(16); } > GAME
  .a4_data : ALIGN(4) { *(.rodata .rodata.* .data .data.* .sdata .sdata.*) . = ALIGN(16); } > A4
  .a1_data : ALIGN(4) { *(.rodata .rodata.* .data .data.* .sdata .sdata.*) . = ALIGN(16); } > A1
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.fini_array*) *(.gcc_except_table*) }
}
