def run_code_from_RCM__patch(raw_data):
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
