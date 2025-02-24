from patches.utils import patch_from_rom_file


def subcpu_dump_bootrom_via_serial__patch(subcpu_patches):
    # ROTINA NECESSARIAS:
    # subcpu_patches[0x20F1F] = [ 0x0E ] # MICRODMA_CH0_HANDLER: RET
    # subcpu_patches[0x1FB41] = [ 0x0E ] # ?: RET
    # subcpu_patches[0x1FDC8] = [ 0x0E ] # ?: RET  ; sem ela, leitura vai só até endereço  0x000B

    # não preciso, mas tmb náo é quem zica:
    subcpu_patches[0x20E86] = [ 0x0E ] # INT0_HANDLER: RET
    subcpu_patches[0x20F01] = [ 0x0E ] # MICRODMA_CH2_HANDLER: RET
    subcpu_patches[0x1FBBD] = [ 0x0E ] # watchdog: RET
    subcpu_patches[0x1FBF4] = [ 0x0E ] # mute and halt: RET
    
    subcpu_patches[0x1f90f] = [
        0x2B,			# SC1MOD => 8-bit UART, 38400 baud via external clock
    ]
    subcpu_patches[0x1fad0] = [ # This replaces the INIT_RING_BUFFER function
                                # and then stops in an infinite loop.
        0x1D, 0x0B, 0xF9, 0x01,			# 1fad0: CALL 1f90b  # initializes serial port #1 and writes a 0xFE byte
        0xF1, 0x38, 0x10, 0x00, 0x03,	# 1fad4: set var_1038 = 03
        0x1B, 0xD9, 0xFA, 0x01,			# 1fad9: JP 1fad9  # infinite loop
    ]

    # TODO: fix this:
    #patch_from_rom_file(
    #    subcpu_patches,
    #    filename = "demos/dump_subcpu/subcpu_receive.rom",
    #    patch_address = 0x1f736,  # INTRX1_HANDLER
    #    upper_limit = 0x1F765 # INTTX1_HANDLER
    #)

    patch_from_rom_file(
        subcpu_patches,
        filename = "demos/dump_subcpu/subcpu_send.rom",
        patch_address = 0x1f765,  # INTTX1_HANDLER
        upper_limit = 0x1F7B9 # READ_BYTE_FROM_RING_BUFFER
    )

    # Locations of interest:
    # string "KN5000 SOUND RAM" at 0x0120E3
    # payload at 0x00f000-2eeff
    # subcpu boot rom at fe0000-ffffff
