/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * doom.ld -- DOOM.BIN, Chocolate Doom: its image linked to run at ARENA2's start (kn7.h), its header first; its bss at
 * GAME_BASE, where ProTracker's image went, as the image is larger than that area, but for r_plane.c's visplanes and
 * openings (123 KB), which are at ARENA1's start. KN7APP.BIN reads the image to the header's base, clears
 * [__bss_lo, __bss_hi) and runs the constructors in [__init_lo, __init_hi) before its entry (src/app/game.c); the entry
 * clears [__bss2_lo, __bss2_hi) (main.c). The rest of these areas, and the other regions KN7APP hands over, are the
 * zone's and the heap's (main.c). (The C names __bss_lo and the like are ___bss_lo here: the MN10300's C symbols take a leading underscore.) */
OUTPUT_FORMAT("elf32-mn10300")
ENTRY(_doom_entry)
SECTIONS {
  . = 0x5031C000;
  .header : { KEEP(*(.header)) }
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) *(.gcc_except_table .gcc_except_table.*) }
  .init_array : ALIGN(4) { ___init_lo = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array .ctors)) ___init_hi = .; }
  .data : ALIGN(4) { *(.data .data.*) *(.sdata .sdata.*) }
  . = ALIGN(4);
  ___image_end = .;
  ASSERT(___image_end <= 0x5037A000, "DOOM.BIN's image must end in ARENA2 (kn7.h)")
  .bss2 0x502C8000 (NOLOAD) : ALIGN(4) { ___bss2_lo = .; *(.bss.visplanes .bss.openings) . = ALIGN(4); ___bss2_hi = .; }
  ASSERT(___bss2_hi <= 0x502E7F00, "DOOM.BIN's second bss must end in ARENA1 (kn7.h)")
  .bss 0x502E8000 (NOLOAD) : ALIGN(4) { ___bss_lo = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); ___bss_hi = .; }
  ___game_end = .;
  ASSERT(___game_end <= 0x5031B000, "DOOM.BIN's bss must end below GAME_END (kn7.h)")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.fini_array*) }
}
