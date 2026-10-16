# KN7000 uptime clock

A small homebrew app for the **Technics SX-KN7000** running Key Soft Service's
**SD PLUG-iN** (SP7001) firmware — program build 942 / table build 83, MAME
`kn7000 -bios sp7001`. It shows how long the instrument has been switched on, as
`HH:MM:SS`, in a box in the bottom-right corner of the LCD, updated five times a
second on every screen.

It is the first KN7000 demo in this repo, next to the KN5000 ones; written in
MN10300 assembly (`clock.s`, about 1.2 KB).

## How it gets to run

The SP7001 firmware extends itself from the SD card: the first time the SD card
screen is opened (the orange **SD CARD** button), it loads the base plug-in
`TECHNICS\DATA\BASEEXT.XAP` — the whole `0x1D000`-byte file — to RAM `0x5029B000`
and calls its first byte. This app does not replace that plug-in; it rides along
with it:

1. `tools/xap_addon.py` (in `kn7000_disassembly`) copies the assembled module into
   the plug-in file's unused tail (`0x502B6000`, all `0xFF` in the original) and
   replaces the plug-in's first two instructions with a jump to the module.
2. The module creates and starts a task of its own through the plug-in API's
   micro-ITRON services (`cre_tsk_X`, `sta_tsk`: id 10, or 9 if taken; priority 3,
   like the firmware's own tasks), runs the two displaced instructions and jumps
   back. The SD card features work as before.
3. The task reads the kernel's 1 kHz tick counter at `0x500D3C68` (milliseconds,
   counting from about 0.27 s after power-on), formats it, draws the box straight
   into the LCD frame at `0x9CE00000` (640×240, RGB565), then sleeps 200 ms with
   `dly_tsk`.

The KN7000 has no real-time clock, so "time since power-on" is the time it can
show. The firmware facts above are documented, with how each was established, on
the docs site page *KN7000 Custom SD Apps* and in `kn7000_disassembly`'s
`notes/sp7001-build-diff.md`.

## Building

Needs a checkout of [`kn7000_disassembly`](../../../kn7000_disassembly) beside this
repo (`DISASM_REPO`), which provides the assembler (`tools/xapasm.py`) and the XAP
and SD-card tools, and in it the vendor `BASEEXT.XAP` extracted from the SP7001
update disks:

    make -C ../../../kn7000_disassembly baserom/sp7001_baseext.xap
    make            # -> build/BASEEXT.XAP  (the vendor file plus the clock)
    make card       # -> build/kn7000_clock_sd.img, a copy of SD_IMAGE with it in place
    make run        # boots MAME (kn7000 -bios sp7001) with that card

The vendor plug-in is not redistributed: the build adds the clock to your own copy.
`SD_IMAGE` must be a card the SP7001 setup has prepared (it carries the
`TECHNICS\DATA` tree); `MAME` and `ROMPATH` select the emulator. After boot, press
the orange SD CARD button once; from then on the clock stays up.

On an instrument, copy `build/BASEEXT.XAP` over `TECHNICS\DATA\BASEEXT.XAP` on the
SP7001 card, keeping the original file as a backup.

## Files

| file       | what it is                                                          |
|------------|---------------------------------------------------------------------|
| `clock.s`  | the module: entry hook, task set-up, time formatting, renderer      |
| `mkfont.py`| generates `build/font.inc`: 5×7 digits and colon, pre-scaled ×3     |
| `Makefile` | build, SD-card image, MAME run                                      |

## Limits

- The clock starts when the plug-in loads, i.e. at the first visit to the SD card
  screen after power-on; the time shown is still counted from power-on.
- Hours wrap at 100.
- When the firmware repaints the corner the clock sits in, the clock reappears
  within 200 ms.

## Status

Built and statically checked (the assembled module disassembles back to the
source). Not yet run.
