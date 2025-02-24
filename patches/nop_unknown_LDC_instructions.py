def nop_unknown_LDC_instructions__patch(maincpu_patches):
    maincpu_patches[0xEF1984] = [  # Replace ldc unknown (encoding is 0x7C), WA   | D8 2E 7C
        0x00, 0x00, 0x00   # NOP NOP NOP
    ]
    maincpu_patches[0xEF1AAD] = [  # Replace ldc unknown (encoding is 0x7C), WA   | D8 2E 7C
        0x00, 0x00, 0x00   # NOP NOP NOP
    ]
    maincpu_patches[0xEF1B89] = [  # Replace ldc unknown (encoding is 0x7C), WA   | D8 2E 7C
        0x00, 0x00, 0x00   # NOP NOP NOP
    ]

# ldc DMAS0, WA     | E8 2E 00
# ldc DMAD2, WA     | E8 2E 28
# ldc DMAM0, A      | C9 2E 42
# ldc DMAM2, A      | C9 2E 4A
# ldcf 7, (0x0406)  | F1 06 04 9F (at EF0797)
