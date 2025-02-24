def patch_from_rom_file(patches, filename, patch_address, upper_limit):
    code_to_insert = list(open(filename, "rb").read())
    msg = (
        f"end address = {patch_address + len(code_to_insert):06X} exceeds upper_limit"
        f" by {patch_address + len(code_to_insert) - upper_limit} bytes"
	)
    assert patch_address + len(code_to_insert) <= upper_limit, msg
    patches[patch_address] = code_to_insert
