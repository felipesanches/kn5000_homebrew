dump_address = 0x800000 # to try dumping the first 0x1100 bytes

def dump_rom_via_ROM_orig__patch(maincpu_patches):
    a0 = (dump_address >> 0) & 0xff
    a1 = (dump_address >> 8) & 0xff
    a2 = (dump_address >> 16) & 0xff
    a3 = (dump_address >> 24) & 0xff

    dump_ptr = 0x00060000
    p0 = (dump_ptr >> 0) & 0xff
    p1 = (dump_ptr >> 8) & 0xff
    p2 = (dump_ptr >> 16) & 0xff
    p3 = (dump_ptr >> 24) & 0xff

#    maincpu_patches[0xF88005] = [   # Load a MIDI file (LoadFileSMF)
#    maincpu_patches[0xF18A74] = [   # Load a RCM file
#        0x45, a0, a1, a2, a3,                # LD XIY, dump_address
#        0xF2, p0, p1, p2, p3,                # LD (dump_ptr), XIY
#        0x0E,                                # RET
#    ]

    maincpu_patches[0xF6F30C] = [   # Restore ROM original settings
        0x1D, 0x00, 0x00, 0x3B,              # CALL 0x3B0000
        0x0E,                                # RET
    ]

#        0xE2, p0, p1, p2, 0x25,              # LD XIY, (dump_ptr)
#        0xF2, 0x20, 0x88, 0x1E, 0x65,        # LD (0x1E8820), XIY
#        0x44, 0x00, 0x8B, 0x1E, 0x00,        # LD XIX, 0x001e8b00
#        0x31, 0x80, 0x0a,                    # LD BC, 0x0a80
#        0x3D,                                # PUSH XIY
#        0x95, 0x11,                          # LDIRW
#        0x5D,                                # POP XIY
#        0xED, 0xC8, 0x00, 0x10, 0x00, 0x00,  # ADD XIY, 0x1000
#        0xF2, p0, p1, p2, 0x65,              # LD (dump_ptr), XIY
#        0x1D, 0xB0, 0xF3, 0xF6,              # CALL LABEL_F6F3B0
#        0x0E,                                # RET
#    ]
            
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
#        maincpu_patches[0xE80FF6] = [0x44, 0x2D, 0xFA]


# ED28A4: Func table[0018]: FC1A22 / ED3218 - PmBankScreenProc
# Func 0xE0A[049C]: F9AC62 / EB08DC - IvDirmdScreenProc
# Func 0xE0A[04B0]: FA2EE6 / EB088A - DbMemoryDumpProc
#        maincpu_patches[0xED28A4] = [0xE6, 0x2E, 0xFA]

 	       # DbDebugMenuProc <==> AcSndEMenuProc
        #          FA33EE <==> F7B665
        if patch_memory_dump_screen:
            maincpu_patches[0xE80FF6] = [0xEE, 0x33, 0xFA]
#        maincpu_patches[0xEAD216] = [0x65, 0xB6, 0xF7]

        # DbMemoryDumpProc <==> AcSndEMenuProc
        #          FA2EE6 <==> F7B665
#        maincpu_patches[0xE80FF6] = [0xE6, 0x2E, 0xFA]
#        maincpu_patches[0xEAD216] = [0x65, 0xB6, 0xF7]



    ptr = 0x118A74 + 1
    #assert list(raw_data)[ptr + 0] == ((dump_address >> 0) & 0xff)
    #assert list(raw_data)[ptr + 1] == ((dump_address >> 8) & 0xff)
    #assert list(raw_data)[ptr + 2] == ((dump_address >> 16) & 0xff)
    #assert list(raw_data)[ptr + 3] == ((dump_address >> 24) & 0xff)
