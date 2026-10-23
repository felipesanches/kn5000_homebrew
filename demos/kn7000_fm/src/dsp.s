# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# dsp.s -- KN7FM's side of the KN7000's effects DSP (IC306, an ADSP-21065L): the FM engine (tools/fm_dsp.py)
# loaded, hooked into the firmware kernel's unit slots 0 and 9, fed notes and patches, and put back.
#
# COPIED from Another World's port (technics-anotherworld kn7000/src/dsp.s, at 03fd8e6), itself DSPTONE's
# (technics-roms-disasm kn7000/tools/dsptone, which ran on Felipe's KN7000, program 893, on 2026-10-21): the search
# for the firmware's own host-port routines (DspFindStep, DspFind), the routines built on them (DspRead, DspWrite,
# DspUpload, DspReadback, Verify, the slots), the hook and its undoing (DspHook, DspUnhook, DspGiveUp). Their rules
# are kept: every access goes through the firmware's routines (its lock, its per-frame handshake wait, its "DSP
# dead" guard); the DSP is checked to be as the firmware leaves it before anything is touched; everything uploaded
# is read back; the two CALL slots, MSGR4-MSGR7 and channel 8's registers are put back on every way out.
# Then KN7FM's own (from "KN7FM's own" below): DspPrepare for the FM engine, the kernel swap, the patch upload,
# the key commands.
#
# Calling convention as the rest of the program. Every routine here that calls a firmware routine makes a frame of
# 0x20 bytes first (movm + add -0x20, sp): the firmware's routines store into 4..0x10(sp) of their caller's frame,
# and the register write takes its third argument at (0x10, sp).

# ==============================================================================================================
# The search (the converter's search steps 6 and 7, fwfind.s FwStep): one signature per call.
# DspFindStep(d0 = 0: the handshake wait, 1: the register routines, then the cross-checks) -> d0 = 0 always (the
# game runs without the DSP when something is not found).
DspFindStep:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    cmp     0, d0
    bne     dfs_regs
    mov     sig_dspwait, a0
    call    CountFlash, 0, 0
    mov     d0, (DV_SIGN0)
    mov     (FS_LASTHIT), d0
    mov     d0, (DV_SIGA0)
    bra     dfs_ret
dfs_regs:
    mov     sig_dspregs, a0
    call    CountFlash, 0, 0
    mov     d0, (DV_SIGN1)
    mov     (FS_LASTHIT), d0
    mov     d0, (DV_SIGA1)
    call    DspFind, 0, 0
dfs_ret:
    clr     d0
    ret     [d2,d3,a2,a3], 0x30

# DspFind: the firmware's DSP host-port routines (941 / 893 addresses), as DSPTONE's DspFind:
#   sig_dspwait  the handshake wait (0x48404D25 / 0x4840AE77): its operands are the "kernel downloaded" flag
#                (+0x0F) and the "DSP dead" flag (+0x36);
#   sig_dspregs  the 2x16 register write and read (0x484052DD / 0x4840B3F5; the read at +0x3E);
#   the read's calls: the index write (body + 6) and the guarded data read (body + 0xE);
#   the guarded data write: the register write's second call (body + 0x14, the DM-word writer) makes it at its
#   own body + 0x11; it must start "movhu d1, (0x1c, sp); movhu d0, (0x18, sp); movbu (dead), d0".
# Every call target must be preceded by the "add -frame, sp" its call allocates. Only when all of it holds is
# DV_FOUND = DFB_ALL; otherwise the DSP is not touched at all. (technics-roms-disasm dsptone/find_dspw.py runs the
# same search over every program build: found and consistent in 893, 935, 941 and 942.)
DspFind:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    clr     d2
    mov     (DV_SIGN0), d0
    cmp     1, d0
    bne     dfd_regs
    or      DFB_WAIT, d2
    mov     (DV_SIGA0), a2
    mov     a2, (DV_DWAIT)
    mov     a2, a0
    add     0x0F, a0
    call    Read32U, 0, 0           # movbu (downloaded), d0
    mov     d0, (DV_DDOWN)
    mov     a2, a0
    add     0x36, a0
    call    Read32U, 0, 0           # movbu d0, (dead)
    mov     d0, (DV_DDEAD)
dfd_regs:
    mov     (DV_SIGN1), d0
    cmp     1, d0
    bne     dfd_end
    or      DFB_REGS, d2
    mov     (DV_SIGA1), a2
    mov     a2, (DV_DREGWR)
    mov     a2, a0
    add     0x3E, a0
    mov     a0, (DV_DREGRD)
    add     9, a0                   # the read's body + 6: call DspAddrWrite
    call    CallTarget, 0, 0
    cmp     0, d0
    beq     dfd_data
    call    CheckEntry, 0, 0
    cmp     0, d0
    beq     dfd_data
    mov     d0, (DV_DIDXWR)
    or      DFB_IDX, d2
dfd_data:
    mov     (DV_DREGRD), a0
    add     0x11, a0                # the read's body + 0xE: call the guarded data read
    call    CallTarget, 0, 0
    cmp     0, d0
    beq     dfd_write
    mov     d0, a3                  # its body: movbu (dead), d0 = fc a8 <dead>
    call    CheckEntry, 0, 0
    cmp     0, d0
    beq     dfd_write
    mov     d0, (DV_DDATARD)
    movbu   (a3), d0
    cmp     0xFC, d0
    bne     dfd_write
    movbu   (1, a3), d0
    cmp     0xA8, d0
    bne     dfd_write
    mov     a3, a0
    add     2, a0
    call    Read32U, 0, 0
    mov     (DV_DDEAD), d1
    cmp     d1, d0
    bne     dfd_write
    or      DFB_DATA, d2
dfd_write:
    mov     (DV_DREGWR), a0
    add     0x17, a0                # the write's body + 0x14: call the DM-word writer
    call    CallTarget, 0, 0
    cmp     0, d0
    beq     dfd_end
    mov     d0, a3
    call    CheckEntry, 0, 0
    cmp     0, d0
    beq     dfd_end
    mov     a3, a0
    add     0x11, a0                # its body + 0x11: call the guarded data write
    call    CallTarget, 0, 0
    cmp     0, d0
    beq     dfd_end
    mov     d0, a3
    call    CheckEntry, 0, 0
    cmp     0, d0
    beq     dfd_end
    mov     d0, (0x14, sp)
    mov     WriteHead, a0           # movhu d1, (0x1c, sp); movhu d0, (0x18, sp); movbu (abs32), d0
    clr     d3
dfd_head:
    mov     a3, a1
    add     d3, a1
    movbu   (a1), d0
    mov     a0, a1
    add     d3, a1
    movbu   (a1), d1
    cmp     d1, d0
    bne     dfd_end
    inc     d3
    cmp     8, d3
    bcs     dfd_head
    mov     a3, a0
    add     8, a0
    call    Read32U, 0, 0
    mov     (DV_DDEAD), d1
    cmp     d1, d0
    bne     dfd_end
    mov     (0x14, sp), d0
    mov     d0, (DV_DDATAWR)
    or      DFB_WRITE, d2
dfd_end:
    mov     d2, (DV_FOUND)
    ret     [d2,d3,a2,a3], 0x30

# CallTarget (leaf): a0 = address of a "call d16, regs, imm8" (cd lo hi regs imm) -> d0 = its target, d1 = its
# imm8 (the frame it allocates); d0 = 0 when a0 holds no such instruction.
CallTarget:
    movbu   (a0), d0
    cmp     0xCD, d0
    bne     ct_none
    movbu   (2, a0), d0
    asl     8, d0
    movbu   (1, a0), d1
    or      d1, d0
    exth    d0
    mov     a0, d1
    add     d1, d0
    movbu   (4, a0), d1
    rets
ct_none:
    clr     d0
    rets

# CheckEntry (leaf): d0 = a function body, d1 = the frame its callers allocate -> d0 = the entry `calls` needs,
# body - 3, which must be "add -frame, sp" (f8 fe -frame); or 0.
CheckEntry:
    mov     d0, a0
    add     -3, a0
    movbu   (a0), d0
    cmp     0xF8, d0
    bne     ce_no
    movbu   (1, a0), d0
    cmp     0xFE, d0
    bne     ce_no
    movbu   (2, a0), d0
    add     d1, d0
    and     0xFF, d0
    bne     ce_no
    mov     a0, d0
    rets
ce_no:
    clr     d0
    rets

# ==============================================================================================================
# The firmware's routines, and what is built on them (DSPTONE's).

# DspRead(d0 = IOP register) -> d0 = its 32 bits. The firmware's read returns (high << 16) + the low half
# sign-extended; when the low half's bit 15 is set that is 0x10000 short, which is added back here.
DspRead:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (DV_DREGRD), a0
    calls   (a0)
    btst    0x8000, d0
    beq     drd_ret
    mov     0x10000, d1
    add     d1, d0
drd_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspWrite(d0 = IOP register, d1 = 32-bit value): the firmware's 2x16 register write, low half first.
DspWrite:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d1, d2
    and     0xFFFF, d2
    mov     d2, (0x10, sp)          # its third argument: the low half
    lsr     16, d1                  # d1: the high half
    mov     (DV_DREGWR), a0
    calls   (a0)
    ret     [d2,d3,a2,a3], 0x30

DspIndex:                           # d0 = the index to latch at 0x98000000 (the firmware's guarded write)
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (DV_DIDXWR), a0
    calls   (a0)
    ret     [d2,d3,a2,a3], 0x30

DspData:                            # -> d0 = one 16-bit read of 0x9C000000 (0xFFFF once the DSP is "dead")
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (DV_DDATARD), a0
    calls   (a0)
    exthu   d0
    ret     [d2,d3,a2,a3], 0x30

DspDataW:                           # d0 = 16 bits to write to 0x9C000000 (the firmware's guarded write: nothing
    movm    [d2,d3,a2,a3], (sp)     #   once the DSP is "dead")
    add     -0x20, sp
    mov     d0, d1
    exthu   d1
    mov     DATA_PORT, d0
    mov     (DV_DDATAWR), a0
    calls   (a0)
    ret     [d2,d3,a2,a3], 0x30

DeadFlag:                           # leaf -> d0 = the firmware's dead flag byte (0 when not found)
    mov     (DV_DDEAD), a0
    clr     d0
    cmp     0, a0
    beq     dfl_ret
    movbu   (a0), d0
dfl_ret:
    rets

DownFlag:                           # leaf -> d0 = the "kernel downloaded" flag byte (0 when not found)
    mov     (DV_DDOWN), a0
    clr     d0
    cmp     0, a0
    beq     dwn_ret
    movbu   (a0), d0
dwn_ret:
    rets

# DspUpload(d0 = DSP address, d1 = words, a0 = their 16-bit halves, least significant first, a1 = DMAC_UP_PM or
# DMAC_UP_DM): the firmware's own upload of a block (941 0x48407AAB, 0x48407A86/0x48407A99, 0x48407ADF):
# IIEP0 = address, IMEP0 = 1, CEP0 = words, DMAC0 = 0xA1 (PM, three halves a word) or 0x41 (DM, two); the index
# latched to EPB0 once; the halves; DMAC0 = 0xA0.
DspUpload:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a2
    mov     a1, d3
    mov     d1, (0x14, sp)
    mov     d0, d1
    mov     IOP_IIEP0, d0
    call    DspWrite, 0, 0
    mov     IOP_IMEP0, d0
    mov     1, d1
    call    DspWrite, 0, 0
    mov     IOP_CEP0, d0
    mov     (0x14, sp), d1
    call    DspWrite, 0, 0
    mov     IOP_DMAC0, d0
    mov     d3, d1
    call    DspWrite, 0, 0
    mov     IOP_EPB0, d0
    call    DspIndex, 0, 0
    mov     (0x14, sp), d2          # halves: two a word, three for PM
    add     d2, d2
    cmp     DMAC_UP_PM, d3
    bne     dup_half
    mov     (0x14, sp), d0
    add     d0, d2
dup_half:
    cmp     0, d2
    beq     dup_close
    movhu   (a2), d0
    add     2, a2
    call    DspDataW, 0, 0
    add     -1, d2
    bra     dup_half
dup_close:
    mov     IOP_DMAC0, d0
    mov     DMAC_CLOSE, d1
    call    DspWrite, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# DspReadback(d0 = first address, d1 = words, a0 = out, a1 = DMAC_RB_PM or DMAC_RB_DM) -> d0 = words read. The
# pass-7 readback: channel 8, slave mode, transmit: flush; IIEP0 = address, IMEP0 = 1, CEP0 = words; DMAC0 = mode.
# Per word: read DMAC0 until FS says EPB0 holds data (FS_POLLS at most: a read of an empty EPB0 would hold the
# bus), latch EPB0, read 3 (PM) or 2 (DM) halves, least significant first. Then flush, and DMAC0's control bits as
# found. DV_RBFAIL = 1 when it stopped early.
DspReadback:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a3
    mov     d0, (0x14, sp)
    mov     d1, (0x18, sp)
    mov     a1, (0x1C, sp)
    mov     1, d0
    mov     d0, (DV_RBFAIL)
    clr     d2                      # words read
    call    DeadFlag, 0, 0
    cmp     0, d0
    bne     drb_skip
    mov     IOP_DMAC0, d0
    mov     DMAC_FLSH, d1
    call    DspWrite, 0, 0
    mov     IOP_IIEP0, d0
    mov     (0x14, sp), d1
    call    DspWrite, 0, 0
    mov     IOP_IMEP0, d0
    mov     1, d1
    call    DspWrite, 0, 0
    mov     IOP_CEP0, d0
    mov     (0x18, sp), d1
    call    DspWrite, 0, 0
    mov     IOP_DMAC0, d0
    mov     (0x1C, sp), d1
    call    DspWrite, 0, 0
drb_word:
    mov     (0x18, sp), d0
    cmp     d0, d2
    bcc     drb_done
    clr     d3                      # polls
drb_poll:
    call    DeadFlag, 0, 0
    cmp     0, d0
    bne     drb_end
    mov     IOP_DMAC0, d0
    call    DspRead, 0, 0
    mov     0xC000, d1              # FS: 00 empty, 10 partly full, 11 full
    and     d1, d0
    bne     drb_data
    inc     d3
    cmp     FS_POLLS, d3
    bcs     drb_poll
    bra     drb_end
drb_data:
    mov     IOP_EPB0, d0
    call    DspIndex, 0, 0
    call    DspData, 0, 0
    movhu   d0, (a3)
    add     2, a3
    call    DspData, 0, 0
    movhu   d0, (a3)
    add     2, a3
    mov     (0x1C, sp), d0
    btst    0x20, d0
    beq     drb_next
    call    DspData, 0, 0
    movhu   d0, (a3)
    add     2, a3
drb_next:
    inc     d2
    bra     drb_word
drb_done:
    clr     d0
    mov     d0, (DV_RBFAIL)
drb_end:
    mov     IOP_DMAC0, d0
    mov     DMAC_FLSH, d1
    call    DspWrite, 0, 0
    mov     IOP_DMAC0, d0
    mov     (DV_DMAC0), d1
    mov     DMAC_KEEP, d3
    and     d3, d1
    call    DspWrite, 0, 0
drb_skip:
    mov     d2, d0
    ret     [d2,d3,a2,a3], 0x30

# Verify(d0 = DSP address, d1 = words, a1 = DMAC_RB_PM or DMAC_RB_DM, a2 = what was uploaded) -> d0 = 0 when the
# readback equals it, else 1. RB_WORDS words at a time (DV_RBBUF).
Verify:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, (0x14, sp)          # the next address
    mov     d1, (0x18, sp)          # words left
    mov     a1, (0x1C, sp)
vf_chunk:
    mov     (0x18, sp), d3
    cmp     0, d3
    beq     vf_good
    cmp     RB_WORDS, d3
    bls     vf_n
    mov     RB_WORDS, d3
vf_n:
    mov     (0x14, sp), d0
    mov     d3, d1
    mov     DV_RBBUF, a0
    mov     (0x1C, sp), a1
    call    DspReadback, 0, 0
    cmp     d3, d0
    bne     vf_bad
    mov     d3, d2                  # halves to compare: two a word, three for PM
    add     d2, d2
    mov     (0x1C, sp), d0
    cmp     DMAC_RB_PM, d0
    bne     vf_cmp0
    add     d3, d2
vf_cmp0:
    mov     DV_RBBUF, a0
vf_cmp:
    cmp     0, d2
    beq     vf_more
    movhu   (a0), d0
    movhu   (a2), d1
    cmp     d1, d0
    bne     vf_bad
    add     2, a0
    add     2, a2
    add     -1, d2
    bra     vf_cmp
vf_more:
    mov     (0x14, sp), d0
    add     d3, d0
    mov     d0, (0x14, sp)
    mov     (0x18, sp), d0
    sub     d3, d0
    mov     d0, (0x18, sp)
    bra     vf_chunk
vf_good:
    clr     d0
    bra     vf_ret
vf_bad:
    mov     1, d0
vf_ret:
    ret     [d2,d3,a2,a3], 0x30

# ReadSlot(d0 = the slot's PM address) -> d0 = 0, d1 = its low 32 bits, a1 = its high 16; or d0 = 1 (the
# readback failed).
ReadSlot:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     1, d1
    mov     DV_RBBUF, a0
    mov     DMAC_RB_PM, a1
    call    DspReadback, 0, 0
    cmp     1, d0
    bne     rs_bad
    mov     DV_RBBUF, a0
    movhu   (4, a0), d0
    mov     d0, a1
    movhu   (2, a0), d1
    asl     16, d1
    movhu   (a0), d0
    or      d0, d1
    clr     d0
    bra     rs_ret
rs_bad:
    mov     1, d0
rs_ret:
    ret     [d2,d3,a2,a3], 0x30

# PokeSlot(a0 = the slot's PM address, d0 = bits 47-32, d1 = bits 31-0): one PM word, as the firmware pokes a slot.
PokeSlot:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, d2
    mov     sp, a0                  # the three halves at (0x14, sp), least significant first
    add     0x14, a0
    movhu   d1, (a0)
    lsr     16, d1
    movhu   d1, (2, a0)
    movhu   d0, (4, a0)
    mov     d2, d0
    mov     1, d1
    mov     DMAC_UP_PM, a1
    call    DspUpload, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# SetSlot(a0 = the slot's PM address, d0 = the target) -> d0 = 0 when it now reads back "CALL (target) (DB)", else 1.
SetSlot:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a2
    mov     0x04000000, d2
    or      d0, d2                  # the low 32 bits
    mov     DSP_CALLDB_HI, d0
    mov     d2, d1
    call    PokeSlot, 0, 0
    mov     a2, d0
    call    ReadSlot, 0, 0
    cmp     0, d0
    bne     sst_bad
    cmp     DSP_CALLDB_HI, a1
    bne     sst_bad
    cmp     d2, d1
    bne     sst_bad
    clr     d0
    bra     sst_ret
sst_bad:
    mov     1, d0
sst_ret:
    ret     [d2,d3,a2,a3], 0x30

# FoundSlot(d0 = the slot's PM address, d1 = the firmware's own target for it) -> d0 = 0 and the slot as the
# firmware keeps it in d1 (low 32) / a1 (high 16), or d0 = 1 (not readable, or not the kernel's form). A slot that
# points into our PM area already (an earlier program of ours that never put it back: DSPTONE's or this one's)
# is put back to the firmware's own target first, so that nothing runs our area while the mixer is uploaded.
FoundSlot:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, a2                  # a2 = the slot
    mov     d1, d3                  # d3 = the firmware's target for it
    call    ReadSlot, 0, 0
    cmp     0, d0
    bne     fsl_bad
    cmp     DSP_CALLDB_HI, a1
    bne     fsl_bad
    mov     d1, d2                  # bits 31-16 must be 0x0400 (DB, no condition), the target in 0x8000-0x8FFF
    lsr     16, d2
    cmp     0x0400, d2
    bne     fsl_bad
    mov     d1, d2
    exthu   d2
    cmp     0x8000, d2
    bcs     fsl_bad
    cmp     0x8FFF, d2
    bhi     fsl_bad
    cmp     DSP_OURS_LO, d2
    bcs     fsl_ok
    cmp     DSP_OURS_HI, d2
    bcc     fsl_ok
    mov     a2, a0                  # ours: the firmware's target back first
    mov     d3, d0
    call    SetSlot, 0, 0
    cmp     0, d0
    bne     fsl_bad
    mov     0x04000000, d1
    or      d3, d1
    mov     DSP_CALLDB_HI, a1
fsl_ok:
    clr     d0
    bra     fsl_ret
fsl_bad:
    mov     1, d0
fsl_ret:
    ret     [d2,d3,a2,a3], 0x30

# Now -> d0 = the kernel's milliseconds. Leaf.
DspNow:
    mov     (FWV_KERNEL_MS), a0
    mov     (a0), d0
    rets

# ==============================================================================================================
# The converter's side.


# ZeroMsgr: MSGR4-MSGR7 = 0, as the kernel leaves them at boot.
ZeroMsgr:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     DSP_MSGR4, d2
zm_one:
    mov     d2, d0
    clr     d1
    call    DspWrite, 0, 0
    inc     d2
    cmp     DSP_MSGR7, d2
    bls     zm_one
    ret     [d2,d3,a2,a3], 0x30

# DspGiveUp(d0 = DF_* reason): the slots back if they are ours, the message registers and channel 8 as found;
# the DSP is not used again in this run.
DspGiveUp:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (DV_FAIL), d1           # the first reason is the one kept
    cmp     0, d1
    bne     dg_keep
    mov     d0, (DV_FAIL)
dg_keep:
    call    DspUnhook, 0, 0
    clr     d0
    mov     d0, (DV_READY)
    mov     DS_OFF, d0
    mov     d0, (DV_STATE)
    ret     [d2,d3,a2,a3], 0x30

# DspHook -> d0 = 0 when unit 0's slot (and unit 9's, DV_U9) now call the mixer, read back; else 1, and whatever
# was hooked is put back.
DspHook:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    call    DeadFlag, 0, 0
    cmp     0, d0
    bne     dh_bad
    mov     (DV_HOOKED), d0         # from here on every way out puts the slots back
    or      1, d0
    mov     d0, (DV_HOOKED)
    mov     DSP_SLOT0, a0
    mov     DSP_ENTRY0, d0
    call    SetSlot, 0, 0
    cmp     0, d0
    bne     dh_bad
    mov     (DV_U9), d0
    cmp     0, d0
    beq     dh_ok
    mov     (DV_HOOKED), d0
    or      2, d0
    mov     d0, (DV_HOOKED)
    mov     DSP_SLOT9, a0
    mov     DSP_ENTRY9, d0
    call    SetSlot, 0, 0
    cmp     0, d0
    bne     dh_bad
dh_ok:
    clr     d0
    bra     dh_ret
dh_bad:
    call    DspUnhook, 0, 0
    mov     1, d0
dh_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspUnhook: the voices silent, the slots as found (each read back), MSGR4-MSGR7 = 0, channel 8's registers as
# found. Does nothing when no slot is ours. -> d0 = 0 when everything read back right.
DspUnhook:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    clr     d3                      # problems
    mov     (DV_HOOKED), d0
    cmp     0, d0
    beq     du_ret0
    call    DeadFlag, 0, 0
    cmp     0, d0
    bne     du_dead
    mov     (DV_HOOKED), d0
    btst    2, d0
    beq     du_u0
    mov     DSP_SLOT9, a0           # unit 9 first: it reads the mix unit 0 makes
    mov     (DV_SLOT9_LO), d0
    exthu   d0
    call    SetSlot, 0, 0
    or      d0, d3
    cmp     0, d0
    bne     du_u0
    mov     (DV_HOOKED), d0
    and     0xFFFFFFFD, d0
    mov     d0, (DV_HOOKED)
du_u0:
    mov     (DV_HOOKED), d0
    btst    1, d0
    beq     du_regs
    mov     DSP_SLOT0, a0
    mov     (DV_SLOT0_LO), d0
    exthu   d0
    call    SetSlot, 0, 0
    or      d0, d3
    cmp     0, d0
    bne     du_regs
    mov     (DV_HOOKED), d0
    and     0xFFFFFFFE, d0
    mov     d0, (DV_HOOKED)
du_regs:
    call    ZeroMsgr, 0, 0
    mov     IOP_IIEP0, d0           # channel 8's registers as found
    mov     (DV_IIEP0), d1
    call    DspWrite, 0, 0
    mov     IOP_IMEP0, d0
    mov     (DV_IMEP0), d1
    call    DspWrite, 0, 0
    mov     IOP_CEP0, d0
    mov     (DV_CEP0), d1
    call    DspWrite, 0, 0
    mov     IOP_DMAC0, d0
    mov     (DV_DMAC0), d1
    mov     DMAC_KEEP, d2
    and     d2, d1
    call    DspWrite, 0, 0
    clr     d0
    mov     d0, (DV_PEND)
    mov     d3, d0
    bra     du_ret
du_dead:                            # nothing reaches the DSP any more: the firmware reloads it at power-on
    clr     d0
    mov     d0, (DV_HOOKED)
    mov     1, d0
    bra     du_ret
du_ret0:
    clr     d0
du_ret:
    ret     [d2,d3,a2,a3], 0x30


WriteHead:  # the guarded data write's first 8 bytes, less the dead flag's address (941 0x48404EF8)
    .byte   0xf8, 0x97, 0x1c, 0xf8, 0x93, 0x18, 0xfc, 0xa8
    .align  2
sig_dspwait:    # the handshake wait (941 0x48404D25); masked: the downloaded and dead flag operands (DSPTONE)
    .word   0xcf, 0x80, 0xf8, 0xfe, 0xfc, 0xfc, 0xcc, 0xff, 0xff, 0x03, 0x00, 0x42, 0x00, 0xfc
    .word   0xa8, 0x100, 0x100, 0x100, 0x100, 0xa0, 0x00, 0xc9, 0x04, 0xca, 0x32, 0x80, 0x01, 0xfc
    .word   0x83, 0x5c, 0x01, 0x00, 0x34, 0xca, 0x1b, 0x58, 0x00, 0x8a, 0x01, 0x81, 0xf1, 0x09
    .word   0x46, 0x00, 0xa0, 0x00, 0xc8, 0x04, 0xca, 0x0c, 0x80, 0xff, 0xfc, 0x82, 0x100, 0x100
    .word   0x100, 0x100, 0xca, 0x0f, 0xfc, 0xac, 0x5c, 0x01, 0x00, 0x34, 0xf8, 0xec, 0x10, 0xc9
    .word   0x04, 0xca, 0xdc, 0xdf, 0x80, 0x08, 0xFFFF
sig_dspregs:    # 2x16 register write, its twin, 2x16 read, its twin (941 0x484052DD); masked: call displacements
    .word   0xf8, 0xfe, 0xf0, 0xf8, 0x97, 0x1c, 0x42, 0x18, 0xf8, 0xbc, 0x18, 0x1c, 0xcd, 0x100
    .word   0x100, 0x00, 0x10, 0xf8, 0xbc, 0x1c, 0xf8, 0xbd, 0x20, 0xcd, 0x100, 0x100, 0x00, 0x10
    .word   0xdf, 0x00, 0x10, 0xf8, 0xfe, 0xf0, 0xf8, 0x97, 0x1c, 0x42, 0x18, 0xf8, 0xbc, 0x18
    .word   0x1c, 0xcd, 0x100, 0x100, 0x00, 0x10, 0xf8, 0xbc, 0x1c, 0xf8, 0xbd, 0x20, 0xcd, 0x100
    .word   0x100, 0x00, 0x10, 0xdf, 0x00, 0x10, 0xf8, 0xfe, 0xe8, 0x42, 0x20, 0xf8, 0xbc, 0x20
    .word   0x1c, 0xcd, 0x100, 0x100, 0x00, 0x10, 0x2c, 0xc4, 0x09, 0xcd, 0x100, 0x100, 0x00, 0x0c
    .word   0x18, 0x42, 0x0c, 0x2c, 0xc4, 0x09, 0xcd, 0x100, 0x100, 0x00, 0x0c, 0x18, 0x42, 0x10
    .word   0x58, 0x10, 0xf8, 0xc0, 0x10, 0x59, 0x0c, 0xe4, 0x42, 0x14, 0xdf, 0x00, 0x18, 0xf8
    .word   0xfe, 0xe8, 0x42, 0x20, 0xf8, 0xbc, 0x20, 0x1c, 0xcd, 0x100, 0x100, 0x00, 0x10, 0x2c
    .word   0xc4, 0x09, 0xcd, 0x100, 0x100, 0x00, 0x14, 0x18, 0x42, 0x0c, 0x2c, 0xc4, 0x09, 0xcd
    .word   0x100, 0x100, 0x00, 0x14, 0x18, 0x42, 0x10, 0x58, 0x10, 0xf8, 0xc0, 0x10, 0x59, 0x0c
    .word   0xe4, 0x42, 0x14, 0xdf, 0x00, 0x18, 0xFFFF

# ==============================================================================================================
# KN7FM's own: the FM engine's load, its commands, its patch.

# DspPrepare -> d0 = 0: the FM engine (tools/fm_dsp.py) is loaded, read back and hooked (DS_READY); else 1,
# DV_FAIL says why, and nothing of the firmware's is left changed (DS_OFF). From the converter, before the task.
# The checks are Another World's (DSPTONE's): the routines found, the DSP alive and as the firmware leaves it,
# both CALL slots in the kernel's form (read back and kept).
DspPrepare:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     DF_NOTFOUND, d2
    mov     (DV_FOUND), d0
    cmp     DFB_ALL, d0
    bne     dpr_fail
    mov     DF_DEAD, d2
    call    DeadFlag, 0, 0
    cmp     0, d0
    bne     dpr_fail
    mov     DF_NOTDOWN, d2
    call    DownFlag, 0, 0
    cmp     0, d0
    beq     dpr_fail
    mov     IOP_SYSCON, d0
    call    DspRead, 0, 0
    mov     d0, (DV_SYSCON)
    mov     IOP_DMAC0, d0
    call    DspRead, 0, 0
    mov     d0, (DV_DMAC0)
    mov     IOP_IIEP0, d0
    call    DspRead, 0, 0
    mov     d0, (DV_IIEP0)
    mov     IOP_IMEP0, d0
    call    DspRead, 0, 0
    mov     d0, (DV_IMEP0)
    mov     IOP_CEP0, d0
    call    DspRead, 0, 0
    mov     d0, (DV_CEP0)
    mov     IOP_DMASTAT, d0
    call    DspRead, 0, 0
    mov     d0, (DV_DMASTAT)
    mov     DF_STATE, d2
    mov     (DV_SYSCON), d0         # SYSCON: HBW = 01 (16-bit host bus), HMSWF = 0 (low half first)
    and     0x70, d0
    cmp     0x10, d0
    bne     dpr_fail
    mov     (DV_DMAC0), d0          # DMAC0: DEN, CHEN clear
    and     3, d0
    bne     dpr_fail
    mov     (DV_DMASTAT), d0        # DMASTAT: channel 8 neither active (bit 6) nor chaining (bit 16)
    mov     0x10040, d1
    and     d1, d0
    bne     dpr_fail
    mov     DF_SLOT, d2             # the slots as the firmware keeps them
    mov     DSP_SLOT0, d0
    mov     DSP_DEF0, d1
    call    FoundSlot, 0, 0
    cmp     0, d0
    bne     dpr_fail
    mov     a1, (DV_SLOT0_HI)
    mov     d1, (DV_SLOT0_LO)
    clr     d0                      # unit 9: hooked too only when it is the kernel's form
    mov     d0, (DV_U9)
    mov     DSP_SLOT9, d0
    mov     DSP_DEF9, d1
    call    FoundSlot, 0, 0
    cmp     0, d0
    bne     dpr_load
    mov     a1, (DV_SLOT9_HI)
    mov     d1, (DV_SLOT9_LO)
    mov     1, d0
    mov     d0, (DV_U9)
dpr_load:
    mov     upload_list, a2         # the program, the tables, the variables, the voices
dpr_up:
    mov     (a2), d0                # DSP address (0 = end of the list)
    cmp     0, d0
    beq     dpr_patch
    mov     (4, a2), d1             # words
    mov     (8, a2), a0             # where they are
    mov     (12, a2), a1            # DMAC_UP_PM / DMAC_UP_DM
    call    DspUpload, 0, 0
    add     16, a2
    bra     dpr_up
dpr_patch:
    call    PatchImage, 0, 0        # the current voice's words (synth.s), into PIMG
    mov     DSP_PATCH, d0
    mov     DSP_PATCH_WORDS, d1
    mov     PIMG, a0
    mov     DMAC_UP_DM, a1
    call    DspUpload, 0, 0
    call    PimgSent, 0, 0
    call    CurAlg, 0, 0            # its algorithm's kernel, into buffer 0 (TRAMP points there as loaded)
    mov     d0, (V_ALG)
    clr     d1
    mov     d1, (V_ALGBUF)
    call    KernelLoad, 0, 0
    call    ZeroMsgr, 0, 0
    mov     DF_DEAD, d2
    call    DeadFlag, 0, 0
    cmp     0, d0
    bne     dpr_fail
    mov     DF_VERIFY, d2           # all of it read back and compared
    mov     upload_list, a3
dpr_ver:
    mov     (a3), d0
    cmp     0, d0
    beq     dpr_ver2
    mov     (4, a3), d1
    mov     (8, a3), a2
    mov     (12, a3), d0
    or      4, d0                   # DMAC_UP_x -> DMAC_RB_x (TRAN)
    mov     d0, a1
    mov     (a3), d0
    mov     d0, (DV_VERADDR)        # (what was being read back, kept for a failure's report)
    call    Verify, 0, 0
    cmp     0, d0
    bne     dpr_fail
    add     16, a3
    bra     dpr_ver
dpr_ver2:
    mov     DSP_PATCH, d0
    mov     DSP_PATCH_WORDS, d1
    mov     DMAC_RB_DM, a1
    mov     d0, (DV_VERADDR)
    mov     PSENT, a2
    call    Verify, 0, 0
    cmp     0, d0
    bne     dpr_fail
    mov     (V_ALG), d0
    call    AlgWords, 0, 0          # a0 = its words, d1 = how many
    mov     a0, a2
    mov     DSP_ALG0, d0
    mov     DMAC_RB_PM, a1
    mov     d0, (DV_VERADDR)
    call    Verify, 0, 0
    cmp     0, d0
    bne     dpr_fail
    call    DspSetVol, 0, 0         # the editor's volume (the variables came with full volume)
    call    DspSetNV, 0, 0          # and its voices (they came with all eight)
    mov     DSP_MSGR7, d0           # the heartbeat before the hook
    call    DspRead, 0, 0
    mov     d0, (V_HB0)
    call    DspNow, 0, 0
    mov     d0, (V_HBMS)
    call    DspHook, 0, 0           # both slots: from here on every way out puts them back
    mov     DF_HOOK, d2
    cmp     0, d0
    bne     dpr_fail
    mov     1, d0
    mov     d0, (DV_READY)
    mov     DS_READY, d0
    mov     d0, (DV_STATE)
    clr     d0
    bra     dpr_ret
dpr_fail:
    mov     d2, d0
    call    DspGiveUp, 0, 0
    mov     1, d0
dpr_ret:
    ret     [d2,d3,a2,a3], 0x30

# AlgWords(d0 = algorithm 0-31) -> a0 = its kernel's PM words (three halves each), d1 = how many. Leaf.
AlgWords:
    asl     3, d0
    mov     DspAlgTab, a0
    add     d0, a0
    mov     (4, a0), d1
    mov     (a0), d0
    mov     DspAlgs, a0
    add     d0, a0
    rets

# KernelLoad(d0 = algorithm, d1 = buffer 0 / 1): its kernel into that PM buffer.
KernelLoad:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d1, d2
    call    AlgWords, 0, 0
    mov     DSP_ALG0, d0
    cmp     0, d2
    beq     kl_go
    mov     DSP_ALG1, d0
kl_go:
    mov     DMAC_UP_PM, a1
    call    DspUpload, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# DspSetAlg(d0 = algorithm): when it is not the one running, its kernel into the other buffer, then TRAMP (one
# PM word, as the firmware pokes a slot) pointed at it: the DSP changes kernel between two frames.
DspSetAlg:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (V_ALG), d1
    cmp     d1, d0
    beq     dsa_ret
    mov     d0, d2
    mov     (V_ALGBUF), d3
    xor     1, d3
    mov     d3, d1
    call    KernelLoad, 0, 0
    mov     DSP_ALG0, d1
    cmp     0, d3
    beq     dsa_poke
    mov     DSP_ALG1, d1
dsa_poke:
    mov     DSP_TRAMP, a0
    mov     DSP_JUMP_HI, d0
    call    PokeSlot, 0, 0
    mov     d2, (V_ALG)
    mov     d3, (V_ALGBUF)
dsa_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspSendPatch: the current voice's DSP words (PatchImage), the ones that differ from what the DSP has (PSENT)
# uploaded as one block; then its algorithm's kernel when that changed (DspSetAlg). From the task.
DspSendPatch:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    call    PatchImage, 0, 0
    clr     d2                      # d2 = the first word that differs
    mov     -1, d3                  # d3 = the last
    mov     PIMG, a0
    mov     PSENT, a1
dsp_cmp:
    mov     (a0), d0
    mov     (a1), d1
    cmp     d1, d0
    beq     dsp_same
    cmp     0, d3
    bge     dsp_last
    mov     d2, (0x14, sp)          # the first
dsp_last:
    mov     d2, d3
dsp_same:
    add     4, a0
    add     4, a1
    inc     d2
    cmp     DSP_PATCH_WORDS, d2
    bcs     dsp_cmp
    cmp     0, d3
    blt     dsp_alg
    mov     (0x14, sp), d2
    mov     d3, d1                  # words: last - first + 1
    sub     d2, d1
    inc     d1
    mov     d2, d0
    asl     2, d2
    mov     PIMG, a0
    add     d2, a0
    add     DSP_PATCH, d0
    mov     DMAC_UP_DM, a1
    call    DspUpload, 0, 0
    call    PimgSent, 0, 0
dsp_alg:
    call    CurAlg, 0, 0
    call    DspSetAlg, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# PimgSent: PSENT = PIMG. Leaf.
PimgSent:
    mov     PIMG, a0
    mov     PSENT, a1
    mov     DSP_PATCH_WORDS, d1
pis_cpy:
    mov     (a0), d0
    mov     d0, (a1)
    add     4, a0
    add     4, a1
    add     -1, d1
    bne     pis_cpy
    rets

# DspSetVol: the master volume word (T_VOLUME[V_VOL]) into the engine's VOL.
DspSetVol:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (V_VOL), d0
    asl     2, d0
    mov     T_VOLUME, a0
    add     d0, a0
    mov     (a0), d0
    mov     d0, (V_VOLWORD)
    mov     DSP_G_VOL, d0
    mov     1, d1
    mov     V_VOLWORD, a0
    mov     DMAC_UP_DM, a1
    call    DspUpload, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# DspSetNV: V_NV into the engine's NV, the voices its kernel plays.
DspSetNV:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (V_NV), d0
    mov     d0, (V_NVWORD)
    mov     DSP_G_NV, d0
    mov     1, d1
    mov     V_NVWORD, a0
    mov     DMAC_UP_DM, a1
    call    DspUpload, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# DspCmdWait -> d0 = 0 once MSGR6 reads 0 (the engine has taken the last command, which it does at its next
# frame), within CMD_POLLS reads; else 1 (V_BUSY + 1).
DspCmdWait:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    clr     d2
dcw_poll:
    mov     DSP_MSGR6, d0
    call    DspRead, 0, 0
    cmp     0, d0
    beq     dcw_ok
    inc     d2
    cmp     CMD_POLLS, d2
    bcs     dcw_poll
    mov     (V_BUSY), d0
    inc     d0
    mov     d0, (V_BUSY)
    mov     1, d0
    bra     dcw_ret
dcw_ok:
    clr     d0
dcw_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspKeyOn(d0 = voice): the key-on's words (STAGEBUF, synth.s NoteWords) into the engine's STAGE, then MSGR6 =
# its key-on bit. Nothing when the engine is not running or still busy (the note is lost).
DspKeyOn:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, d2
    mov     (DV_HOOKED), d0
    btst    1, d0
    beq     dko_ret
    call    DspCmdWait, 0, 0
    cmp     0, d0
    bne     dko_ret
    mov     DSP_STAGE, d0
    mov     DSP_STAGE_WORDS, d1
    mov     STAGEBUF, a0
    mov     DMAC_UP_DM, a1
    call    DspUpload, 0, 0
    mov     0x10000, d1             # its key-on bit, 16 + voice: in the high half (tools/fm_dsp.py)
    asl     d2, d1
    mov     DSP_MSGR6, d0
    call    DspWrite, 0, 0
dko_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspKeyOff(d0 = a mask of voices): MSGR6 = their key-off bits (24 + voice).
DspKeyOff:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, d2
    and     0xFF, d2
    beq     dkf_ret
    mov     (DV_HOOKED), d0
    btst    1, d0
    beq     dkf_ret
    call    DspCmdWait, 0, 0
    cmp     0, d0
    bne     dkf_ret
    mov     d2, d1
    asl     24, d1
    mov     DSP_MSGR6, d0
    call    DspWrite, 0, 0
dkf_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspRate: every second, the DSP's frames per second from its heartbeat (MSGR7) -> V_RATE.
DspRate:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    call    DspNow, 0, 0
    mov     d0, d2
    mov     (V_HBMS), d1
    sub     d1, d0
    cmp     1000, d0
    bcs     drt_ret
    mov     d0, d3                  # d3 = ms since
    mov     DSP_MSGR7, d0
    call    DspRead, 0, 0
    mov     (V_HB0), d1
    mov     d0, (V_HB0)
    sub     d1, d0                  # frames since
    mov     1000, d1
    mulu    d1, d0
    clr     d1
    mov     d1, mdr
    divu    d3, d0
    mov     d0, (V_RATE)
    mov     d2, (V_HBMS)
    cmp     RATE_MIN, d0            # frames missed: the voices take more than a frame. One voice less
    bcc     drt_dirty
    mov     (V_NV), d0
    cmp     1, d0
    bls     drt_dirty
    add     -1, d0
    mov     d0, (V_NV)
    call    AllNotesOff, 0, 0
    call    DspSetNV, 0, 0
    mov     msg_busy, d0
    call    PostMsg, 0, 0
    mov     (V_COLDIRTY), d0
    or      8, d0                   # (the BANK page's POLY column, the fourth, when shown)
    mov     d0, (V_COLDIRTY)
drt_dirty:
    mov     (V_DIRTY), d0
    or      D_TITLE, d0
    mov     d0, (V_DIRTY)
drt_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspWatch -> d0 = 1 when the firmware has stopped calling the converter for WATCH_MS while the slots are ours (it
# left STYLE CONVERT some other way than our 0xFE): then the voices are released and the slots put back, from the
# task. Else 0. (Another World's.)
DspWatch:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    call    DspNow, 0, 0
    mov     d0, d2
    mov     (V_CALLS), d0
    mov     (DV_LASTCALLS), d1
    cmp     d1, d0
    beq     dw_same
    mov     d0, (DV_LASTCALLS)
    mov     d2, (DV_LASTMS)
    bra     dw_no
dw_same:
    mov     (DV_HOOKED), d0
    cmp     0, d0
    beq     dw_no
    mov     d2, d0
    mov     (DV_LASTMS), d1
    sub     d1, d0
    cmp     WATCH_MS, d0
    bcs     dw_no
    call    DspQuit, 0, 0
    mov     DF_WATCH, d0
    mov     d0, (DV_FAIL)
    mov     1, d0
    bra     dw_ret
dw_no:
    clr     d0
dw_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspQuit: every voice released, then the slots as the firmware had them (read back), once.
DspQuit:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (DV_STATE), d0
    cmp     DS_READY, d0
    bne     dq_ret
    mov     0xFF, d0
    call    DspKeyOff, 0, 0
    call    DspUnhook, 0, 0
    clr     d0
    mov     d0, (DV_READY)
    mov     DS_OFF, d0
    mov     d0, (DV_STATE)
dq_ret:
    ret     [d2,d3,a2,a3], 0x30

# DspNow -> d0 = the kernel's milliseconds. Leaf. (DspNow is Another World's; FWV_KERNEL_MS is always found:
# the search's step 1 must succeed.)

# ---- data ------------------------------------------------------------------------------------------------------
msg_busy:       .asciz  "DSP too busy: a voice less"
    .align  4
upload_list:    # what DspPrepare uploads and reads back: DSP address, words, where, DMAC mode
    .long   DSP_CODE_ADDR, DSP_CODE_WORDS, DspCode, DMAC_UP_PM
    .long   DSP_CODE2_ADDR, DSP_CODE2_WORDS, DspCode2, DMAC_UP_PM
    .long   DSP_SINE, DSP_SINE_WORDS, DspSine, DMAC_UP_DM
    .long   DSP_STATICS, 100, DspStatics, DMAC_UP_DM
    .long   DSP_GLOBALS, DSP_NGLOBALS, DspGlobals, DMAC_UP_DM
    .long   DSP_VOICES + 0 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   DSP_VOICES + 1 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   DSP_VOICES + 2 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   DSP_VOICES + 3 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   DSP_VOICES + 4 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   DSP_VOICES + 5 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   DSP_VOICES + 6 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   DSP_VOICES + 7 * DSP_VOICE_LEN, DSP_VOICE_LEN, DspVoice, DMAC_UP_DM
    .long   0

.include "dsp_data.inc"             # generated by tools/fm_dsp.py: DspCode, DspCode2, DspAlgTab, DspAlgs, DspSine, ...
    .align  4
