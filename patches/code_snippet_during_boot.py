from utils import patch_from_rom_file

def code_snippet_during_boot__patch(maincpu_patches):
    patch_from_rom_file(
        maincpu_patches,
        filename = "demos/monitor/monitor.rom",
        patch_address = 0xEF05E8, # "User_didnt_request_flash_mem_update"
        upper_limit = 0xEF061E # why?
    )
