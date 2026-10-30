/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * keyfall.ld -- KEYFALL.BIN: its image linked to run at GAME_BASE (kn7.h), its header first, its bss after it, all
 * below GAME_END. KN7APP.BIN reads the image to the header's base, clears [__bss_lo, __bss_hi) and runs the
 * constructors in [__init_lo, __init_hi) (none) before its entry (src/app/game.c). ARENA2 is the songs' (main.c). (The C
 * names __bss_lo and the like are ___bss_lo here: the MN10300's C symbols take a leading underscore.) */
OUTPUT_FORMAT("elf32-mn10300")
ENTRY(_kf_entry)
SECTIONS {
  . = 0x502E8000;
  .header : { KEEP(*(.header)) }
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .init_array : ALIGN(4) { ___init_lo = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array .ctors)) ___init_hi = .; }
  .data : ALIGN(4) { *(.data .data.*) *(.sdata .sdata.*) }
  . = ALIGN(4);
  ___image_end = .;
  .bss (NOLOAD) : ALIGN(4) { ___bss_lo = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); ___bss_hi = .; }
  ___game_end = .;
  ASSERT(___game_end <= 0x5031B000, "KEYFALL.BIN's image and bss must end below GAME_END (kn7.h)")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.fini_array*) }
}
