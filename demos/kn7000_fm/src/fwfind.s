# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# COPIED unchanged from Another World's port (technics-anotherworld kn7000/src/fwfind.s, at 03fd8e6): KN7FM needs
# the same firmware services, found the same way ("the game" below is the program that runs it). Its tool,
# technics-anotherworld kn7000/tools/aw_find_fw.py, checks these searches over every program build.
#
# fwfind.s -- the game's run-time firmware search. KN7APP.BIN carries no firmware address: on its
# first calls it finds what it needs by the code, in the program flash (0x48400000-0x487FFFFF) and in
# the kernel library the boot copies to RAM at 0x4C000000, the way KN7LOAD.CNV, the loader that runs
# it, does (technics-roms-disasm kn7000/tools/cnv_romdump/loader.s, whose FindSig, CountSig and
# Read32U are reused here; that loader ran on the owner's program 893 on 2026-10-19). The SD file
# routines are the loader's: it found them by the same kind of search and hands them over in its
# service table (kn7cnv.inc), which step 2 checks.
# tools/aw_find_fw.py runs exactly these searches, with the same rules, over the program images of
# 893, 935, 941 and 942, and checks the 941 and 942 results against the addresses measured in MAME.
#
# FwStep(d0 = step 0..FW_STEPS-1) -> d0 = 0 when that step found everything it looks for, else 1.
# One step per converter call (cnv_main.s), each a scan or two of the flash. What they find:
#   0 panel   the panel decoder's two "mov CPR, a0 ... mov CPL, a0" sites: one hit each, the same two
#             bitmaps, CPL = CPR + 0x30, CPR in work RAM                         -> FWV_PANEL
#   1 clock   the routine the firmware yields with (adds d0 ms to the kernel's millisecond counter
#             and rotates the ready queue until then), in the library RAM: one hit, its three counter
#             operands equal; and the kernel's 1 kHz tick handler: one hit, its two counter operands
#             equal to that, and to the loader's (SV_MSCOUNT) when it found one  -> FW_DLY, FWV_KERNEL_MS
#   2 files   the loader's service table at SVC: magic "KN7S", the loader ABI this build was made for,
#             file access found (FOUND_FILESIG or FOUND_TABLES), and an address for each of open, read,
#             close and the drive switch pair                -> FW_OPEN, FW_READ, FW_CLOSE, FW_SETEXT, FW_RESETEXT
#             (and, when the loader has them, write and the program build: FW_WRITE, FWV_BUILD)
#   3 tasks   the firmware's own "mov N, d0; mov X, a0; call cre_tsk; mov N, d0; clr d1; call sta_tsk"
#             sites with both targets in the library: at least two, all agreeing, each a kernel entry
#             stub ("add -4, sp; mov d1, (0, sp)")                               -> FW_CRE, FW_STA
#   4 exit    its "clr d0; movbu d0, (X); call exd_tsk; ret" sites: at least two, all agreeing, a
#             stub that is neither cre_tsk nor sta_tsk                           -> FW_EXD
#   5 rings   the library's per-port MIDI ring wrappers: one hit, both headers in work RAM, MIDI IN 1
#             below MIDI OUT 1                                                   -> FWV_MIDIIN1, FWV_MIDIOUT1
#   6, 7 dsp  the effects DSP's host-port routines, one signature a step (dsp.s DspFindStep, DSPTONE's
#             search): the game goes on without the DSP when they are not all found -> DV_FOUND, DV_D*
# Calling convention as the rest of the port; every routine that calls makes a frame first.

.equ FL_LO,         0x48400000      # the program flash, as the dumper scans it
.equ FL_HI,         0x487FFF00
.equ LIB_LO,        0x4C000000      # the kernel library in RAM
.equ LIB_HI,        0x4C040000
.equ WR_LO,         0x50000000      # work RAM: where a found RAM address must lie
.equ WR_HI,         0x503FFF00
# the search's scratch (FW_VARS + 0x40.., platformcnv.inc)
.equ FS_LASTHIT,    FW_VARS + 0x40  # CountSig: the first hit
.equ FS_A,          FW_VARS + 0x54  # a found pair, while the rest of the sites are checked
.equ FS_B,          FW_VARS + 0x58
.equ FS_N,          FW_VARS + 0x5C
.equ FS_BAD,        FW_VARS + 0x60

FwStep:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    cmp     0, d0
    beq     fw_panel
    cmp     1, d0
    beq     fw_clock
    cmp     2, d0
    beq     fw_files
    cmp     3, d0
    beq     fw_tasks
    cmp     4, d0
    beq     fw_exit
    cmp     5, d0
    beq     fw_rings
    cmp     6, d0
    beq     fw_dsp
    cmp     7, d0
    beq     fw_dsp
    bra     fw_no

# ---- 6, 7: the effects DSP's host-port routines (dsp.s DspFindStep): never fails -----------------
fw_dsp:
    add     -6, d0
    call    DspFindStep, 0, 0
    bra     fw_ret

# ---- 0: the panel switch bitmaps ----------------------------------------------------------------
fw_panel:
    mov     sig_panel1, a0
    call    CountFlash, 0, 0
    cmp     1, d0
    bne     fw_no
    mov     (FS_LASTHIT), a2
    mov     sig_panel2, a0
    call    CountFlash, 0, 0
    cmp     1, d0
    bne     fw_no
    mov     (FS_LASTHIT), a3
    mov     a2, a0                  # CPR, from the first site
    add     2, a0
    call    Read32U, 0, 0
    mov     d0, d2
    mov     a3, a0                  # the same from the second
    add     2, a0
    call    Read32U, 0, 0
    cmp     d2, d0
    bne     fw_no
    mov     a2, a0                  # CPL from both, = CPR + 0x30
    add     15, a0
    call    Read32U, 0, 0
    mov     d0, d3
    mov     a3, a0
    add     13, a0
    call    Read32U, 0, 0
    cmp     d3, d0
    bne     fw_no
    mov     d2, d0
    add     PANEL_CPL_OFS, d0
    cmp     d3, d0
    bne     fw_no
    mov     d2, d0
    call    InWorkRam, 0, 0
    cmp     0, d0
    bne     fw_no
    mov     d2, (FWV_PANEL)
    bra     fw_yes

# ---- 1: the yield routine and the kernel's millisecond counter ----------------------------------
fw_clock:
    mov     sig_dly, a0
    mov     LIB_LO, d0
    mov     LIB_HI - 0x100, d1
    call    CountSig, 0, 0
    cmp     1, d0
    bne     fw_no
    mov     (FS_LASTHIT), a2
    mov     a2, a0
    add     7, a0
    call    Read32U, 0, 0
    mov     d0, d2                  # the counter, three times in the routine
    mov     a2, a0
    add     14, a0
    call    Read32U, 0, 0
    cmp     d2, d0
    bne     fw_no
    mov     a2, a0
    add     30, a0
    call    Read32U, 0, 0
    cmp     d2, d0
    bne     fw_no
    mov     sig_tick, a0            # and twice in the tick handler
    call    CountFlash, 0, 0
    cmp     1, d0
    bne     fw_no
    mov     (FS_LASTHIT), a3
    mov     a3, a0
    add     7, a0
    call    Read32U, 0, 0
    cmp     d2, d0
    bne     fw_no
    mov     a3, a0
    add     14, a0
    call    Read32U, 0, 0
    cmp     d2, d0
    bne     fw_no
    mov     d2, d0
    call    InWorkRam, 0, 0
    cmp     0, d0
    bne     fw_no
    mov     (SVC+SV_MSCOUNT), d0    # the loader's own find of the counter (0 = none): the same
    cmp     0, d0
    beq     fc_ok
    cmp     d2, d0
    bne     fw_no
fc_ok:
    mov     a2, (FW_DLY)
    mov     d2, (FWV_KERNEL_MS)
    bra     fw_yes

# ---- 2: the SD file routines, from the loader's service table ------------------------------------
# The loader (KN7LOAD.CNV) has found them and checked them against each other before it read
# KN7APP.BIN with them; here the table must be the one this build was made for. Only loads from the
# table: the game never writes the loader's resident area (tools/ic23_guard.py --loader).
fw_files:
    mov     (SVC+SV_MAGIC), d0
    mov     0x53374E4B, d1          # "KN7S"
    cmp     d1, d0
    bne     fw_no
    mov     (SVC+SV_ABI), d0
    cmp     LOADER_ABI, d0
    bne     fw_no
    mov     (SVC+SV_FOUND), d0
    and     FOUND_FILESIG|FOUND_TABLES, d0
    beq     fw_no
    mov     (SVC+SV_OPEN), d0
    cmp     0, d0
    beq     fw_no
    mov     d0, (FW_OPEN)
    mov     (SVC+SV_READ), d0
    cmp     0, d0
    beq     fw_no
    mov     d0, (FW_READ)
    mov     (SVC+SV_CLOSE), d0
    cmp     0, d0
    beq     fw_no
    mov     d0, (FW_CLOSE)
    mov     (SVC+SV_SETEXT), d0
    cmp     0, d0
    beq     fw_no
    mov     d0, (FW_SETEXT)
    mov     (SVC+SV_RESETEXT), d0
    cmp     0, d0
    beq     fw_no
    mov     d0, (FW_RESETEXT)
    mov     (SVC+SV_WRITE), d0      # optional: the speed report AWPERF.TXT at quit (0: not written)
    mov     d0, (FW_WRITE)
    mov     (SVC+SV_BUILD), d0      # optional: the program build, for the speed figures
    mov     d0, (FWV_BUILD)
    bra     fw_yes

# ---- 3: cre_tsk and sta_tsk, from the firmware's own call sites ----------------------------------
fw_tasks:
    clr     d0
    mov     d0, (FS_N)
    mov     d0, (FS_BAD)
    mov     FL_LO, a2
ft_next:
    mov     sig_cresta, a0
    mov     a2, d0
    mov     FL_HI, d1
    call    FindSig, 0, 0
    cmp     0, d0
    beq     ft_done
    mov     d0, a2                  # a hit: "fc dc X dd R 00 00 80 ?? 04 dd R 00 00"
    movbu   (-2, a2), d0            # preceded by "mov N, d0" with the same N as the second one
    cmp     0x80, d0
    bne     ft_skip
    movbu   (-1, a2), d0
    movbu   (14, a2), d1
    cmp     d1, d0
    bne     ft_skip
    mov     a2, a0                  # cre_tsk = hit + 6 + rel32
    add     7, a0
    call    Read32U, 0, 0
    add     6, d0
    add     a2, d0
    mov     d0, d2
    mov     a2, a0                  # sta_tsk = hit + 16 + rel32
    add     17, a0
    call    Read32U, 0, 0
    add     16, d0
    add     a2, d0
    mov     d0, d3
    mov     d2, d0                  # both into the library, or not a site of these
    call    InLib, 0, 0
    cmp     0, d0
    bne     ft_skip
    mov     d3, d0
    call    InLib, 0, 0
    cmp     0, d0
    bne     ft_skip
    mov     (FS_N), d0
    cmp     0, d0
    bne     ft_cmp
    mov     d2, (FS_A)
    mov     d3, (FS_B)
    bra     ft_count
ft_cmp:
    mov     (FS_A), d0              # every site must name the same two routines
    cmp     d2, d0
    bne     ft_disagree
    mov     (FS_B), d0
    cmp     d3, d0
    beq     ft_count
ft_disagree:
    mov     1, d0
    mov     d0, (FS_BAD)
ft_count:
    mov     (FS_N), d0
    inc     d0
    mov     d0, (FS_N)
ft_skip:
    inc     a2
    bra     ft_next
ft_done:
    mov     (FS_BAD), d0
    cmp     0, d0
    bne     fw_no
    mov     (FS_N), d0
    cmp     2, d0
    blt     fw_no
    mov     (FS_A), a0
    call    IsStub, 0, 0
    cmp     0, d0
    bne     fw_no
    mov     (FS_B), a0
    call    IsStub, 0, 0
    cmp     0, d0
    bne     fw_no
    mov     (FS_A), d0
    mov     d0, (FW_CRE)
    mov     (FS_B), d0
    mov     d0, (FW_STA)
    bra     fw_yes

# ---- 4: exd_tsk, from the firmware's tasks that end themselves ------------------------------------
fw_exit:
    clr     d0
    mov     d0, (FS_N)
    mov     d0, (FS_BAD)
    mov     FL_LO, a2
fx_next:
    mov     sig_exd, a0
    mov     a2, d0
    mov     FL_HI, d1
    call    FindSig, 0, 0
    cmp     0, d0
    beq     fx_done
    mov     d0, a2                  # a hit: "fc 82 X dd R 00 00 df", preceded by "clr d0"
    movbu   (-1, a2), d0
    cmp     0, d0
    bne     fx_skip
    mov     a2, a0                  # exd_tsk = hit + 6 + rel32
    add     7, a0
    call    Read32U, 0, 0
    add     6, d0
    add     a2, d0
    mov     d0, d2
    call    InLib, 0, 0
    cmp     0, d0
    bne     fx_skip
    mov     (FS_N), d0
    cmp     0, d0
    bne     fx_cmp
    mov     d2, (FS_A)
    bra     fx_count
fx_cmp:
    mov     (FS_A), d0
    cmp     d2, d0
    beq     fx_count
    mov     1, d0
    mov     d0, (FS_BAD)
fx_count:
    mov     (FS_N), d0
    inc     d0
    mov     d0, (FS_N)
fx_skip:
    inc     a2
    bra     fx_next
fx_done:
    mov     (FS_BAD), d0
    cmp     0, d0
    bne     fw_no
    mov     (FS_N), d0
    cmp     2, d0
    blt     fw_no
    mov     (FS_A), a0
    call    IsStub, 0, 0
    cmp     0, d0
    bne     fw_no
    mov     (FS_A), d0              # and not one of the two found before
    mov     (FW_CRE), d1
    cmp     d1, d0
    beq     fw_no
    mov     (FW_STA), d1
    cmp     d1, d0
    beq     fw_no
    mov     d0, (FW_EXD)
    bra     fw_yes

# ---- 5: the MIDI rings -------------------------------------------------------------------------
fw_rings:
    mov     sig_rings, a0
    call    CountFlash, 0, 0
    cmp     1, d0
    bne     fw_no
    mov     (FS_LASTHIT), a2
    mov     a2, a0
    add     17, a0
    call    Read32U, 0, 0
    mov     d0, d2                  # MIDI IN 1 receive
    mov     a2, a0
    add     35, a0
    call    Read32U, 0, 0
    mov     d0, d3                  # MIDI OUT 1 transmit
    cmp     d3, d2
    bcc     fw_no                   # IN below OUT
    mov     d2, d0
    call    InWorkRam, 0, 0
    cmp     0, d0
    bne     fw_no
    mov     d3, d0
    call    InWorkRam, 0, 0
    cmp     0, d0
    bne     fw_no
    mov     d2, (FWV_MIDIIN1)
    mov     d3, (FWV_MIDIOUT1)
fw_yes:
    clr     d0
    bra     fw_ret
fw_no:
    mov     1, d0
fw_ret:
    ret     [d2,d3,a2,a3], 0x30

# InWorkRam(d0 = address) -> d0 = 0 inside [WR_LO, WR_HI], else 1. Leaf.
InWorkRam:
    mov     WR_LO, d1
    cmp     d1, d0
    bcs     iw_no
    mov     WR_HI, d1
    cmp     d1, d0
    bhi     iw_no
    clr     d0
    rets
iw_no:
    mov     1, d0
    rets

# InLib(d0 = address) -> d0 = 0 inside [LIB_LO, LIB_HI), else 1. Leaf.
InLib:
    mov     LIB_LO, d1
    cmp     d1, d0
    bcs     il_no
    mov     LIB_HI, d1
    cmp     d1, d0
    bcc     il_no
    clr     d0
    rets
il_no:
    mov     1, d0
    rets

# IsStub(a0 = routine) -> d0 = 0 when it starts with a kernel entry stub's
# "add -4, sp; mov d1, (0, sp)" (f8 fe fc 46 00), else 1. Leaf.
IsStub:
    movbu   (a0), d0
    cmp     0xf8, d0
    bne     is_no
    movbu   (1, a0), d0
    cmp     0xfe, d0
    bne     is_no
    movbu   (2, a0), d0
    cmp     0xfc, d0
    bne     is_no
    movbu   (3, a0), d0
    cmp     0x46, d0
    bne     is_no
    movbu   (4, a0), d0
    cmp     0, d0
    bne     is_no
    rets
is_no:
    mov     1, d0
    rets

# CountFlash(a0 = signature) -> d0 = hits in the program flash; FS_LASTHIT = the first.
CountFlash:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     FL_LO, d0
    mov     FL_HI, d1
    call    CountSig, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# CountSig(a0 = signature, d0 = start, d1 = end) -> d0 = hits; FS_LASTHIT = the first. (loader.s)
CountSig:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a2
    mov     d1, d3
    clr     d2
    mov     d2, (FS_LASTHIT)
cs_next:
    mov     a2, a0
    mov     d3, d1
    call    FindSig, 0, 0
    cmp     0, d0
    beq     cs_done
    cmp     0, d2
    bne     cs_more
    mov     d0, (FS_LASTHIT)
cs_more:
    inc     d2
    inc     d0
    bra     cs_next
cs_done:
    mov     d2, d0
    ret     [d2,d3,a2,a3], 0x30

# FindSig(a0 = signature, d0 = start, d1 = end) -> d0 = first address in [start, end) where it
# matches, or 0. Elements are 16-bit: 0x00nn = byte nn, 0x0100 = any byte, 0xFFFF = end; the first
# is a byte. (loader.s; the scan for the first byte is DSPTONE's, technics-roms-disasm
# kn7000/tools/dsptone/dsptone.s FindSig: five instructions a byte, all in registers, the loop on a
# 4-byte boundary. On MAME's MN103002A timing, calibrated against the owner's unit (ROM dumper pass 9),
# DSPTONE's scan of the whole flash took 0.88 s that way and 4.85 s with the loop at an odd address;
# the rest of the signature is compared only where the first byte matches.)
FindSig:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a2
    movhu   (a2), d3
    mov     d0, a3
    mov     d1, a1
    .align  4, 0xCB                 # nops: the loop starts on a fetch word (the scan's speed depends on it)
fs_loop:
    cmp     a1, a3
    bcc     fs_none
    movbu   (a3), d2
    inc     a3
    cmp     d3, d2
    bne     fs_loop
    mov     a1, (0x14, sp)          # a first byte at a3 - 1: the rest
    mov     a2, a0
    add     2, a0
    mov     a3, a1
fs_cmp:
    movhu   (a0), d2
    mov     0xFFFF, d0
    cmp     d0, d2
    beq     fs_hit
    btst    0x100, d2
    bne     fs_any
    movbu   (a1), d1
    cmp     d2, d1
    bne     fs_miss
fs_any:
    add     2, a0
    inc     a1
    bra     fs_cmp
fs_miss:
    mov     (0x14, sp), a1
    bra     fs_loop
fs_hit:
    mov     a3, d0
    add     -1, d0
    bra     fs_ret
fs_none:
    clr     d0
fs_ret:
    ret     [d2,d3,a2,a3], 0x30

# Read32U(a0 = address) -> d0 = the little-endian u32 there, at any alignment. Leaf. (loader.s)
Read32U:
    movbu   (3, a0), d0
    asl     8, d0
    movbu   (2, a0), d1
    or      d1, d0
    asl     8, d0
    movbu   (1, a0), d1
    or      d1, d0
    asl     8, d0
    movbu   (a0), d1
    or      d1, d0
    rets

# =================================================================================================
# The signatures: 0x00nn = byte nn, 0x0100 = any byte (an operand that moves between builds), 0xFFFF
# ends. tools/aw_find_fw.py carries the same ones.
    .align  2
sig_panel1:     # PanelFrameDecode: mov CPR, a0; mov a2, d0; and 0xc0, d0; beq; mov CPL, a0; ... (941 0x484AD22F)
    .word   0xfc, 0xdc, 0x100, 0x100, 0x100, 0x100, 0xf1, 0xd8, 0xf8, 0xe0, 0xc0, 0xc8, 0x08
    .word   0xfc, 0xdc, 0x100, 0x100, 0x100, 0x100, 0xf1, 0xd8, 0xf8, 0xe0, 0x0f, 0xf1, 0x60
    .word   0xf0, 0x40, 0xf0, 0x5c, 0xFFFF
sig_panel2:     # the same choice in the next routine (941 0x484AD3DD)
    .word   0xfc, 0xdc, 0x100, 0x100, 0x100, 0x100, 0xf8, 0xef, 0xc0, 0xc8, 0x08
    .word   0xfc, 0xdc, 0x100, 0x100, 0x100, 0x100, 0xf8, 0xb8, 0x10, 0x85, 0x0f, 0xf2, 0x0d
    .word   0xf1, 0x64, 0xFFFF
sig_dly:        # the yield routine: d2 = MS + d0; while (MS < d2) rotate the ready queue (941 lib 0x4C03DD6F)
    .word   0xcf, 0xa0, 0xf8, 0xfe, 0xf0, 0xfc, 0xa6, 0x100, 0x100, 0x100, 0x100, 0xe2
    .word   0xfc, 0xa4, 0x100, 0x100, 0x100, 0x100, 0xa2, 0xc7, 0x100, 0x00, 0x81
    .word   0xcd, 0x100, 0x100, 0x00, 0x00, 0xfc, 0xa4, 0x100, 0x100, 0x100, 0x100
    .word   0xa2, 0xc5, 0x100, 0x00, 0xdf, 0xa0, 0x18, 0xFFFF
sig_tick:       # the kernel's 1 kHz tick handler: mov (MS), d0; inc d0; mov d0, (MS) (loader.s)
    .word   0xcf, 0x80, 0xf8, 0xfe, 0xf0, 0xfc, 0xa4, 0x100, 0x100, 0x100, 0x100, 0x40
    .word   0xfc, 0x81, 0x100, 0x100, 0x100, 0x100, 0xf8, 0xec, 0x01, 0xc8, 0x09
    .word   0xdd, 0x100, 0x100, 0x100, 0x100, 0x80, 0x0c, 0xfa, 0xfc, 0xff, 0xf7, 0xcb, 0xcb
    .word   0xfc, 0xac, 0x18, 0x01, 0x00, 0x34, 0xfa, 0xe0, 0x00, 0xff, 0xf8, 0xe4, 0x01
    .word   0xfc, 0x83, 0x18, 0x01, 0x00, 0x34, 0xFFFF
sig_cresta:     # mov X, a0; call cre_tsk, 0, 0; mov N, d0; clr d1; call sta_tsk, 0, 0 (941 0x48405D51)
    .word   0xfc, 0xdc, 0x100, 0x100, 0x100, 0x100, 0xdd, 0x100, 0x100, 0x100, 0x100, 0x00, 0x00
    .word   0x80, 0x100, 0x04, 0xdd, 0x100, 0x100, 0x100, 0x100, 0x00, 0x00, 0xFFFF
sig_exd:        # movbu d0, (X); call exd_tsk, 0, 0; ret (941 0x48472381)
    .word   0xfc, 0x82, 0x100, 0x100, 0x100, 0x100, 0xdd, 0x100, 0x100, 0x100, 0x100, 0x00, 0x00
    .word   0xdf, 0xFFFF
sig_rings:      # the end of one wrapper, then MIDI IN 1's and MIDI OUT 1's enqueue wrappers (941 0x487DD987)
    .word   0x2c, 0xff, 0x03, 0xfc, 0x83, 0x100, 0x100, 0x100, 0x100, 0xde, 0x00, 0x00
    .word   0xf8, 0xfe, 0xf0, 0xfc, 0xdc, 0x100, 0x100, 0x100, 0x100, 0x14, 0xcd, 0x100, 0x100
    .word   0x30, 0x08, 0xdf, 0x00, 0x10, 0xf8, 0xfe, 0xf0, 0xfc, 0xdc, 0x100, 0x100, 0x100, 0x100
    .word   0x14, 0xcd, 0x100, 0x100, 0x30, 0x08, 0xdf, 0x00, 0x10, 0xFFFF
    .align  4
