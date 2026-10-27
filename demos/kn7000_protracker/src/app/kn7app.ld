/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * kn7app.ld -- KN7APP.BIN: linked to run at APP_BASE, where the loader (or the program menu) reads it, the
 * header first. Its bss and the game task's stack follow the file; the loader zeroes the whole application
 * area before it loads, so nothing clears them here. Everything must end below APP_END (kn7.h). */
OUTPUT_FORMAT("elf32-mn10300")
ENTRY(_cnv_entry)
SECTIONS {
  . = 0x502BA000;
  .header : { KEEP(*(.header)) }
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  __image_end = .;
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
  __app_end = .;
  ASSERT(__app_end <= 0x502C4000, "KN7APP.BIN and its bss must end below APP_END (kn7.h)")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
