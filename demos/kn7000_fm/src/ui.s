# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# ui.s -- KN7FM's screen and panel: the editor (README.md, "Editing on the instrument's screen").
#
#   +------------------------------------------------------------------------------+
#   | KN7FM  01 E.PIANO    ALG 5   OP1 A                         44100/s           |  title bar
#   | OP1  |   the algorithm's diagram     |   the envelope of the operator     | OP5 |  side buttons
#   | OP2  |   (the operator shown lit)    |   (or the pitch envelope)          | OP6 |  LCDL 1-4,
#   | OP3  |                               |                                    |VOICE|  LCDR 1-4
#   | OP4  |                               |                                    | BANK|
#   | R1  | R2  | R3  | ... 16 columns of 40 pixels: a parameter's name and value  |      strip
#   +------------------------------------------------------------------------------+
#              MUTE UP / DOWN 1..16, under the columns: each column's value up / down
#
# An operator's key shows its page (A: envelope, level, frequency; pressed again, B: keyboard scaling); VOICE
# (A: algorithm, feedback, LFO; again, B: the pitch envelope); BANK: the bank's file, LOAD, SAVE and the voice's
# name. PAGE UP / DOWN choose the voice of the bank. Every change goes to the DSP at once (synth.s, dsp.s); what
# a note's key-on computes (levels, frequencies, scaling) applies from the next note, as on the DX7.
#
# The drawing primitives are DSPTONE's (technics-roms-disasm kn7000/tools/dsptone/dsptone.s: FillRect, DrawText,
# DrawChar; Font8x13 from the loader). Everything is drawn from the task, a piece at a time between note polls.

# ---- the buttons -----------------------------------------------------------------------------------------------
# Buttons: V_BTN = the buttons down (B_* bits, ButtonTab), V_PRESSED = those that went down since the last call;
# V_MUTENEW = the MUTE UP / DOWN presses (bit 2c up, 2c + 1 down, column c), with auto-repeat while one is held.
Buttons:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    clr     d2
    mov     (FWV_PANEL), a2
    cmp     0, a2
    beq     bt_ret
    mov     ButtonTab, a3
    clr     d3
bt_one:
    movbu   (a3), d0
    mov     a2, a0
    add     d0, a0
    movbu   (a0), d0
    movbu   (1, a3), d1
    and     d1, d0
    beq     bt_up
    mov     1, d0
    asl     d3, d0
    or      d0, d2
bt_up:
    add     2, a3
    inc     d3
    cmp     NBUTTONS, d3
    bcs     bt_one
    mov     (V_BTN), d0
    mov     d2, (V_BTN)
    not     d0
    and     d2, d0
    mov     d0, (V_PRESSED)
    clr     d2                      # the MUTE grid: 16 columns x (up, down)
    mov     MuteTab, a3
    clr     d3
bt_mute:
    movbu   (a3), d0                # the column's byte, and its up bit (down is the next)
    mov     a2, a0
    add     d0, a0
    movbu   (a0), d0
    movbu   (1, a3), d1
    mov     d1, a0
    and     d0, d1
    beq     bt_mdn
    mov     1, d1
    asl     d3, d1
    or      d1, d2
bt_mdn:
    mov     a0, d1
    add     d1, d1
    and     d0, d1
    beq     bt_mnext
    mov     2, d1
    asl     d3, d1
    or      d1, d2
bt_mnext:
    add     2, a3
    add     2, d3
    cmp     32, d3
    bcs     bt_mute
    mov     (V_MUTEDN), d0          # new presses
    mov     d2, (V_MUTEDN)
    mov     d0, d1
    not     d1
    and     d2, d1
    mov     d1, (V_MUTENEW)
    cmp     0, d1
    beq     bt_rep
    call    UiNow, 0, 0             # a new press: the repeat starts later
    mov     d0, (V_REPT0)
    clr     d0
    mov     d0, (V_REPN)
    bra     bt_ret
bt_rep:
    cmp     0, d2                   # held: repeat
    beq     bt_ret
    call    UiNow, 0, 0
    mov     (V_REPT0), d1
    sub     d1, d0
    mov     (V_REPN), d1
    cmp     0, d1
    bne     bt_rep2
    cmp     REP_DELAY, d0
    bcs     bt_ret
    bra     bt_fire
bt_rep2:
    cmp     REP_FAST, d1
    bcc     bt_fast
    cmp     REP_MS, d0
    bcs     bt_ret
    bra     bt_fire
bt_fast:
    cmp     REP_MS2, d0
    bcs     bt_ret
bt_fire:
    mov     d2, (V_MUTENEW)
    call    UiNow, 0, 0
    mov     d0, (V_REPT0)
    mov     (V_REPN), d0
    inc     d0
    mov     d0, (V_REPN)
bt_ret:
    ret     [d2,d3,a2,a3], 0x30

# UiNow -> d0 = the kernel's milliseconds. Leaf.
UiNow:
    mov     (FWV_KERNEL_MS), a0
    mov     (a0), d0
    rets

# ---- the editor ------------------------------------------------------------------------------------------------
# A page is 16 columns of PARAM records (8 bytes): kind (K_*), the byte (an operator's 0-20, or the VCED's own
# offset), the maximum, the display (T_*), the name (4 characters at most).
.equ K_NONE,        0
.equ K_OP,          1               # a byte of the operator shown (V_OPSEL)
.equ K_VOICE,       2               # a byte of the voice (VCED offset)
.equ K_FILE,        3               # V_FILENO
.equ K_LOAD,        4               # UP: read BANKn.SYX
.equ K_SAVE,        5               # UP twice: write it
.equ K_NAME,        6               # a character of the name (VCED offset), 32-126
.equ T_NUM,         0
.equ T_PLUS1,       1               # shown + 1 (the algorithm)
.equ T_MODE,        2               # RATIO / FIXED
.equ T_CURVE,       3               # -LN -EX +EX +LN
.equ T_WAVE,        4               # TRI SAW- SAW+ SQR SIN S/H
.equ T_ONOFF,       5
.equ T_DET,         6               # - 7
.equ T_TRN,         7               # - 24 (transpose, in semitones)
.equ T_CHAR,        8
.equ T_ACT,         9               # no value: its name only
.equ PR_KIND,       0
.equ PR_OFS,        1
.equ PR_MAX,        2
.equ PR_TYPE,       3
.equ PR_NAME,       4
.equ PR_LEN,        8

# PageTable -> a0 = the shown page's 16 records. Leaf.
PageTable:
    mov     (V_PAGE), d0
    asl     1, d0
    mov     (V_SUB), d1
    add     d1, d0
    asl     2, d0
    mov     PageTabs, a0
    add     d0, a0
    mov     (a0), a0
    rets

# ParamAddr(a1 = a record) -> a0 = the byte it edits (K_OP, K_VOICE, K_NAME), d0 = its value; a0 = 0 for the others.
# Leaf.
ParamAddr:
    movbu   (PR_KIND, a1), d0
    cmp     K_OP, d0
    beq     pa_op
    cmp     K_VOICE, d0
    beq     pa_voice
    cmp     K_NAME, d0
    beq     pa_voice
    mov     0, a0
    clr     d0
    rets
pa_op:
    mov     (V_OPSEL), d0           # OP n is the VCED's operator 6 - n: (5 - V_OPSEL) x 21
    mov     5, d1
    sub     d0, d1
    mov     21, d0
    mulu    d0, d1
    movbu   (PR_OFS, a1), d0
    add     d0, d1
    bra     pa_at
pa_voice:
    movbu   (PR_OFS, a1), d1
pa_at:
    mov     d1, a0                  # a0 = the byte's offset in the VCED
    mov     (V_PATCH), d0
    mov     VCED_LEN, d1
    mulu    d1, d0
    add     d0, a0
    add     BANK, a0
    movbu   (a0), d0
    rets

# ParamStep(d0 = column, d1 = +1 or -1): the column's value one step up or down (or its action).
ParamStep:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, d2                  # d2 = the column
    mov     d1, d3                  # d3 = the step
    call    PageTable, 0, 0
    mov     d2, d0
    asl     3, d0
    add     d0, a0
    mov     a0, a2                  # a2 = its record
    movbu   (PR_KIND, a2), d0
    cmp     K_FILE, d0
    beq     ps_file
    cmp     K_LOAD, d0
    beq     ps_load
    cmp     K_SAVE, d0
    beq     ps_save
    cmp     K_NONE, d0
    beq     ps_ret
    mov     a2, a1
    call    ParamAddr, 0, 0
    mov     a0, a3                  # a3 = the byte
    add     d3, d0
    movbu   (PR_KIND, a2), d1
    cmp     K_NAME, d1
    beq     ps_char
    cmp     0, d0
    bge     ps_lo
    clr     d0
ps_lo:
    movbu   (PR_MAX, a2), d1
    cmp     d1, d0
    ble     ps_set
    mov     d1, d0
    bra     ps_set
ps_char:
    cmp     32, d0
    bge     ps_clo
    mov     32, d0
ps_clo:
    cmp     126, d0
    ble     ps_cset
    mov     126, d0
ps_cset:
    movbu   d0, (a3)
    mov     D_TITLE, d0
    bra     ps_dirty
ps_set:
    movbu   (a3), d1
    cmp     d1, d0
    beq     ps_ret
    movbu   d0, (a3)
    mov     1, d0                   # the DSP's words
    mov     d0, (V_PDIRTY)
    mov     D_GRAPH, d0
    movbu   (PR_KIND, a2), d1
    cmp     K_VOICE, d1
    bne     ps_dirty
    movbu   (PR_OFS, a2), d1        # the algorithm: the diagram, the title, the side
    cmp     134, d1
    bne     ps_dirty
    mov     D_DIAG | D_TITLE, d0
ps_dirty:
    mov     (V_DIRTY), d1
    or      d0, d1
    mov     d1, (V_DIRTY)
    mov     1, d0
    asl     d2, d0
    mov     (V_COLDIRTY), d1
    or      d0, d1
    mov     d1, (V_COLDIRTY)
ps_ret:
    ret     [d2,d3,a2,a3], 0x30
ps_file:
    mov     (V_FILENO), d0
    add     d3, d0
    cmp     0, d0
    blt     ps_ret
    cmp     9, d0
    bgt     ps_ret
    mov     d0, (V_FILENO)
    clr     d0
    bra     ps_dirty
ps_load:
    cmp     0, d3
    ble     ps_ret
    mov     (V_FREQ), d0            # a request already waiting
    cmp     0, d0
    bne     ps_ret
    clr     d0
    mov     d0, (V_FRES)
    mov     1, d0
    mov     d0, (V_FREQ)
    mov     msg_loading, d0
    call    PostMsg, 0, 0
    bra     ps_ret
ps_save:
    cmp     0, d3
    ble     ps_ret
    mov     (V_FREQ), d0
    cmp     0, d0
    bne     ps_ret
    call    UiNow, 0, 0
    mov     (V_SAVEARM), d1
    cmp     0, d1
    beq     ps_arm
    sub     d1, d0
    cmp     ARM_MS, d0
    bcc     ps_arm2
    clr     d0                      # the second press: the bank into FILEBUF, the converter writes it
    mov     d0, (V_SAVEARM)
    call    BankToFile, 0, 0
    clr     d0
    mov     d0, (V_FRES)
    mov     2, d0
    mov     d0, (V_FREQ)
    mov     msg_saving, d0
    call    PostMsg, 0, 0
    bra     ps_ret
ps_arm2:
    call    UiNow, 0, 0
ps_arm:
    or      1, d0
    mov     d0, (V_SAVEARM)
    mov     msg_arm, d0
    call    PostMsg, 0, 0
    bra     ps_ret

# PostMsg(d0 = a string): shown in the title bar for MSG_MS.
PostMsg:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, (V_MSG)
    call    UiNow, 0, 0
    mov     d0, (V_MSGT0)
    mov     (V_DIRTY), d0
    or      D_TITLE, d0
    mov     d0, (V_DIRTY)
    ret     [d2,d3,a2,a3], 0x30

# UiPoll: the panel read (every POLL_MS), its presses acted on; the patch's words sent when it changed; a file
# request's answer taken; the title's message expired.
UiPoll:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    call    UiNow, 0, 0
    mov     d0, d2
    mov     (V_POLLMS), d1
    sub     d1, d0
    cmp     POLL_MS, d0
    bcs     up_file
    mov     d2, (V_POLLMS)
    call    Buttons, 0, 0
    mov     (V_PRESSED), d3
    clr     d2                      # the side buttons: L1-L4 OP1-OP4, R1 OP5, R2 OP6, R3 VOICE, R4 BANK
up_side:
    btst    1, d3
    beq     up_snext
    mov     d2, d0
    call    SideKey, 0, 0
up_snext:
    lsr     1, d3
    inc     d2
    cmp     8, d2
    bcs     up_side
    btst    1, d3                   # (d3 = V_PRESSED >> 8) PAGE UP
    beq     up_pgdn
    mov     1, d0
    call    PatchStep, 0, 0
up_pgdn:
    btst    2, d3
    beq     up_mute
    mov     -1, d0
    call    PatchStep, 0, 0
up_mute:
    mov     (V_MUTENEW), d3
    clr     d2
up_m:
    cmp     0, d3
    beq     up_file
    btst    1, d3
    beq     up_mdn
    mov     d2, d0
    mov     1, d1
    call    ParamStep, 0, 0
up_mdn:
    btst    2, d3
    beq     up_mnext
    mov     d2, d0
    mov     -1, d1
    call    ParamStep, 0, 0
up_mnext:
    lsr     2, d3
    inc     d2
    bra     up_m
up_file:
    mov     (V_FRES), d0            # a file request answered
    cmp     0, d0
    beq     up_patch
    clr     d1
    mov     d1, (V_FRES)
    cmp     1, d0
    bne     up_ffail
    mov     (V_FREQ), d0
    clr     d1
    mov     d1, (V_FREQ)
    cmp     1, d0
    bne     up_saved
    mov     (V_FBYTES), d0          # read: a bank?
    call    BankFromFile, 0, 0
    cmp     0, d0
    bne     up_fbad
    mov     (V_FILENO), d0
    inc     d0
    mov     d0, (V_BANKSRC)
    call    AllNotesOff, 0, 0
    mov     1, d0
    mov     d0, (V_PDIRTY)
    mov     D_ALL, d0
    mov     d0, (V_DIRTY)
    mov     msg_loaded, d0
    call    PostMsg, 0, 0
    bra     up_patch
up_saved:
    mov     msg_saved, d0
    call    PostMsg, 0, 0
    bra     up_patch
up_fbad:
    mov     msg_notbank, d0
    call    PostMsg, 0, 0
    bra     up_patch
up_ffail:
    clr     d1
    mov     d1, (V_FREQ)
    mov     msg_nofile, d0
    call    PostMsg, 0, 0
up_patch:
    mov     (V_PDIRTY), d0          # the DSP's words
    cmp     0, d0
    beq     up_msg
    clr     d0
    mov     d0, (V_PDIRTY)
    call    DspSendPatch, 0, 0
up_msg:
    mov     (V_MSG), d0             # the message's time is up
    cmp     0, d0
    beq     up_ret
    call    UiNow, 0, 0
    mov     (V_MSGT0), d1
    sub     d1, d0
    cmp     MSG_MS, d0
    bcs     up_ret
    clr     d0
    mov     d0, (V_MSG)
    mov     (V_DIRTY), d0
    or      D_TITLE, d0
    mov     d0, (V_DIRTY)
up_ret:
    ret     [d2,d3,a2,a3], 0x30

# SideKey(d0 = 0-7: L1-L4, R1-R4)
SideKey:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    cmp     6, d0
    bcc     sk_page
    mov     PG_OP, d1               # an operator: OP1-OP6
    mov     (V_PAGE), d2
    cmp     d1, d2
    bne     sk_op
    mov     (V_OPSEL), d2
    cmp     d0, d2
    bne     sk_op
    mov     (V_SUB), d2             # the same operator again: its other half
    xor     1, d2
    mov     d2, (V_SUB)
    bra     sk_dirty
sk_op:
    mov     d0, (V_OPSEL)
    mov     d1, (V_PAGE)
    clr     d0
    mov     d0, (V_SUB)
    mov     d0, (V_PEGSEL)
    bra     sk_dirty
sk_page:
    add     -6, d0                  # 0: VOICE, 1: BANK
    inc     d0
    mov     (V_PAGE), d1
    cmp     d0, d1
    bne     sk_new
    mov     (V_SUB), d2             # again: the other half (VOICE only)
    xor     1, d2
    cmp     PG_VOICE, d0
    beq     sk_sub
    clr     d2
sk_sub:
    mov     d2, (V_SUB)
    mov     d2, (V_PEGSEL)
    bra     sk_dirty
sk_new:
    mov     d0, (V_PAGE)
    clr     d0
    mov     d0, (V_SUB)
    mov     d0, (V_PEGSEL)
sk_dirty:
    mov     (V_DIRTY), d0
    or      D_TITLE | D_SIDE | D_DIAG | D_GRAPH | D_STRIP, d0
    mov     d0, (V_DIRTY)
    ret     [d2,d3,a2,a3], 0x30

# PatchStep(d0 = +1 / -1): the next or previous voice of the bank (wrapping); the notes released.
PatchStep:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (V_PATCH), d1
    add     d0, d1
    and     31, d1
    mov     d1, (V_PATCH)
    call    AllNotesOff, 0, 0
    mov     1, d0
    mov     d0, (V_PDIRTY)
    mov     (V_DIRTY), d0
    or      D_TITLE | D_DIAG | D_GRAPH | D_STRIP, d0
    mov     d0, (V_DIRTY)
    ret     [d2,d3,a2,a3], 0x30

# ---- drawing ---------------------------------------------------------------------------------------------------
# UiDraw: one piece of what is dirty (the task calls it between note polls): everything (cleared), then the title,
# the side labels, the diagram, the graph, the strip. Every LCD_CHECK_MS the title bar is compared with what was
# drawn there: when the firmware has drawn over the screen, everything is drawn again.
UiDraw:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (V_DIRTY), d2
    btst    D_ALL, d2
    beq     ud_title
    mov     C_BG, d0                # all: the screen cleared, every piece dirty
    mov     d0, (V_FG)
    clr     d0
    clr     d1
    mov     640, a0
    mov     240, a1
    call    FillRect, 0, 0
    mov     D_TITLE | D_SIDE | D_DIAG | D_GRAPH | D_STRIP, d2
    mov     d2, (V_DIRTY)
    mov     0xFFFF, d0
    mov     d0, (V_COLDIRTY)
    bra     ud_ret
ud_title:
    btst    D_TITLE, d2
    beq     ud_side
    call    DrawTitle, 0, 0
    mov     ~D_TITLE, d0
    bra     ud_clear
ud_side:
    btst    D_SIDE, d2
    beq     ud_diag
    call    DrawSide, 0, 0
    mov     ~D_SIDE, d0
    bra     ud_clear
ud_diag:
    btst    D_DIAG, d2
    beq     ud_graph
    call    DrawDiagram, 0, 0
    mov     ~D_DIAG, d0
    bra     ud_clear
ud_graph:
    btst    D_GRAPH, d2
    beq     ud_strip
    call    DrawGraph, 0, 0
    mov     ~D_GRAPH, d0
    bra     ud_clear
ud_strip:
    btst    D_STRIP, d2             # the names (and every value)
    beq     ud_cols
    call    DrawStripNames, 0, 0
    mov     0xFFFF, d0
    mov     d0, (V_COLDIRTY)
    mov     ~D_STRIP, d0
    bra     ud_clear
ud_cols:
    mov     (V_COLDIRTY), d3        # the values that changed, one column a call
    cmp     0, d3
    beq     ud_ret
    clr     d2
ud_col:
    btst    1, d3
    bne     ud_colgo
    lsr     1, d3
    inc     d2
    bra     ud_col
ud_colgo:
    mov     1, d0
    asl     d2, d0
    not     d0
    mov     (V_COLDIRTY), d1
    and     d0, d1
    mov     d1, (V_COLDIRTY)
    mov     d2, d0
    call    DrawValue, 0, 0
    bra     ud_ret
ud_clear:
    mov     (V_DIRTY), d1
    and     d0, d1
    mov     d1, (V_DIRTY)
ud_ret:
    ret     [d2,d3,a2,a3], 0x30

# LcdCheck: when the title bar no longer holds what we drew (its checksum), everything is drawn again.
LcdCheck:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (V_DIRTY), d0
    cmp     0, d0
    bne     lc_ret                  # still drawing
    call    LcdSum, 0, 0
    mov     (V_LCDSUM), d1
    cmp     d1, d0
    beq     lc_ret
    mov     D_ALL, d0
    mov     d0, (V_DIRTY)
    mov     (V_REDRAWS), d0
    inc     d0
    mov     d0, (V_REDRAWS)
lc_ret:
    ret     [d2,d3,a2,a3], 0x30

# LcdSum -> d0: a checksum of the title bar's rows 2 and 20 (what DrawTitle leaves there). Leaf.
LcdSum:
    clr     d0
    mov     FB + 2 * FB_STRIDE, a0
    mov     320, d1
ls_a:
    mov     (a0), a1
    add     a1, d0
    add     4, a0
    add     -1, d1
    bne     ls_a
    mov     FB + 20 * FB_STRIDE, a0
    mov     320, d1
ls_b:
    mov     (a0), a1
    add     a1, d0
    add     4, a0
    add     -1, d1
    bne     ls_b
    rets

# DrawTitle: "KN7FM", the voice's number and name, its algorithm, the page, the DSP's frame rate, or a message.
DrawTitle:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     C_TITLE, d0
    mov     d0, (V_FG)
    clr     d0
    clr     d1
    mov     640, a0
    mov     TITLE_H, a1
    call    FillRect, 0, 0
    mov     C_TITLE, d0
    mov     d0, (V_BG)
    mov     C_TEXT, d0
    mov     d0, (V_FG)
    mov     1, d0
    mov     d0, (V_SCALE)
    mov     str_title, a0
    mov     8, d0
    mov     8, d1
    call    DrawText, 0, 0
    mov     TMPSTR, a0              # " nn NAME______"
    mov     (V_PATCH), d0
    inc     d0
    mov     2, d1
    call    PutDecN, 0, 0
    mov     32, d0
    movbu   d0, (a0)
    inc     a0
    mov     a0, a2
    call    CurPatch, 0, 0
    add     VO_NAME, a0
    mov     a0, a1
    mov     a2, a0
    mov     10, d1
dtt_name:
    movbu   (a1), d0
    movbu   d0, (a0)
    inc     a0
    inc     a1
    add     -1, d1
    bne     dtt_name
    clr     d0
    movbu   d0, (a0)
    mov     C_EG, d0
    mov     d0, (V_FG)
    mov     TMPSTR, a0
    mov     64, d0
    mov     8, d1
    call    DrawText, 0, 0
    mov     C_TEXT, d0
    mov     d0, (V_FG)
    mov     TMPSTR, a0              # "ALG n"
    mov     str_alg, a1
    call    StrCpy, 0, 0
    mov     a0, (0x14, sp)
    call    CurAlg, 0, 0
    inc     d0
    mov     (0x14, sp), a0
    mov     1, d1
    call    PutDecN, 0, 0
    mov     TMPSTR, a0
    mov     184, d0
    mov     8, d1
    call    DrawText, 0, 0
    mov     TMPSTR, a0              # the page
    call    PageName, 0, 0
    mov     TMPSTR, a0
    mov     264, d0
    mov     8, d1
    call    DrawText, 0, 0
    mov     (V_MSG), d0             # a message, or the DSP's frame rate
    cmp     0, d0
    beq     dtt_rate
    mov     d0, a0
    mov     C_EG, d0
    mov     d0, (V_FG)
    mov     384, d0
    mov     8, d1
    call    DrawText, 0, 0
    bra     dtt_sum
dtt_rate:
    mov     TMPSTR, a0
    mov     (V_RATE), d0
    mov     5, d1
    call    PutDecN, 0, 0
    mov     str_persec, a1
    call    StrCpy, 0, 0
    mov     C_DIM, d0
    mov     d0, (V_FG)
    mov     TMPSTR, a0
    mov     528, d0
    mov     8, d1
    call    DrawText, 0, 0
dtt_sum:
    call    LcdSum, 0, 0
    mov     d0, (V_LCDSUM)
    ret     [d2,d3,a2,a3], 0x30

# PageName(a0 = where) -> the page's name there ("OP1 A", "VOICE B", "BANK"), NUL-ended.
PageName:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     (V_PAGE), d0
    cmp     PG_OP, d0
    bne     pn_voice
    mov     str_op, a1
    call    StrCpy, 0, 0
    mov     (V_OPSEL), d0
    add     0x31, d0
    movbu   d0, (a0)
    inc     a0
    bra     pn_sub
pn_voice:
    cmp     PG_VOICE, d0
    bne     pn_bank
    mov     str_voice, a1
    call    StrCpy, 0, 0
pn_sub:
    mov     32, d0
    movbu   d0, (a0)
    mov     (V_SUB), d0
    add     0x41, d0
    movbu   d0, (1, a0)
    clr     d0
    movbu   d0, (2, a0)
    bra     pn_ret
pn_bank:
    mov     str_bank, a1
    call    StrCpy, 0, 0
    clr     d0
    movbu   d0, (a0)
pn_ret:
    ret     [d2,d3,a2,a3], 0x30

# DrawSide: the side buttons' labels, the page shown lit.
DrawSide:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    clr     d2                      # 0-7: L1-L4, R1-R4
ds_one:
    mov     d2, d0
    call    SideLit, 0, 0           # d0 = 1 when it names the page shown
    mov     C_LABEL, d1
    cmp     0, d0
    beq     ds_col
    mov     C_SEL, d1
ds_col:
    mov     d1, (V_FG)
    mov     d1, (V_BG)
    mov     d2, d0                  # x: left 4, right 640 - SIDE_W + 4 ... y: TITLE_H + 42 x row + 6
    and     3, d0
    mov     ROW_H, d1
    mulu    d1, d0
    add     TITLE_H + 6, d0
    mov     d0, d3                  # d3 = y
    mov     4, d0
    cmp     4, d2
    bcs     ds_x
    mov     640 - SIDE_W + 4, d0
ds_x:
    mov     d0, (0x14, sp)
    mov     d3, d1
    mov     SIDE_W - 8, a0
    mov     30, a1
    call    FillRect, 0, 0
    mov     C_TEXT, d0
    mov     d0, (V_FG)
    mov     d2, d0
    asl     2, d0
    mov     SideNames, a0
    add     d0, a0
    mov     (a0), a0
    mov     (0x14, sp), d0
    add     12, d0
    mov     d3, d1
    add     9, d1
    call    DrawText, 0, 0
    inc     d2
    cmp     8, d2
    bcs     ds_one
    ret     [d2,d3,a2,a3], 0x30

# SideLit(d0 = 0-7) -> d0 = 1 when that side button names the page shown. Leaf.
SideLit:
    cmp     6, d0
    bcc     sl2_page
    mov     (V_PAGE), d1
    cmp     PG_OP, d1
    bne     sl2_no
    mov     (V_OPSEL), d1
    cmp     d0, d1
    bne     sl2_no
    mov     1, d0
    rets
sl2_page:
    add     -5, d0                  # 6 -> PG_VOICE, 7 -> PG_BANK
    mov     (V_PAGE), d1
    cmp     d0, d1
    bne     sl2_no
    mov     1, d0
    rets
sl2_no:
    clr     d0
    rets

# DrawDiagram: the current algorithm's diagram (T_DIAG, tools/fm_host.py): its strokes, then the six boxes,
# carriers and modulators in their colours, the operator shown outlined.
DrawDiagram:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x30, sp
    mov     C_BG, d0
    mov     d0, (V_FG)
    mov     DIAG_X, d0
    mov     DIAG_Y, d1
    mov     DIAG_W, a0
    mov     DIAG_H, a1
    call    FillRect, 0, 0
    call    CurAlg, 0, 0
    asl     2, d0
    mov     T_DIAG, a0
    add     d0, a0
    mov     (a0), a2                # a2 = its record: strokes, carriers, boxes, then the strokes
    movhu   (a2), d3                # d3 = strokes
    mov     C_LINE, d0
    mov     d0, (V_FG)
    mov     a2, a3
    add     28, a3                  # the first stroke: x, y, w, h
dd_stroke:
    cmp     0, d3
    beq     dd_boxes
    movhu   (a3), d0
    add     DIAG_X, d0
    movhu   (2, a3), d1
    add     DIAG_Y, d1
    movhu   (4, a3), a0
    movhu   (6, a3), a1
    call    FillRect, 0, 0
    add     8, a3
    add     -1, d3
    bra     dd_stroke
dd_boxes:
    clr     d3                      # d3 = OP d3 + 1
dd_box:
    mov     d3, d0
    asl     2, d0
    mov     a2, a0
    add     d0, a0
    movhu   (4, a0), d0
    add     DIAG_X, d0
    mov     d0, (0x14, sp)          # x
    movhu   (6, a0), d1
    add     DIAG_Y, d1
    mov     d1, (0x18, sp)          # y
    movhu   (2, a2), d1             # a carrier? (its bit in the record's mask)
    mov     1, d0
    asl     d3, d0
    and     d0, d1
    mov     C_MOD, d0
    cmp     0, d1
    beq     dd_col
    mov     C_CAR, d0
dd_col:
    mov     d0, (0x20, sp)          # the box's colour
    mov     C_TEXT, d0              # the outline: white, yellow for the operator shown
    mov     (V_PAGE), d1
    cmp     PG_OP, d1
    bne     dd_ol
    mov     (V_OPSEL), d1
    cmp     d3, d1
    bne     dd_ol
    mov     C_EG, d0
dd_ol:
    mov     d0, (V_FG)
    mov     (0x14, sp), d0
    mov     (0x18, sp), d1
    mov     BOX_W, a0
    mov     BOX_H, a1
    call    FillRect, 0, 0
    mov     (0x20, sp), d0
    mov     d0, (V_FG)
    mov     d0, (V_BG)
    mov     (0x14, sp), d0
    inc     d0
    mov     (0x18, sp), d1
    inc     d1
    mov     BOX_W - 2, a0
    mov     BOX_H - 2, a1
    call    FillRect, 0, 0
    mov     C_TEXT, d0
    mov     d0, (V_FG)
    mov     d3, d0
    add     0x31, d0
    mov     (0x14, sp), d1
    add     8, d1
    mov     (0x18, sp), a1
    add     2, a1
    call    DrawChar, 0, 0
    inc     d3
    cmp     6, d3
    bcs     dd_box
    ret     [d2,d3,a2,a3], 0x40

# DrawGraph: the envelope of the operator shown (V_OPSEL), or the pitch envelope (V_PEGSEL): four rates and four
# levels, drawn as the DX7 manual draws them: from L4 up (or down) to L1 at R1, to L2 at R2, to L3 at R3, held,
# back to L4 at R4. A segment's width shrinks as its rate rises.
.equ GR_X0,         0x14
.equ GR_Y0,         0x18
.equ GR_I,          0x1C
DrawGraph:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     C_BG, d0
    mov     d0, (V_FG)
    mov     GRAPH_X, d0
    mov     GRAPH_Y, d1
    mov     GRAPH_W, a0
    mov     GRAPH_H, a1
    call    FillRect, 0, 0
    mov     C_GRID, d0              # the frame: bottom line, the key-off mark
    mov     d0, (V_FG)
    mov     GRAPH_X, d0
    mov     GRAPH_Y + GRAPH_H - 1, d1
    mov     GRAPH_W, a0
    mov     1, a1
    call    FillRect, 0, 0
    call    CurPatch, 0, 0          # a2 = the rates (R1-R4), the levels at a2 + 4
    mov     (V_PEGSEL), d0
    cmp     0, d0
    beq     dg_op
    add     VO_PR, a0
    bra     dg_have
dg_op:
    mov     (V_OPSEL), d0           # OP n: the VCED's operator 6 - n
    mov     5, d1
    sub     d0, d1
    mov     21, d0
    mulu    d0, d1
    add     d1, a0
dg_have:
    mov     a0, a2
    mov     C_EG, d0
    mov     d0, (V_FG)
    mov     GRAPH_X + 4, d2         # the start: x, L4
    movbu   (7, a2), d0
    call    LevelY, 0, 0
    mov     d0, d3
    clr     d0
    mov     d0, (GR_I, sp)
dg_seg:
    mov     d2, (GR_X0, sp)
    mov     d3, (GR_Y0, sp)
    mov     (GR_I, sp), d1          # stage i: to L(i+1) at R(i+1); stage 3 is the key-off: the hold first
    cmp     3, d1
    bne     dg_rate
    mov     d2, d0                  # the sustain: a flat stretch
    add     24, d0
    mov     d0, d2
    mov     (GR_X0, sp), d0
    mov     (GR_Y0, sp), d1
    mov     d2, a0
    mov     d3, a1
    call    DrawLine, 0, 0
    mov     C_GRID, d0              # the key-off mark
    mov     d0, (V_FG)
    mov     d2, d0
    mov     GRAPH_Y + 4, d1
    mov     1, a0
    mov     GRAPH_H - 6, a1
    call    FillRect, 0, 0
    mov     C_EG, d0
    mov     d0, (V_FG)
    mov     d2, (GR_X0, sp)
    mov     (GR_I, sp), d1
dg_rate:
    mov     a2, a0
    add     d1, a0
    movbu   (a0), d0                # the rate: width 4 + (99 - R) x 3 / 7
    mov     99, d1
    sub     d0, d1
    mov     d1, d0
    mov     3, d1
    mulu    d1, d0
    clr     d1
    mov     d1, mdr
    mov     7, d1
    divu    d1, d0
    add     4, d0
    add     d0, d2
    mov     (GR_I, sp), d1
    mov     a2, a0
    add     d1, a0
    movbu   (4, a0), d0             # its level
    call    LevelY, 0, 0
    mov     d0, d3
    mov     (GR_X0, sp), d0
    mov     (GR_Y0, sp), d1
    mov     d2, a0
    mov     d3, a1
    call    DrawLine, 0, 0
    mov     (GR_I, sp), d0
    inc     d0
    mov     d0, (GR_I, sp)
    cmp     4, d0
    bcs     dg_seg
    ret     [d2,d3,a2,a3], 0x30

# LevelY(d0 = level 0-99) -> d0 = its y in the graph. Leaf.
LevelY:
    mov     GRAPH_H - 12, d1
    mulu    d1, d0
    clr     d1
    mov     d1, mdr
    mov     99, d1
    divu    d1, d0
    mov     GRAPH_Y + GRAPH_H - 6, d1
    sub     d0, d1
    mov     d1, d0
    rets

# DrawLine(d0 = x0, d1 = y0, a0 = x1, a1 = y1) in V_FG, one pixel wide (Bresenham).
.equ DL_DX,         0x14
.equ DL_DY,         0x18
.equ DL_ERR,        0x1C
.equ DL_SX,         0x20
.equ DL_SY,         0x24
.equ DL_X1,         0x28
.equ DL_Y1,         0x2C
DrawLine:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x30, sp
    mov     d0, d2                  # d2, d3 = the point
    mov     d1, d3
    mov     a0, (DL_X1, sp)
    mov     a1, (DL_Y1, sp)
    mov     a0, d0                  # dx = |x1 - x0|, sx
    sub     d2, d0
    mov     1, d1
    cmp     0, d0
    bge     dl_sx
    mov     -1, d1
    mov     d0, a0
    clr     d0
    sub     a0, d0
dl_sx:
    mov     d1, (DL_SX, sp)
    mov     d0, (DL_DX, sp)
    mov     (DL_Y1, sp), d0         # dy = -|y1 - y0|, sy
    sub     d3, d0
    mov     1, d1
    cmp     0, d0
    bge     dl_sy
    mov     -1, d1
    mov     d0, a0
    clr     d0
    sub     a0, d0
dl_sy:
    mov     d1, (DL_SY, sp)
    mov     d0, a0
    clr     d0
    sub     a0, d0
    mov     d0, (DL_DY, sp)
    mov     (DL_DX, sp), d1
    add     d1, d0
    mov     d0, (DL_ERR, sp)        # err = dx + dy
dl_pt:
    mov     d3, d0                  # the pixel
    mov     FB_STRIDE, d1
    mulu    d1, d0
    add     d2, d0
    add     d2, d0
    mov     FB, a0
    add     d0, a0
    mov     (V_FG), d0
    movhu   d0, (a0)
    mov     (DL_X1, sp), d0
    cmp     d2, d0
    bne     dl_step
    mov     (DL_Y1, sp), d0
    cmp     d3, d0
    beq     dl_ret
dl_step:
    mov     (DL_ERR, sp), d0        # e2 = 2 err
    add     d0, d0
    mov     d0, a0
    mov     (DL_DY, sp), d1
    cmp     d1, d0
    blt     dl_noy
    mov     (DL_ERR, sp), d0        # e2 >= dy: err += dy, x += sx
    add     d1, d0
    mov     d0, (DL_ERR, sp)
    mov     (DL_SX, sp), d1
    add     d1, d2
dl_noy:
    mov     a0, d0
    mov     (DL_DX, sp), d1
    cmp     d1, d0
    bgt     dl_pt
    mov     (DL_ERR, sp), d0        # e2 <= dx: err += dx, y += sy
    add     d1, d0
    mov     d0, (DL_ERR, sp)
    mov     (DL_SY, sp), d1
    add     d1, d3
    bra     dl_pt
dl_ret:
    ret     [d2,d3,a2,a3], 0x40

# DrawStripNames: the strip cleared, each column's name (its value follows, DrawValue).
DrawStripNames:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     C_BG, d0
    mov     d0, (V_FG)
    clr     d0
    mov     STRIP_Y, d1
    mov     640, a0
    mov     240 - STRIP_Y, a1
    call    FillRect, 0, 0
    mov     C_GRID, d0              # the columns' separators
    mov     d0, (V_FG)
    mov     COL_W, d2
dsn_sep:
    mov     d2, d0
    mov     STRIP_Y, d1
    mov     1, a0
    mov     240 - STRIP_Y, a1
    call    FillRect, 0, 0
    add     COL_W, d2
    cmp     640, d2
    bcs     dsn_sep
    mov     C_BG, d0
    mov     d0, (V_BG)
    mov     C_NAME, d0
    mov     d0, (V_FG)
    call    PageTable, 0, 0
    mov     a0, a2
    clr     d2
dsn_col:
    mov     (PR_NAME, a2), a0
    mov     d2, d0
    mov     COL_W, d1
    mulu    d1, d0
    add     4, d0
    mov     STRIP_Y + 4, d1
    mov     4, a1
    call    DrawTextN, 0, 0
    add     PR_LEN, a2
    inc     d2
    cmp     16, d2
    bcs     dsn_col
    ret     [d2,d3,a2,a3], 0x30

# DrawValue(d0 = column): its value, in its display form.
DrawValue:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, d2
    mov     COL_W, d1
    mulu    d1, d0
    add     2, d0
    mov     d0, d3                  # d3 = x
    mov     C_BG, d0
    mov     d0, (V_FG)
    mov     d3, d0
    mov     STRIP_Y + 20, d1
    mov     COL_W - 3, a0
    mov     240 - STRIP_Y - 20, a1
    call    FillRect, 0, 0
    call    PageTable, 0, 0
    mov     d2, d0
    asl     3, d0
    add     d0, a0
    mov     a0, a2                  # a2 = the record
    mov     TMPSTR, a0
    clr     d0
    movbu   d0, (a0)
    movbu   (PR_KIND, a2), d0
    cmp     K_NONE, d0
    beq     dv_draw
    cmp     K_LOAD, d0
    beq     dv_draw
    cmp     K_SAVE, d0
    beq     dv_draw
    cmp     K_FILE, d0
    bne     dv_byte
    mov     TMPSTR, a0              # BANKn
    mov     (V_FILENO), d0
    mov     1, d1
    call    PutDecN, 0, 0
    bra     dv_draw
dv_byte:
    mov     a2, a1
    call    ParamAddr, 0, 0
    mov     d0, d1                  # d1 = the value
    movbu   (PR_TYPE, a2), d0
    mov     TMPSTR, a0
    call    FormatValue, 0, 0
dv_draw:
    mov     C_BG, d0
    mov     d0, (V_BG)
    mov     C_VAL, d0
    mov     d0, (V_FG)
    mov     2, d0
    mov     d0, (V_SCALE)
    mov     TMPSTR, a0              # two characters at scale 2, four at scale 1
    call    StrLen, 0, 0
    cmp     2, d0
    bls     dv_big
    mov     1, d0
    mov     d0, (V_SCALE)
    mov     TMPSTR, a0
    mov     d3, d0
    add     2, d0
    mov     STRIP_Y + 24, d1
    mov     4, a1
    call    DrawTextN, 0, 0
    bra     dv_ret
dv_big:
    mov     TMPSTR, a0
    mov     d3, d0
    add     2, d0
    mov     STRIP_Y + 20, d1
    mov     2, a1
    call    DrawTextN, 0, 0
dv_ret:
    mov     1, d0
    mov     d0, (V_SCALE)
    ret     [d2,d3,a2,a3], 0x30

# FormatValue(d0 = T_* display, d1 = the value, a0 = where) -> the text there, NUL-ended.
FormatValue:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d1, d2
    cmp     T_PLUS1, d0
    beq     fv_plus1
    cmp     T_MODE, d0
    beq     fv_mode
    cmp     T_CURVE, d0
    beq     fv_curve
    cmp     T_WAVE, d0
    beq     fv_wave
    cmp     T_ONOFF, d0
    beq     fv_onoff
    cmp     T_DET, d0
    beq     fv_det
    cmp     T_TRN, d0
    beq     fv_trn
    cmp     T_CHAR, d0
    beq     fv_char
    mov     d2, d0                  # T_NUM
    mov     1, d1
    call    PutDecN, 0, 0
    bra     fv_ret
fv_plus1:
    mov     d2, d0
    inc     d0
    mov     1, d1
    call    PutDecN, 0, 0
    bra     fv_ret
fv_mode:
    mov     str_ratio, a1
    cmp     0, d2
    beq     fv_str
    mov     str_fixed, a1
    bra     fv_str
fv_curve:
    mov     d2, d0
    and     3, d0
    asl     2, d0
    mov     CurveNames, a1
    add     d0, a1
    mov     (a1), a1
    bra     fv_str
fv_wave:
    mov     d2, d0
    cmp     5, d0
    bls     fv_w
    mov     5, d0
fv_w:
    asl     2, d0
    mov     WaveNames, a1
    add     d0, a1
    mov     (a1), a1
    bra     fv_str
fv_onoff:
    mov     str_off, a1
    cmp     0, d2
    beq     fv_str
    mov     str_on, a1
    bra     fv_str
fv_det:
    mov     d2, d0
    add     -7, d0
    bra     fv_signed
fv_trn:
    mov     d2, d0
    add     -24, d0
fv_signed:
    cmp     0, d0
    beq     fv_zero
    mov     0x2B, d1                # '+'
    cmp     0, d0
    bgt     fv_sign
    mov     0x2D, d1                # '-'
    mov     d0, d2
    clr     d0
    sub     d2, d0
fv_sign:
    movbu   d1, (a0)
    inc     a0
fv_zero:
    mov     1, d1
    call    PutDecN, 0, 0
    bra     fv_ret
fv_char:
    movbu   d2, (a0)
    clr     d0
    movbu   d0, (1, a0)
    bra     fv_ret
fv_str:
    call    StrCpy, 0, 0
    clr     d0
    movbu   d0, (a0)
fv_ret:
    ret     [d2,d3,a2,a3], 0x30

# ---- text helpers ----------------------------------------------------------------------------------------------
# StrCpy(a0 = where, a1 = a string) -> a0 past the copy (not NUL-ended). Leaf.
StrCpy:
    movbu   (a1), d0
    cmp     0, d0
    beq     scp_end
    movbu   d0, (a0)
    inc     a0
    inc     a1
    bra     StrCpy
scp_end:
    rets

# StrLen(a0 = a string) -> d0 = its length. Leaf.
StrLen:
    clr     d0
sln_loop:
    movbu   (a0), d1
    cmp     0, d1
    beq     sln_end
    inc     d0
    inc     a0
    bra     sln_loop
sln_end:
    rets

# PutDecN(d0 = a value >= 0, d1 = at least this many digits (zeros in front), a0 = where) -> a0 past the digits,
# NUL-ended. Leaf.
PutDecN:
    mov     V_DECBUF + 12, a1       # the digits, backwards
    mov     a0, (V_TMP2)
pdn_dig:
    mov     d1, (V_TMP)
    clr     d1
    mov     d1, mdr
    mov     10, d1
    divu    d1, d0                  # d0 = the rest, mdr = the digit
    mov     mdr, d1
    add     0x30, d1
    add     -1, a1
    movbu   d1, (a1)
    mov     (V_TMP), d1
    add     -1, d1
    cmp     0, d0
    bne     pdn_dig
    cmp     0, d1
    bgt     pdn_dig
    mov     (V_TMP2), a0
pdn_cpy:
    mov     V_DECBUF + 12, d0
    mov     a1, d1
    cmp     d0, d1
    beq     pdn_end
    movbu   (a1), d0
    movbu   d0, (a0)
    inc     a0
    inc     a1
    bra     pdn_cpy
pdn_end:
    clr     d0
    movbu   d0, (a0)
    rets

# ---- primitives (DSPTONE's) ------------------------------------------------------------------------------------
# FillRect(d0 = x, d1 = y, a0 = width, a1 = height) in V_FG; two pixels a store when x and the width are even.
FillRect:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, (0x14, sp)
    mov     d1, d2
    mov     FB_STRIDE, d3
    mulu    d3, d2
    add     d0, d0
    add     d0, d2
    mov     FB, a2
    add     d2, a2
    mov     (V_FG), d3
    cmp     0, a0
    beq     fr_ret
    cmp     0, a1
    beq     fr_ret
    mov     (0x14, sp), d0
    mov     a0, d1
    or      d1, d0
    btst    1, d0
    bne     fr_row16
    mov     d3, d0
    exthu   d3
    asl     16, d0
    or      d0, d3
    mov     a0, d0
    lsr     1, d0
    mov     d0, a0
fr_row32:
    mov     a2, a3
    mov     a0, d2
fr_px32:
    mov     d3, (a3)
    add     4, a3
    add     -1, d2
    bne     fr_px32
    add     FB_STRIDE, a2
    add     -1, a1
    cmp     0, a1
    bne     fr_row32
    bra     fr_ret
fr_row16:
    mov     a2, a3
    mov     a0, d2
fr_px16:
    movhu   d3, (a3)
    add     2, a3
    add     -1, d2
    bne     fr_px16
    add     FB_STRIDE, a2
    add     -1, a1
    cmp     0, a1
    bne     fr_row16
fr_ret:
    ret     [d2,d3,a2,a3], 0x30

# DrawText(a0 = text, d0 = x, d1 = y) -> d0 = x after; DrawTextN the same with at most a1 characters. x even.
DrawText:
    mov     255, a1
DrawTextN:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a2
    mov     d0, d2
    mov     d1, d3
    mov     a1, (0x14, sp)
dt_char:
    movbu   (a2), d0
    cmp     0, d0
    beq     dt_done
    mov     (0x14, sp), d1
    cmp     0, d1
    beq     dt_done
    add     -1, d1
    mov     d1, (0x14, sp)
    inc     a2
    mov     d2, d1
    mov     d3, a1
    call    DrawChar, 0, 0
    mov     (V_SCALE), d0
    asl     3, d0
    add     d0, d2
    bra     dt_char
dt_done:
    mov     d2, d0
    ret     [d2,d3,a2,a3], 0x30

# DrawChar(d0 = character, d1 = x (even), a1 = y): the 8x13 cell in V_FG on V_BG, two pixels a 32-bit store (at
# scale 2 each pixel is doubled: one store a font pixel, each row twice).
DrawChar:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    cmp     0x20, d0
    bcs     dc_other
    cmp     0x7F, d0
    bcs     dc_ok
dc_other:
    mov     0x3F, d0
dc_ok:
    add     -0x20, d0
    mov     13, d2
    mulu    d2, d0
    mov     Font8x13, a3
    add     d0, a3
    mov     a1, d2
    mov     FB_STRIDE, d3
    mulu    d3, d2
    add     d1, d1
    add     d1, d2
    mov     FB, a2
    add     d2, a2
    mov     13, d0
    mov     d0, (0x14, sp)
dch_row:
    movbu   (a3), d2
    inc     a3
    mov     (V_SCALE), d0
    mov     d0, (0x18, sp)
dch_rep:
    mov     a2, a0
    mov     (V_SCALE), d0
    cmp     2, d0
    beq     dch_x2
    mov     0x80, d3
dch_pair:
    mov     (V_BG), d1
    mov     d2, d0
    and     d3, d0
    beq     dch_l
    mov     (V_FG), d1
dch_l:
    exthu   d1
    mov     d1, (0x1C, sp)
    lsr     1, d3
    mov     (V_BG), d1
    mov     d2, d0
    and     d3, d0
    beq     dch_r
    mov     (V_FG), d1
dch_r:
    asl     16, d1
    mov     (0x1C, sp), d0
    or      d0, d1
    mov     d1, (a0)
    add     4, a0
    lsr     1, d3
    bne     dch_pair
    bra     dch_next
dch_x2:
    mov     0x80, d3
dch_px2:
    mov     (V_BG), d1
    mov     d2, d0
    and     d3, d0
    beq     dch_p2
    mov     (V_FG), d1
dch_p2:
    exthu   d1
    mov     d1, d0
    asl     16, d0
    or      d0, d1
    mov     d1, (a0)
    add     4, a0
    lsr     1, d3
    bne     dch_px2
dch_next:
    add     FB_STRIDE, a2
    mov     (0x18, sp), d0
    add     -1, d0
    mov     d0, (0x18, sp)
    bne     dch_rep
    mov     (0x14, sp), d0
    add     -1, d0
    mov     d0, (0x14, sp)
    bne     dch_row
    ret     [d2,d3,a2,a3], 0x30

# ---- data ------------------------------------------------------------------------------------------------------
ButtonTab:  # in B_* order: the segment's byte offset from CPR and the switch's bit (kn7menu menu_data.s)
    .byte   0x30, 0x02              # B_L1   LCDL 1           CPL_SEG0 SW1
    .byte   0x30, 0x08              # B_L2   LCDL 2           CPL_SEG0 SW3
    .byte   0x30, 0x20              # B_L3   LCDL 3           CPL_SEG0 SW5
    .byte   0x30, 0x01              # B_L4   LCDL 4           CPL_SEG0 SW0
    .byte   0x05, 0x10              # B_R1   LCDR 1           CPR_SEG5 SW4
    .byte   0x05, 0x20              # B_R2   LCDR 2           CPR_SEG5 SW5
    .byte   0x07, 0x01              # B_R3   LCDR 3           CPR_SEG7 SW0
    .byte   0x06, 0x01              # B_R4   LCDR 4           CPR_SEG6 SW0
    .byte   0x3B, 0x10              # B_PGUP PAGE UP          CPC_SEG11 SW4
    .byte   0x3B, 0x20              # B_PGDN PAGE DOWN        CPC_SEG11 SW5
    .byte   0x3B, 0x80              # B_EXIT EXIT             CPC_SEG11 SW7
MuteTab:    # columns 1-16: the byte (CPC_SEGn at 0x30 + n) and MUTE UP's bit; MUTE DOWN is the next bit
            # (kn7000_mame kn7000_cpanel.cpp: CPC_SEG5 SW4-7, CPC_SEG8-10 SW0-7, CPC_SEG11 SW0-3)
    .byte   0x35, 0x10, 0x35, 0x40
    .byte   0x38, 0x01, 0x38, 0x04, 0x38, 0x10, 0x38, 0x40
    .byte   0x39, 0x01, 0x39, 0x04, 0x39, 0x10, 0x39, 0x40
    .byte   0x3A, 0x01, 0x3A, 0x04, 0x3A, 0x10, 0x3A, 0x40
    .byte   0x3B, 0x01, 0x3B, 0x04
    .align  4
SideNames:
    .long   str_op1, str_op2, str_op3, str_op4, str_op5, str_op6, str_voice, str_bank
CurveNames:
    .long   str_mln, str_mex, str_pex, str_pln
WaveNames:
    .long   str_tri, str_sawd, str_sawu, str_sqr, str_sin, str_sh
PageTabs:   # (V_PAGE, V_SUB)
    .long   page_op_a, page_op_b, page_voice_a, page_voice_b, page_bank, page_bank

# the pages: kind, byte, maximum, display, name
page_op_a:
    .byte   K_OP, 0, 99, T_NUM
    .long   str_r1
    .byte   K_OP, 1, 99, T_NUM
    .long   str_r2
    .byte   K_OP, 2, 99, T_NUM
    .long   str_r3
    .byte   K_OP, 3, 99, T_NUM
    .long   str_r4
    .byte   K_OP, 4, 99, T_NUM
    .long   str_l1
    .byte   K_OP, 5, 99, T_NUM
    .long   str_l2
    .byte   K_OP, 6, 99, T_NUM
    .long   str_l3
    .byte   K_OP, 7, 99, T_NUM
    .long   str_l4
    .byte   K_OP, 16, 99, T_NUM
    .long   str_out
    .byte   K_OP, 15, 7, T_NUM
    .long   str_vel
    .byte   K_OP, 14, 3, T_NUM
    .long   str_ams
    .byte   K_OP, 13, 7, T_NUM
    .long   str_rs
    .byte   K_OP, 17, 1, T_MODE
    .long   str_mode
    .byte   K_OP, 18, 31, T_NUM
    .long   str_crs
    .byte   K_OP, 19, 99, T_NUM
    .long   str_fine
    .byte   K_OP, 20, 14, T_DET
    .long   str_det
page_op_b:
    .byte   K_OP, 8, 99, T_NUM
    .long   str_bp
    .byte   K_OP, 9, 99, T_NUM
    .long   str_ld
    .byte   K_OP, 10, 99, T_NUM
    .long   str_rd
    .byte   K_OP, 11, 3, T_CURVE
    .long   str_lc
    .byte   K_OP, 12, 3, T_CURVE
    .long   str_rc
    .fill   11, 8, 0
page_voice_a:
    .byte   K_VOICE, 134, 31, T_PLUS1
    .long   str_alg2
    .byte   K_VOICE, 135, 7, T_NUM
    .long   str_fb
    .byte   K_VOICE, 136, 1, T_ONOFF
    .long   str_oks
    .byte   K_VOICE, 144, 48, T_TRN
    .long   str_trn
    .byte   K_VOICE, 137, 99, T_NUM
    .long   str_lfs
    .byte   K_VOICE, 138, 99, T_NUM
    .long   str_lfd
    .byte   K_VOICE, 139, 99, T_NUM
    .long   str_pmd
    .byte   K_VOICE, 140, 99, T_NUM
    .long   str_amd
    .byte   K_VOICE, 141, 1, T_ONOFF
    .long   str_sync
    .byte   K_VOICE, 142, 5, T_WAVE
    .long   str_wave
    .byte   K_VOICE, 143, 7, T_NUM
    .long   str_pms
    .fill   5, 8, 0
page_voice_b:
    .byte   K_VOICE, 126, 99, T_NUM
    .long   str_r1
    .byte   K_VOICE, 127, 99, T_NUM
    .long   str_r2
    .byte   K_VOICE, 128, 99, T_NUM
    .long   str_r3
    .byte   K_VOICE, 129, 99, T_NUM
    .long   str_r4
    .byte   K_VOICE, 130, 99, T_NUM
    .long   str_l1
    .byte   K_VOICE, 131, 99, T_NUM
    .long   str_l2
    .byte   K_VOICE, 132, 99, T_NUM
    .long   str_l3
    .byte   K_VOICE, 133, 99, T_NUM
    .long   str_l4
    .fill   8, 8, 0
page_bank:
    .byte   K_FILE, 0, 9, T_NUM
    .long   str_file
    .byte   K_NONE, 0, 0, 0
    .long   str_none
    .byte   K_LOAD, 0, 0, T_ACT
    .long   str_load
    .byte   K_NONE, 0, 0, 0
    .long   str_none
    .byte   K_SAVE, 0, 0, T_ACT
    .long   str_save
    .byte   K_NONE, 0, 0, 0
    .long   str_none
    .byte   K_NAME, 145, 126, T_CHAR
    .long   str_name
    .byte   K_NAME, 146, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 147, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 148, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 149, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 150, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 151, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 152, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 153, 126, T_CHAR
    .long   str_none
    .byte   K_NAME, 154, 126, T_CHAR
    .long   str_none

str_title:      .asciz  "KN7FM"
str_alg:        .asciz  "ALG "
str_persec:     .asciz  "/s"
str_op:         .asciz  "OP"
str_op1:        .asciz  "OP1"
str_op2:        .asciz  "OP2"
str_op3:        .asciz  "OP3"
str_op4:        .asciz  "OP4"
str_op5:        .asciz  "OP5"
str_op6:        .asciz  "OP6"
str_voice:      .asciz  "VOICE"
str_bank:       .asciz  "BANK"
str_none:       .asciz  ""
str_r1:         .asciz  "R1"
str_r2:         .asciz  "R2"
str_r3:         .asciz  "R3"
str_r4:         .asciz  "R4"
str_l1:         .asciz  "L1"
str_l2:         .asciz  "L2"
str_l3:         .asciz  "L3"
str_l4:         .asciz  "L4"
str_out:        .asciz  "OUT"
str_vel:        .asciz  "VEL"
str_ams:        .asciz  "AMS"
str_rs:         .asciz  "RS"
str_mode:       .asciz  "MODE"
str_crs:        .asciz  "CRS"
str_fine:       .asciz  "FINE"
str_det:        .asciz  "DET"
str_bp:         .asciz  "BP"
str_ld:         .asciz  "LD"
str_rd:         .asciz  "RD"
str_lc:         .asciz  "LC"
str_rc:         .asciz  "RC"
str_alg2:       .asciz  "ALG"
str_fb:         .asciz  "FB"
str_oks:        .asciz  "SYNC"
str_trn:        .asciz  "TRNS"
str_lfs:        .asciz  "SPD"
str_lfd:        .asciz  "DLY"
str_pmd:        .asciz  "PMD"
str_amd:        .asciz  "AMD"
str_sync:       .asciz  "LSYN"
str_wave:       .asciz  "WAVE"
str_pms:        .asciz  "PMS"
str_file:       .asciz  "FILE"
str_load:       .asciz  "LOAD"
str_save:       .asciz  "SAVE"
str_name:       .asciz  "NAME"
str_ratio:      .asciz  "RAT"
str_fixed:      .asciz  "FIX"
str_mln:        .asciz  "-LN"
str_mex:        .asciz  "-EX"
str_pex:        .asciz  "+EX"
str_pln:        .asciz  "+LN"
str_tri:        .asciz  "TRI"
str_sawd:       .asciz  "SAW-"
str_sawu:       .asciz  "SAW+"
str_sqr:        .asciz  "SQR"
str_sin:        .asciz  "SIN"
str_sh:         .asciz  "S/H"
str_on:         .asciz  "ON"
str_off:        .asciz  "OFF"
msg_loading:    .asciz  "Loading..."
msg_saving:     .asciz  "Saving..."
msg_arm:        .asciz  "SAVE again to overwrite"
msg_loaded:     .asciz  "Bank loaded"
msg_saved:      .asciz  "Bank saved"
msg_notbank:    .asciz  "Not a DX7 bank"
msg_nofile:     .asciz  "File error"
    .align  4
