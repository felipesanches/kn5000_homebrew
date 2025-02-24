def run_at_boot__patch():
    maincpu_patches[0xEF0541] = [  # Replace initial "Please Wait !!" message:
        0xFE, 0x0F   # 0x00E00FFE = "Illegal Disk !!"
    ]

    maincpu_patches[0xEF05E8] = [   # Patched at "User_didnt_request_flash_mem_update"
        # 0x1D, 0xA7, 0x55, 0xEF,        #	CALL Some_VGA_setup
        0x08, 0x08, 0x00,              #	PUSH, 0x0008
        0x08, 0x03, 0x00,              #	PUSH, 0x0003
        0x40, 0x66, 0x12, 0xE0, 0x00,  #	LD XWA, 0x00e01266  # "Turn On AGAIN !!"
        0x31, 0x30, 0x00,              #	LD BC, 0x0030
        0x32, 0x50, 0x00,              #	LD DE, 0x0050
        0x1D, 0x40, 0x50, 0xEF,        #	CALL Draw_FlashMemUpdate_message_bitmap
        0x68, 0xFE  #	JP infinite_loop
    ]
