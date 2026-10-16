# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# KN7000 uptime clock -- a homebrew app for the Technics SX-KN7000 running the
# Key Soft Service "SD PLUG-iN" (SP7001) firmware, program 942 / table 83.
#
# It shows, in the bottom-right corner of the LCD, how long the instrument has been
# on, as HH:MM:SS, updated five times a second on every screen.
#
# How it gets to run: the SP7001 firmware loads its base plug-in, BASEEXT.XAP, from
# the SD card the first time the SD card screen is opened (the orange SD CARD
# button), copying the whole 0x1D000-byte file to RAM 0x5029B000 and calling its
# first byte. This module is placed in the file's unused tail (tools/xap_addon.py in
# kn7000_disassembly) and the plug-in's first instruction jumps here; we start a
# task of our own and hand control back, so the SD card features work as before.
#
# Firmware facts used (all in kn7000_disassembly/notes/sp7001-build-diff.md and the
# docs page "KN7000 Custom SD Apps"):
#   0x500D3C68  32-bit kernel tick counter, 1 kHz (measured 999.5/s); starts ~0.27 s
#               after power-on, so it is the time since the instrument was turned on.
#   0x9CE00000  the LCD frame the controller scans: 640x240 RGB565, 1280 bytes/row.
#               The firmware only recomposites the rectangles it redraws, so pixels
#               written here stay until it repaints that spot; we redraw every 200 ms.
#   cre_tsk_X / sta_tsk / dly_tsk  -- the plug-in API's micro-ITRON task services
#               (names from the firmware's own export table). The firmware's tasks
#               all run at priority 3 and use ids 1-8 of the 11 task slots (0-10).
#
# Build: see Makefile (`make` needs kn7000_disassembly beside this repo).

.mnbase 0x502B6000                  # where xap_addon.py places this module

# ---- firmware -----------------------------------------------------------------
.equ XAP_RESUME,      0x5029B005    # XapEntry after its first two instructions
.equ SYS_MS,          0x500D3C68    # milliseconds since power-on (kernel tick)
.equ CRE_TSK_X,       0x48585D42    # ER cre_tsk_X(d0 = id, a0 = &T_CTSK)
.equ STA_TSK,         0x4C03DCD3    # ER sta_tsk(d0 = id, d1 = start code)
.equ DLY_TSK,         0x4C03E295    # ER dly_tsk(d0 = milliseconds); yields while waiting
.equ FB,              0x9CE00000    # LCD frame, RGB565
.equ FB_STRIDE,       1280          # bytes per row (640 pixels)

# ---- task -------------------------------------------------------------------------
.equ TASK_ID_FIRST,   10            # free slots 10 then 9 (11 would overrun the table)
.equ TASK_ID_LAST,    9
.equ TASK_PRI,        3             # the firmware tasks' priority: round-robin with them
.equ TASK_STACK,      0xC00         # the firmware's smallest task stack
.equ PERIOD_MS,       200

# ---- layout: a 149x29 box in the bottom-right corner ---------------------------
.equ CELL_W,          18            # 5x7 glyph x3 + one blank column x3
.equ CELL_H,          21
.equ BOX_W,           1 + 3 + 8*CELL_W + 1      # border, pad, 8 cells (last blank col = right pad), border
.equ BOX_H,           1 + 3 + CELL_H + 3 + 1
.equ BOX_X,           640 - BOX_W - 2
.equ BOX_Y,           240 - BOX_H - 2
.equ GLYPH_X,         BOX_X + 4
.equ GLYPH_Y,         BOX_Y + 4
.equ CELL0,           FB + GLYPH_Y*FB_STRIDE + GLYPH_X*2
.equ COLOR_FG,        0xFFFF        # white
.equ COLOR_BG,        0x0010        # dark blue
.equ COLOR_BORDER,    0xC618        # light grey
.equ GLYPH_COLON,     10
.equ FRAME_RECTS,     7             # entries in frame_rects

# =================================================================================
# The hook. XapEntry's first 5 bytes are `jmp clock_hook`; we arrive exactly as
# XapEntry would (arguments in d0, d1, a0, a1; return address at (sp)).
clock_hook:
    add     -0x20, sp               # scratch below the caller's (sp) slot
    mov     d0, (0x10, sp)
    mov     d1, (0x14, sp)
    mov     a0, (0x18, sp)
    mov     a1, (0x1c, sp)
    call    clock_start, 0, 0       # preserves d2, d3, a2, a3
    mov     (0x10, sp), d0
    mov     (0x14, sp), d1
    mov     (0x18, sp), a0
    mov     (0x1c, sp), a1
    add     0x20, sp
    movm    [a2,a3], (sp)           # the two instructions the jmp displaced
    add     -8, sp
    jmp     XAP_RESUME

# Create and start the clock task (once per power-on: the firmware loads the base
# plug-in only once). If both slots are taken the clock simply does not start.
clock_start:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x10, sp               # (0..0xF, sp) stay free for callees
    mov     TASK_ID_FIRST, d2
cs_try:
    mov     d2, d0
    mov     clock_ctsk, a0
    mov     CRE_TSK_X, a1
    calls   (a1)
    cmp     0, d0                   # E_OK
    beq     cs_start
    add     -1, d2
    cmp     TASK_ID_LAST, d2
    bge     cs_try
    bra     cs_done
cs_start:
    mov     d2, d0
    clr     d1
    mov     STA_TSK, a1
    calls   (a1)
cs_done:
    ret     [d2,d3,a2,a3], 0x20

# The task: redraw, sleep, forever.
clock_task:
    add     -0x10, sp
ct_loop:
    call    clock_draw, 0, 0
    mov     PERIOD_MS, d0
    mov     DLY_TSK, a1
    calls   (a1)
    bra     ct_loop

# =================================================================================
# Draw the whole clock box. Every pixel is written once with its final colour, so
# there is no flicker from clearing first.
clock_draw:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp               # locals: (0x10..0x17, sp) = the 8 glyph indices

    mov     frame_rects, a2         # border and padding
    mov     FRAME_RECTS, d3
cd_rect:
    movhu   (4, a2), d0
    mov     d0, a0                  # w
    movhu   (6, a2), d0
    mov     d0, a1                  # h
    movhu   (8, a2), d2             # colour
    movhu   (a2), d0                # x
    movhu   (2, a2), d1             # y
    call    fill_rect, 0, 0
    add     10, a2
    add     -1, d3
    bne     cd_rect

    mov     (SYS_MS), d0            # milliseconds since power-on
    mov     1000, d1
    clr     d2
    mov     d2, mdr
    divu    d1, d0                  # d0 = seconds
    mov     60, d1
    clr     d2
    mov     d2, mdr
    divu    d1, d0                  # d0 = minutes, MDR = seconds
    mov     mdr, d3                 # d3 = ss
    clr     d2
    mov     d2, mdr
    divu    d1, d0                  # d0 = hours, MDR = minutes
    mov     mdr, d2                 # d2 = mm
    clr     d1
    mov     d1, mdr
    mov     100, d1
    divu    d1, d0
    mov     mdr, d0                 # d0 = hours modulo 100

    mov     sp, a0
    add     0x10, a0
    call    put2, 0, 0              # HH
    mov     GLYPH_COLON, d1
    movbu   d1, (2, a0)
    mov     d2, d0
    add     3, a0
    call    put2, 0, 0              # MM
    mov     GLYPH_COLON, d1
    movbu   d1, (2, a0)
    mov     d3, d0
    add     3, a0
    call    put2, 0, 0              # SS

    mov     sp, a2
    add     0x10, a2
    call    draw_digits, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# (a0) = d0 / 10, (1, a0) = d0 % 10, for d0 in 0..99. Clobbers d0, d1, MDR.
put2:
    clr     d1
    mov     d1, mdr
    mov     10, d1
    divu    d1, d0
    movbu   d0, (a0)
    mov     mdr, d0
    movbu   d0, (1, a0)
    rets

# Draw 8 glyphs (indices at a2) into the cells of the box.
draw_digits:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp               # (0x10, sp) cells left, (0x14, sp) row address
    mov     CELL0, a3               # top-left pixel of the current cell
    mov     8, d0
    mov     d0, (0x10, sp)
dd_char:
    movbu   (a2), d0                # glyph index
    inc     a2
    mov     d0, d1
    asl     6, d0
    sub     d1, d0                  # index * 63 bytes
    mov     font, a1
    add     d0, a1
    mov     a3, d1
    mov     d1, (0x14, sp)
    mov     CELL_H, d3
dd_row:
    mov     (0x14, sp), a0
    movbu   (a1), d0                # 18 pixels from 3 bytes ...
    asl     8, d0
    movbu   (1, a1), d1
    or      d1, d0
    asl     8, d0
    movbu   (2, a1), d1
    or      d1, d0
    asl     8, d0                   # ... leftmost pixel now in bit 31
    add     3, a1
    mov     CELL_W, d2
dd_px:
    mov     COLOR_BG, d1
    cmp     0, d0
    bge     dd_put
    mov     COLOR_FG, d1
dd_put:
    movhu   d1, (a0)
    add     2, a0
    asl     1, d0
    add     -1, d2
    bne     dd_px
    mov     (0x14, sp), d1
    add     FB_STRIDE, d1
    mov     d1, (0x14, sp)
    add     -1, d3
    bne     dd_row
    add     CELL_W*2, a3
    mov     (0x10, sp), d0
    add     -1, d0
    mov     d0, (0x10, sp)
    bne     dd_char
    ret     [d2,d3,a2,a3], 0x30

# Fill a rectangle: d0 = x, d1 = y, a0 = width, a1 = height, d2 = RGB565 colour.
# Clobbers d0, d1, a0, a1; preserves d2, d3, a2, a3.
fill_rect:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x10, sp
    asl     1, d0                   # 2x
    mov     d1, d3
    asl     8, d3                   # 256y
    add     d3, d0
    asl     2, d3                   # 1024y
    add     d3, d0                  # 2x + 1280y
    mov     FB, a2
    add     d0, a2
    mov     a1, d3                  # rows
fr_row:
    mov     a2, a3
    mov     a0, d1                  # pixels in the row
fr_px:
    movhu   d2, (a3)
    add     2, a3
    add     -1, d1
    bne     fr_px
    add     FB_STRIDE, a2
    add     -1, d3
    bne     fr_row
    ret     [d2,d3,a2,a3], 0x20

# =================================================================================
    .align  4
clock_ctsk:                         # T_CTSK (micro-ITRON 3.0), as the firmware's own
    .long   0                       # exinf
    .long   1                       # tskatr: TA_HLNG, like every firmware task
    .long   clock_task              # task entry
    .word   TASK_PRI                # itskpri
    .word   0
    .long   TASK_STACK              # stksz

frame_rects:                        # x, y, w, h, colour
    .word   BOX_X,           BOX_Y,             BOX_W,     1,         COLOR_BORDER
    .word   BOX_X,           BOX_Y + BOX_H - 1, BOX_W,     1,         COLOR_BORDER
    .word   BOX_X,           BOX_Y + 1,         1,         BOX_H - 2, COLOR_BORDER
    .word   BOX_X + BOX_W - 1, BOX_Y + 1,       1,         BOX_H - 2, COLOR_BORDER
    .word   BOX_X + 1,       BOX_Y + 1,         BOX_W - 2, 3,         COLOR_BG
    .word   BOX_X + 1,       GLYPH_Y + CELL_H,  BOX_W - 2, 3,         COLOR_BG
    .word   BOX_X + 1,       GLYPH_Y,           3,         CELL_H,    COLOR_BG

    .asciz  "KN7000 uptime clock - homebrew"
font:
    .include "font.inc"
