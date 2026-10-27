/* offsets.c -- the module_t and editor_t field offsets pt_test.lua reads, as mn10300-elf-gcc lays them out:
 *   mn10300-elf-gcc -Os -include src/kn7/pt2_kn7.h -Isrc/kn7/shim -Isrc/pt2 -Isrc/kn7 -Isrc/common -c tests/mame/offsets.c
 *   mn10300-elf-objdump -s -j .rodata offsets.o   (patterns, row, header, speed, currPos, currPattern, currRow, currBPM,
 *   editor.songPlaying, editor.currMode, sizeof (module_t), editor.currSample, editor.muted,
 *   little-endian words) */
#include <stddef.h>
#include "pt2_structs.h"
const int offs[] = { offsetof(module_t, patterns), offsetof(module_t, row), offsetof(module_t, header), offsetof(module_t, speed), offsetof(module_t, currPos), offsetof(module_t, currPattern), offsetof(module_t, currRow), offsetof(module_t, currBPM), offsetof(editor_t, songPlaying), offsetof(editor_t, currMode), sizeof(module_t), offsetof(editor_t, currSample), offsetof(editor_t, muted) };
/* and for PT_EDIT=2: editor.editMoveAdd 0x2184, editor.effectMacros 0x218E, editor.multiFlag 0x87, cursor.pos 1,
 * cursor.channel 3, module_t header.patternTable 0x2C, header.songLength 0x12C, modified 1 (2026-10-27) */
const int offs2[] = { offsetof(editor_t, editMoveAdd), offsetof(editor_t, effectMacros), offsetof(editor_t, multiFlag),
                      offsetof(cursor_t, pos), offsetof(cursor_t, channel), offsetof(module_t, header.songLength),
                      offsetof(module_t, header.patternTable), offsetof(module_t, modified) };
