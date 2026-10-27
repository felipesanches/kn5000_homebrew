/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * pt.ld -- PT.BIN, ProTracker: linked to run at GAME_BASE (kn7.h), its header first. KN7APP.BIN reads it
 * there, clears [__bss_lo, __bss_hi) and runs the constructors in [__init_lo, __init_hi) before its entry. All of
 * it, bss included, must end below GAME_END: the rest of the area is the program's own heap (main.c). (The C
 * names __bss_lo and the like are ___bss_lo here: the MN10300's C symbols take a leading underscore.) */
OUTPUT_FORMAT("elf32-mn10300")
ENTRY(_pt_entry)
SECTIONS {
  . = 0x502E8000;
  .header : { KEEP(*(.header)) }
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) *(.gcc_except_table .gcc_except_table.*) }
  .init_array : ALIGN(4) { ___init_lo = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array .ctors)) ___init_hi = .; }
  .data : ALIGN(4) { *(.data .data.*) *(.sdata .sdata.*) }
  __image_end = .;
  .bss (NOLOAD) : ALIGN(4) { ___bss_lo = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); ___bss_hi = .; }
  ___game_end = .;
  ASSERT(___game_end <= 0x5031B000, "PT.BIN must end below GAME_END (kn7.h)")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.fini_array*) }
}
