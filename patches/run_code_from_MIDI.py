def run_code_from_MIDI__patch(raw_data):
    #ptr = 0x187600
#    ptr = 0x1DB4A1
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
