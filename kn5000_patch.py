# (c) 2021,2024,2025 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# This program loads a compressed firmware update file and
# outputs both its raw, uncompressed contents as well as a recompressed file.
#
# And it also takes the opportunity to optionally apply experimental patches
# that are useful in research for the implementation of an emulator of
# the Technics KN5000 on MAME.
#
# FIXME: Even though this is a patching script, it ended up working as an
#        extraction script as well. The extraction code should move to a dedicated
#        file and then be imported here. Similar refactoring must be done
#        to the encoding code.

# ALL PATCHES CURRENTLY TARGET VERSION 10 OF THE PROGRAM ROM
# AND VERSION 142 OF THE SUBCPU PAYLOAD
versions = ["10"] # ["5", "6", "7", "8", "9", "10"]

import lzss

from patches.code_snippet_during_boot import code_snippet_during_boot__patch
from patches.dump_rom_via_ROM_orig import dump_rom_via_ROM_orig__patch
from patches.insert_code_snippet_with_preamble import insert_code_snippet_with_preamble__patch
from patches.misc_small_patches import misc_small_patches__patch
from patches.nop_unknown_LDC_instructions import nop_unknown_LDC_instructions__patch
from patches.replace_images import replace_images__patch # Technics logo replaced by "Happy Hacking KN5000"
from patches.run_at_boot import run_at_boot__patch
from patches.run_code_from_MIDI import run_code_from_MIDI__patch
from patches.run_code_from_RCM import run_code_from_RCM__patch
from patches.store_magic_on_flash import store_magic_on_flash__patch
from patches.subcpu_dump_bootrom_via_serial import subcpu_dump_bootrom_via_serial__patch


for version in versions:
    compressed_file = open(f"HKMSPRG.SLD.v{version}", "rb")

    # skip the header:
    expected_header1 = b"SLIDE4K\x00\x20\x00\x00"
    header = compressed_file.read(len(expected_header1))
    assert header == expected_header1

    # FIXME: Instead of hardcoding these "offset_second_part" values here,
    # we should seach for the occurrence of the "SLIDE" marker on the update file.

    #if version == "4":
    #    offset_second_part = 0x
    #    subversion = "139"
    if version == "5":
        offset_second_part = 0xEB33E
        subversion = "140"
    elif version == "6":
        offset_second_part = 0xEB576
        subversion = "140"
    elif version == "7":
        offset_second_part = 0xEB898
        subversion = "141"
    elif version == "8":
        offset_second_part = 0xEBBA0
        subversion = "141"
    elif version == "9":
        offset_second_part = 0xEBBA0
        subversion = "142"
    elif version == "10":
        offset_second_part = 0xEBBA9
        subversion = "142"

    # and read the rest
    compressed_data = compressed_file.read(offset_second_part - len(expected_header1))

    raw_data = lzss.decompress(data=compressed_data, initial_buffer_values=0x00000000)

    expected_header2 = b"SLIDE4K\x00\x03\x00\x00"
    header = compressed_file.read(len(expected_header2))
    assert header == expected_header2

    compressed_data2 = compressed_file.read()
    raw_data2 = lzss.decompress(data=compressed_data2, initial_buffer_values=0x00000000)

# ------------------------------------------------------------------------------------
    if version == "10":
        # ALL PATCHES CURRENTLY TARGET VERSION 10 OF THE PROGRAM ROM
        # AND VERSION 142 OF THE SUBCPU PAYLOAD

        raw_data = list(raw_data)
        raw_data2 = list(raw_data2)
        patches = {}
        subcpu_patches = {}

        ##############################################################################
        # THE VAST MAJORITY OF PATCHES BELOW ARE EXPERIMENTAL AND MOST LIKELY BROKEN #
        ##############################################################################

        # code_snippet_during_boot__patch(maincpu_patches)
        # insert_code_snippet_with_preamble__patch(apply_dangerous_patch=False, maincpu_patches)
        # store_magic_on_flash__patch(maincpu_patches)
        # nop_unknown_LDC_instructions__patch(maincpu_patches)
        # run_at_boot__patch(maincpu_patches)
        # dump_rom_via_ROM_orig__patch(maincpu_patches)
        subcpu_dump_bootrom_via_serial__patch(subcpu_patches)
        # run_code_from_RCM__patch(raw_data)
        # run_code_from_MIDI__patch(raw_data)
        replace_images__patch(raw_data)


        for ptr, injected_code in patches.items():
            for i, value in enumerate(injected_code):
                raw_data[ptr - 0xE00000 + i] = value

        for ptr, injected_code in subcpu_patches.items():
            for i, value in enumerate(injected_code):
                raw_data2[0x100 + ptr - 0x00f000 + i] = value

        raw_data = bytes(raw_data)
        raw_data2 = bytes(raw_data2)
# ------------------------------------------------------------------------------------


    open(f"kn5000_v{version}_program.rom", "wb").write(raw_data)
    open(f"kn5000_subprogram_v{subversion}.rom", "wb").write(raw_data2)

    # write the header and the re-compressed data to a new file:
    data = lzss.compress(data=raw_data, initial_buffer_values=0x00000000)
    data2 = lzss.compress(data=raw_data2, initial_buffer_values=0x00000000)

    new_file = open(f"HKMSPRG.SLD.v{version}.patched", "wb")
    new_file.write(expected_header1)
    new_file.write(data)
    new_file.write(expected_header2)
    new_file.write(data2)
    new_file.close()

