def insert_code_snippet_with_preamble__patch(apply_dangerous_patch=False, maincpu_patches):
    if apply_dangerous_patch: # DANGEROUS_DO_NOT_RUN_THIS_ON_REAL_KN5000
        maincpu_patches[0xEF0526] = [
            0x1B, 0x00, 0x20, 0xEE,   # JP PREAMBLE
            0x1B, 0x2a, 0x05, 0xEF,   # inf. loop
        ]

    # TODO: try to use utils.patch_from_rom_file here as well:

    code_to_insert = list(open("demos/monitor/monitor.rom", "rb").read())
    PREAMBLE_address = 0xE0018E # one of the fw-update bitmaps
    upper_limit = 0xE008C6 # another fw-update bitmap
    codeinsert_address = PREAMBLE_address + 0x20

    PREAMBLE_address_bytes = [
        (PREAMBLE_address >> (i*8)) & 0xFF for i in range(3)]
    codeinsert_address_bytes = [
        (codeinsert_address >> (i*8)) & 0xFF for i in range(3)]

    maincpu_patches[0xEF05E8] = [   # Patched at "User_didnt_request_flash_mem_update"
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
    
    hex_print(maincpu_patches[0xEF05E8])
    hex_print(PREAMBLE)

    maincpu_patches[PREAMBLE_address] = PREAMBLE # <-- faz patch do preambulo
    assert len(PREAMBLE) < (codeinsert_address - PREAMBLE_address)
    maincpu_patches[codeinsert_address] = code_to_insert # <-- faz patch da ROM (monitor.rom)

    assert codeinsert_address + len(code_to_insert) <= upper_limit
