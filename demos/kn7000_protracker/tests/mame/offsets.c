/* offsets.c -- the module_t and editor_t field offsets pt_test.lua reads, as mn10300-elf-gcc lays them out:
 *   mn10300-elf-gcc -Os -include src/kn7/pt2_kn7.h -Isrc/kn7/shim -Isrc/pt2 -Isrc/kn7 -Isrc/common -c tests/mame/offsets.c
 *   mn10300-elf-objdump -s -j .rodata offsets.o   (patterns, row, header, speed, currPos, currPattern, currRow, currBPM,
 *   editor.songPlaying, editor.currMode, sizeof (module_t), little-endian words) */
#include <stddef.h>
#include "pt2_structs.h"
const int offs[] = { offsetof(module_t, patterns), offsetof(module_t, row), offsetof(module_t, header), offsetof(module_t, speed), offsetof(module_t, currPos), offsetof(module_t, currPattern), offsetof(module_t, currRow), offsetof(module_t, currBPM), offsetof(editor_t, songPlaying), offsetof(editor_t, currMode), sizeof(module_t) };
