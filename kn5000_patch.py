# (c) 2021,2024 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# WARNING! Below is the original, slightly outdated description of this file:
# ------------------------------------------------------------------------------------
# This program loads a compressed firmware update file and
# outputs both its raw, uncompressed contents as well as a recompressed file.
# ------------------------------------------------------------------------------------
#
# FIXME: Even though this is a patching script, it ended up working as an
#        extraction script as well. The extraction code should move to a dedicated
#        file and then be imported here. Similar refactoring must be done
#        to the encoding code.

patch_images = True  # Technics logo replaced by "Happy Hacking KN5000"
patch_memory_dump_screen = False
patch_code_snippet_during_boot = False

patch_insert_code_snippet_with_preamble = True

DANGEROUS_DO_NOT_RUN_THIS_ON_REAL_KN5000 = False

patch_insert_code_snippet = False
patch_store_magic_on_flash = False
patch_nop_unknown_LDC_instructions = False
patch_run_at_boot = False
patch_run_code_from_RCM = False
patch_run_code_from_MIDI = False
patch_dump_rom_via_ROM_orig = False
dump_address = 0x800000 # to try dumping the first 0x1100 bytes
versions = ["10"] # ["5", "6", "7", "8", "9", "10"]


import lzss
from binxeledit import (BinxelEdit,
                        reuse_palette)
                       
def kn5000_golden_palette(r, g, b, a, colors, transparent=None):
    if transparent and r==255 and g==255 and b==255:
        return transparent
    else:
        return 0x21 + int(0xE * (r + g + b) / (255*3.0))


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
    # This is still experimental:
    if version == "10":
        raw_data = list(raw_data)
        patches = {}

        # 0xFB729E # "MainCPU_self_test_routines"
        # 0xFB7328 # "A_Short_Pause"

        if patch_code_snippet_during_boot:
            code_to_insert = list(open("demos/monitor/monitor.rom", "rb").read())
            codeinsert_address = 0xEF05E8  # "User_didnt_request_flash_mem_update"
            upper_limit = 0xEF061E
            patches[codeinsert_address] = code_to_insert
            assert codeinsert_address + len(code_to_insert) <= upper_limit


           
        if patch_insert_code_snippet_with_preamble:
            if DANGEROUS_DO_NOT_RUN_THIS_ON_REAL_KN5000:
                patches[0xEF0526] = [
                    0x1B, 0x00, 0x20, 0xEE,   # JP PREAMBLE
                    0x1B, 0x2a, 0x05, 0xEF,   # inf. loop
                ]
            
            code_to_insert = list(open("demos/monitor/monitor.rom", "rb").read())
            PREAMBLE_address = 0xE0018E # one of the fw-update bitmaps
            upper_limit = 0xE008C6 # another fw-update bitmap
            codeinsert_address = PREAMBLE_address + 0x20

            PREAMBLE_address_bytes = [
                (PREAMBLE_address >> (i*8)) & 0xFF for i in range(3)]
            codeinsert_address_bytes = [
                (codeinsert_address >> (i*8)) & 0xFF for i in range(3)]

            patches[0xEF05E8] = [   # Patched at "User_didnt_request_flash_mem_update"
                                    # overwriting C1 02 04 21 D8 12 
                0x1D] + PREAMBLE_address_bytes + [0x00, 0x00]  # CALL PREAMBLE; NOP
            CALL = 0x1d

            PREAMBLE = [
                CALL] + codeinsert_address_bytes + \
            [  # call code_to_insert
                0xC1, 0x02, 0x04, 0x21, # LD A (0x0402)
                0xD8, 0x12,             # EXTZ WA
                0x0E,   			    # RET
            ]
            
            def hex_print(v):
                print(list(map(hex, v)))
            
            hex_print(patches[0xEF05E8])
            hex_print(PREAMBLE)

            patches[PREAMBLE_address] = PREAMBLE # <-- faz patch do preambulo
            assert len(PREAMBLE) < (codeinsert_address - PREAMBLE_address)
            patches[codeinsert_address] = code_to_insert # <-- faz patch da ROM (monitor.rom)

            assert codeinsert_address + len(code_to_insert) <= upper_limit
        

        if patch_store_magic_on_flash:
            patches[0xEF197C] = [
                0x42, 0x00, 0x00, 0x3B, 0x00,   # ld XDE, 0x003B0000
                0xF5, 0xE9, 0x02, 0x34, 0x12,   # ld (XDE+), 0x1234
                0x00,  # nop
            ]


        if patch_nop_unknown_LDC_instructions:
            patches[0xEF1984] = [  # Replace ldc unknown (encoding is 0x7C), WA   | D8 2E 7C
                0x00, 0x00, 0x00   # NOP NOP NOP
            ]
            patches[0xEF1AAD] = [  # Replace ldc unknown (encoding is 0x7C), WA   | D8 2E 7C
                0x00, 0x00, 0x00   # NOP NOP NOP
            ]
            patches[0xEF1B89] = [  # Replace ldc unknown (encoding is 0x7C), WA   | D8 2E 7C
                0x00, 0x00, 0x00   # NOP NOP NOP
            ]

# ldc DMAS0, WA     | E8 2E 00
# ldc DMAD2, WA     | E8 2E 28
# ldc DMAM0, A      | C9 2E 42
# ldc DMAM2, A      | C9 2E 4A
# ldcf 7, (0x0406)  | F1 06 04 9F (at EF0797)



        if patch_run_at_boot:
            patches[0xEF0541] = [  # Replace initial "Please Wait !!" message:
                0xFE, 0x0F   # 0x00E00FFE = "Illegal Disk !!"
            ]

            patches[0xEF05E8] = [   # Patched at "User_didnt_request_flash_mem_update"
                # 0x1D, 0xA7, 0x55, 0xEF,        #	CALL Some_VGA_setup
                0x08, 0x08, 0x00,              #	PUSH, 0x0008
                0x08, 0x03, 0x00,              #	PUSH, 0x0003
                0x40, 0x66, 0x12, 0xE0, 0x00,  #	LD XWA, 0x00e01266  # "Turn On AGAIN !!"
                0x31, 0x30, 0x00,              #	LD BC, 0x0030
                0x32, 0x50, 0x00,              #	LD DE, 0x0050
                0x1D, 0x40, 0x50, 0xEF,        #	CALL Draw_FlashMemUpdate_message_bitmap
                0x68, 0xFE  #	JP infinite_loop
            ]

        if patch_dump_rom_via_ROM_orig:
            a0 = (dump_address >> 0) & 0xff
            a1 = (dump_address >> 8) & 0xff
            a2 = (dump_address >> 16) & 0xff
            a3 = (dump_address >> 24) & 0xff

            dump_ptr = 0x00060000
            p0 = (dump_ptr >> 0) & 0xff
            p1 = (dump_ptr >> 8) & 0xff
            p2 = (dump_ptr >> 16) & 0xff
            p3 = (dump_ptr >> 24) & 0xff

#            patches[0xF88005] = [   # Load a MIDI file (LoadFileSMF)
#            patches[0xF18A74] = [   # Load a RCM file
#                0x45, a0, a1, a2, a3,                # LD XIY, dump_address
#                0xF2, p0, p1, p2, p3,                # LD (dump_ptr), XIY
#                0x0E,                                # RET
#            ]

            patches[0xF6F30C] = [   # Restore ROM original settings
                0x1D, 0x00, 0x00, 0x3B,              # CALL 0x3B0000
                0x0E,                                # RET
            ]

#                0xE2, p0, p1, p2, 0x25,              # LD XIY, (dump_ptr)
#                0xF2, 0x20, 0x88, 0x1E, 0x65,        # LD (0x1E8820), XIY
#                0x44, 0x00, 0x8B, 0x1E, 0x00,        # LD XIX, 0x001e8b00
#                0x31, 0x80, 0x0a,                    # LD BC, 0x0a80
#                0x3D,                                # PUSH XIY
#                0x95, 0x11,                          # LDIRW
#                0x5D,                                # POP XIY
#                0xED, 0xC8, 0x00, 0x10, 0x00, 0x00,  # ADD XIY, 0x1000
#                0xF2, p0, p1, p2, 0x65,              # LD (dump_ptr), XIY
#                0x1D, 0xB0, 0xF3, 0xF6,              # CALL LABEL_F6F3B0
#                0x0E,                                # RET
#            ]
            
#
#LABEL_F6F309:
#	CALR LABEL_F6F0B1    #  xx B1 F0 F6  
#
#LABEL_F6F30d:
#	LD XIY, 0x00f6f42f
#	LD XIX, 0x001e8820
#	LD BC, 0x00f0
#	LDIRW
#	LD XIX, 0x001e8a00
#	LD XIY, 0x00f6f249
#	LD BC, 0x0060
#	LDIRW
#	LD XIX, 0x001e8b00
#	LD XIY, 0x00f6f62f
#	LD BC, 0x0a80
#	LDIRW
#	CALR LABEL_F6F3B0
#	RET
#


# Em algum lugar a partir do endereço 0xEB2AC1 fica
# o inicio da estrutura de dados da tela "Panel Simulator for HK"
#
# candidatos:
# 0xEB2AC2: 33 00 60 01 FF FF 01 00 FF FF ...
# 0xEB2AE4: 2C 00 60 01 00 00 FF ...
# 0xEB2AFE: 2B 00 60 01 00 00 FF FF 03 00 01 00 ...



        # FA2D44 - DbMemoProc
#        patches[0xE80FF6] = [0x44, 0x2D, 0xFA]


# ED28A4: Func table[0018]: FC1A22 / ED3218 - PmBankScreenProc
# Func 0xE0A[049C]: F9AC62 / EB08DC - IvDirmdScreenProc
# Func 0xE0A[04B0]: FA2EE6 / EB088A - DbMemoryDumpProc
#        patches[0xED28A4] = [0xE6, 0x2E, 0xFA]

 	       # DbDebugMenuProc <==> AcSndEMenuProc
        #          FA33EE <==> F7B665
        if patch_memory_dump_screen:
            patches[0xE80FF6] = [0xEE, 0x33, 0xFA]
#        patches[0xEAD216] = [0x65, 0xB6, 0xF7]

        # DbMemoryDumpProc <==> AcSndEMenuProc
        #          FA2EE6 <==> F7B665
#        patches[0xE80FF6] = [0xE6, 0x2E, 0xFA]
#        patches[0xEAD216] = [0x65, 0xB6, 0xF7]

        for ptr, injected_code in patches.items():
            for i, value in enumerate(injected_code):
                raw_data[ptr - 0xE00000 + i] = value

        if patch_run_code_from_RCM:
            # At address F18AA7 we insert some code in the routine that loads
            # an RCM file. Then we call the very first address in the file
            # where we're supposed to place executable TLCS-900 code.
            # Finally after returning, we jump to the place where the kn5000
            # things the RCM file was malformed.
            # ptr = 0x00118aa7
            ptr = 0x00118a94
            injected_code = [
                0xE8, 0x64,             # INC 4, XWA   # To skip the header.
                0xE8, 0x8B,             # LD XHL, XWA
                0xB3, 0xE8,             # CALL T XHL
                0x1B, 0xF5, 0x8B, 0xF1, # JP LABEL_F18BF5
            ]
            for i, value in enumerate(injected_code):
                raw_data[ptr + i] = value

        if patch_run_code_from_MIDI:
            #ptr = 0x187600
#            ptr = 0x1DB4A1
            ptr = 0x00118a74
            injected_code = [
                0x06, 0x06,             # EI 6 ; disable interrupts
                0x1D, 0x04, 0x00, 0x06, # CALL 0x060004
            ]
            # ptr = 0x188051
            #injected_code = [
            #    0xE8, 0x64,             # INC 4, XWA   # To skip the header.
            #    0xE8, 0x8B,             # LD XHL, XWA
            #    0xB3, 0xE8,             # CALL T XHL
            #    0x1B, 0xF5, 0x8B, 0xF1, # JP LABEL_F18BF5
            #]
            for i, value in enumerate(injected_code):
                raw_data[ptr + i] = value


        if False: #patch_run_code_from_MIDI:
            # Infinite loop at LoadFileSMF (0xF88005)
            ptr = 0x188005
            injected_code = [
                0x1B, 0x05, 0x80, 0xF8, # JP LABEL_F88005
            ]
            for i, value in enumerate(injected_code):
                raw_data[ptr + i] = value

        raw_data = bytes(raw_data)

# ------------------------------------------------------------------------------------

    if patch_images and version == "10":
        # perform patching on decompressed data:
        raw_data = list(raw_data)
        edit = BinxelEdit(raw_data=raw_data)
        edit.write_image(filename="HappyHacking.png",
                         transparent=0xF7, pixel_value=kn5000_golden_palette,
                         byte=0x8FFA6, width=312, height=45, bpp=8)

        #edit.write_image(filename="Sanfona-1.png",
        #                 transparent=0xF7, pixel_value=reuse_palette,
        #                 byte=0x86676, width=120, height=95, bpp=8)

        #edit.write_image(filename="Sanfona-2.png",
        #                 transparent=0xF7, pixel_value=reuse_palette,
        #                 byte=0x892FE, width=120, height=95, bpp=8)
        raw_data = bytes(edit.raw_data)

    if patch_dump_rom_via_ROM_orig and version == "10":
        ptr = 0x118A74 + 1
        #assert list(raw_data)[ptr + 0] == ((dump_address >> 0) & 0xff)
        #assert list(raw_data)[ptr + 1] == ((dump_address >> 8) & 0xff)
        #assert list(raw_data)[ptr + 2] == ((dump_address >> 16) & 0xff)
        #assert list(raw_data)[ptr + 3] == ((dump_address >> 24) & 0xff)

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

