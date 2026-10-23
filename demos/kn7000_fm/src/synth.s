# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# synth.s -- the main CPU's side of the FM engine: what tools/fm_model.py calls note_words and patch_words, in
# integers on its tables (fm_tables.inc, tools/fm_host.py), so that the DSP gets exactly the words the model
# computes; the bank (DX7 32-voice SysEx dumps, unpacked and packed); the notes, from the keybed's and MIDI IN's
# byte rings; the voices they are given to.
#
# A voice's data is the DX7's unpacked form (VCED, 155 bytes): six operators of 21 bytes, OP6 first, then the
# voice's 19, then its name. The current voice is BANK + 155 x V_PATCH (CurPatch).
#
# Calling convention as the rest of the program: d0, d1, a0, a1 scratch; d2, d3, a2, a3 preserved; a routine that
# calls makes a frame first (movm + add -0x20, sp; locals at 0x14..0x1C, sp).

.equ OP_LEN,        21
.equ VO_PR,         126             # the voice's own bytes in a VCED
.equ VO_PL,         130
.equ VO_ALG,        134
.equ VO_FB,         135
.equ VO_OKS,        136
.equ VO_LFS,        137
.equ VO_LFD,        138
.equ VO_LPMD,       139
.equ VO_LAMD,       140
.equ VO_LFKS,       141
.equ VO_LFW,        142
.equ VO_LPMS,       143
.equ VO_TRNSP,      144
.equ VO_NAME,       145
.equ ST_DOWN,       0x20000000      # fm_model.ST_DOWN

# CurPatch -> a0 = the current voice (BANK + 155 x V_PATCH). Leaf.
CurPatch:
    mov     (V_PATCH), d0
    mov     VCED_LEN, d1
    mulu    d1, d0
    mov     BANK, a0
    add     d0, a0
    rets

# CurAlg -> d0 = the current voice's algorithm, 0-31. Leaf.
CurAlg:
    mov     (V_PATCH), d0
    mov     VCED_LEN, d1
    mulu    d1, d0
    mov     BANK + VO_ALG, a0
    add     d0, a0
    movbu   (a0), d0
    and     31, d0
    rets

# ScaleOutLevel(d0 = output level 0-99) -> d0: msfa Env::scaleoutlevel. Leaf.
ScaleOutLevel:
    cmp     20, d0
    bcs     sol_lut
    add     28, d0
    rets
sol_lut:
    mov     T_LEVELLUT, a0
    add     d0, a0
    movbu   (a0), d0
    rets

# ==============================================================================================================
# NoteWords(d0 = key, d1 = velocity) -> STAGEBUF: the key-on's 18 words (fm_model.note_words) for the current voice.
.equ NW_NOTE,       0x14
.equ NW_VEL,        0x18
NoteWords:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d1, (NW_VEL, sp)
    mov     d0, d2
    call    CurPatch, 0, 0
    mov     a0, a2                  # a2 = the voice
    movbu   (VO_TRNSP, a2), d0      # the note: key + transpose - 24, 0-127
    add     d0, d2
    add     -24, d2
    cmp     0, d2
    bge     nw_lo
    clr     d2
nw_lo:
    cmp     127, d2
    ble     nw_hi
    mov     127, d2
nw_hi:
    mov     d2, (NW_NOTE, sp)
    clr     d3                      # d3 = the operator, 0 = OP6
nw_op:
    mov     d3, d0                  # a3 = its bytes
    mov     OP_LEN, d1
    mulu    d1, d0
    mov     a2, a3
    add     d0, a3
    movbu   (16, a3), d0            # the output level
    call    ScaleOutLevel, 0, 0
    mov     d0, d2
    mov     (NW_NOTE, sp), d0
    mov     a3, a0
    call    ScaleLevel, 0, 0        # keyboard level scaling
    add     d0, d2
    cmp     127, d2
    ble     nw_ol
    mov     127, d2
nw_ol:
    asl     5, d2
    mov     (NW_VEL, sp), d0
    movbu   (15, a3), d1
    call    ScaleVelocity, 0, 0
    add     d0, d2
    cmp     0, d2
    bge     nw_olpos
    clr     d2
nw_olpos:
    asl     8, d2                   # OLRS = level << 8 | rate scaling
    mov     (NW_NOTE, sp), d0
    movbu   (13, a3), d1
    call    ScaleRate, 0, 0
    or      d0, d2
    mov     d3, d1
    asl     2, d1
    mov     STAGEBUF + 24, a0
    add     d1, a0
    mov     d2, (a0)
    mov     (NW_NOTE, sp), d0       # PITCH
    mov     a3, a0
    call    OscFreq, 0, 0
    mov     d3, d1
    asl     2, d1
    mov     STAGEBUF, a0
    add     d1, a0
    mov     d0, (a0)
    mov     ST_DOWN, d2             # STATE: stage 0, the key held; L1 = 0: the static count
    movbu   (4, a3), d0
    cmp     0, d0
    bne     nw_state
    movbu   (a3), d0                # min(R1 + rate scaling, 99)
    mov     d3, d1
    asl     2, d1
    mov     STAGEBUF + 24, a0
    add     d1, a0
    mov     (a0), d1
    and     0xFF, d1
    add     d1, d0
    cmp     99, d0
    bls     nw_r
    mov     99, d0
nw_r:
    mov     d0, (0x1C, sp)
    asl     2, d0
    mov     T_STATICS, a0
    add     d0, a0
    mov     (a0), d0
    mov     (0x1C, sp), d1
    cmp     77, d1
    bcc     nw_st
    clr     d1                      # rate < 77: a twentieth
    mov     d1, mdr
    mov     20, d1
    divu    d1, d0
nw_st:
    or      d0, d2
nw_state:
    mov     d3, d1
    asl     2, d1
    mov     STAGEBUF + 48, a0
    add     d1, a0
    mov     d2, (a0)
    inc     d3
    cmp     6, d3
    bcs     nw_op
    ret     [d2,d3,a2,a3], 0x30

# ScaleLevel(d0 = note, a0 = the operator's bytes) -> d0: msfa ScaleLevel (BP 8, LD 9, RD 10, LC 11, RC 12), the
# depth kept in V_TMP. Leaf.
ScaleLevel:
    movbu   (8, a0), d1
    sub     d1, d0
    add     -17, d0                 # offset = note - BP - 17
    cmp     0, d0
    blt     sl_left
    inc     d0                      # group = (offset + 1) / 3, depth RD, curve RC
    movbu   (10, a0), d1
    mov     d1, (V_TMP)
    movbu   (12, a0), d1
    bra     sl_div
sl_left:
    mov     1, d1                   # group = (1 - offset) / 3, depth LD, curve LC
    sub     d0, d1
    mov     d1, d0
    movbu   (9, a0), d1
    mov     d1, (V_TMP)
    movbu   (11, a0), d1
sl_div:
    mov     d1, a1                  # a1 = the curve
    clr     d1
    mov     d1, mdr
    mov     3, d1
    divu    d1, d0                  # d0 = group (>= 0)
    mov     a1, d1
    cmp     0, d1
    beq     sl_lin
    cmp     3, d1
    beq     sl_lin
    cmp     32, d0                  # exponential: EXP_SCALE[min(group, 32)] x depth x 329 >> 15
    bls     sl_exp
    mov     32, d0
sl_exp:
    mov     T_EXPSCALE, a0
    add     d0, a0
    movbu   (a0), d0
    mov     (V_TMP), d1
    mulu    d1, d0
    mov     329, d1
    mulu    d1, d0
    lsr     15, d0
    bra     sl_sign
sl_lin:
    mov     (V_TMP), d1             # linear: group x depth x 329 >> 12
    mulu    d1, d0
    mov     329, d1
    mulu    d1, d0
    lsr     12, d0
sl_sign:
    mov     a1, d1
    cmp     2, d1
    bcc     sl_ret
    clr     d1                      # curves 0 and 1 (-LIN, -EXP) go down
    sub     d0, d1
    mov     d1, d0
sl_ret:
    rets

# ScaleVelocity(d0 = velocity, d1 = sensitivity 0-7) -> d0: msfa ScaleVelocity. Leaf.
ScaleVelocity:
    cmp     127, d0
    bls     sv_ok
    mov     127, d0
sv_ok:
    lsr     1, d0
    mov     T_VELOCITY, a0
    add     d0, a0
    movbu   (a0), d0
    add     -239, d0                # vel_value
    mul     d1, d0                  # x sensitivity
    add     7, d0
    asr     3, d0
    asl     4, d0
    rets

# ScaleRate(d0 = note, d1 = rate scaling 0-7) -> d0: msfa ScaleRate, (RS x min(31, max(0, note / 3 - 7))) >> 3.
# Leaf.
ScaleRate:
    mov     d1, a0
    clr     d1
    mov     d1, mdr
    mov     3, d1
    divu    d1, d0
    add     -7, d0
    cmp     0, d0
    bge     sr_lo
    clr     d0
sr_lo:
    cmp     31, d0
    ble     sr_hi
    mov     31, d0
sr_hi:
    mov     a0, d1
    mulu    d1, d0
    lsr     3, d0
    rets

# OscFreq(d0 = note, a0 = the operator's bytes) -> d0: its log frequency, Q24 octaves (fm_model.osc_freq: MODE 17,
# FC 18, FF 19, DET 20). Leaf.
OscFreq:
    movbu   (17, a0), d1
    cmp     0, d1
    bne     of_fixed
    mov     a0, a1                  # ratio: PITCH_BASE + note x PITCH_STEP + detune + coarse + fine
    mov     d0, a0                  # a0 = note
    mov     PITCH_STEP, d1
    mulu    d1, d0
    add     PITCH_BASE, d0
    mov     d0, (V_TMP)
    mov     a0, d0                  # detune: DETUNE_Q[note] x (DET - 7) / 65536, towards zero
    asl     2, d0
    mov     T_DETUNE, a0
    add     d0, a0
    mov     (a0), d0
    movbu   (20, a1), d1
    add     -7, d1
    cmp     0, d1
    blt     of_dneg
    mulu    d1, d0
    lsr     16, d0
    bra     of_dadd
of_dneg:
    mov     d0, a0                  # a0 = DETUNE_Q[note]
    clr     d0
    sub     d1, d0                  # d0 = 7 - DET
    mov     a0, d1
    mulu    d1, d0
    lsr     16, d0
    clr     d1
    sub     d0, d1
    mov     d1, d0
of_dadd:
    mov     (V_TMP), d1
    add     d1, d0
    mov     d0, (V_TMP)
    movbu   (18, a1), d0            # coarse
    and     31, d0
    asl     2, d0
    mov     T_COARSE, a0
    add     d0, a0
    mov     (a0), d0
    mov     (V_TMP), d1
    add     d1, d0
    mov     d0, (V_TMP)
    movbu   (19, a1), d0            # fine
    asl     2, d0
    mov     T_FINE, a0
    add     d0, a0
    mov     (a0), d0
    mov     (V_TMP), d1
    add     d1, d0
    rets
of_fixed:
    movbu   (18, a0), d0            # fixed: (4458616 x ((FC & 3) x 100 + FF)) >> 3, + 13457 x (DET - 7) above 7
    and     3, d0
    mov     100, d1
    mulu    d1, d0
    movbu   (19, a0), d1
    add     d1, d0
    mov     4458616, d1
    mulu    d1, d0
    lsr     3, d0
    movbu   (20, a0), d1
    add     -7, d1
    cmp     0, d1
    ble     of_ret
    mov     d0, a1
    mov     13457, d0
    mulu    d1, d0
    mov     a1, d1
    add     d1, d0
of_ret:
    rets

# ==============================================================================================================
# PatchImage -> PIMG: the current voice's DSP words (fm_model.patch_words, as tools/fm_dsp.patch_image lays them
# out): per operator its four envelope words, then per operator GSHX, MODE, AMS, NC; the pitch envelope's rates
# and levels; PMDS, AMD, the LFO's step and delay steps, its wave and sync, OKS, ENDW, FBKM, FBKC.
.equ PI_ENDW,       0x14
.equ PI_ALG,        0x18
PatchImage:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    call    CurPatch, 0, 0
    mov     a0, a2                  # a2 = the voice
    movbu   (VO_ALG, a2), d0
    and     31, d0
    mov     6, d1
    mulu    d1, d0
    mov     d0, (PI_ALG, sp)        # its row of T_ALGFLAGS
    mov     -1, d0
    mov     d0, (PI_ENDW, sp)
    mov     PIMG, a3                # a3 = the next word
    clr     d3                      # the envelopes: operator d3, stage d2
pi_envop:
    clr     d2
pi_env:
    mov     d3, d0
    mov     OP_LEN, d1
    mulu    d1, d0
    add     d2, d0
    mov     a2, a0
    add     d0, a0                  # a0 = &R(stage)
    movbu   (4, a0), d0             # the level: ((scaleoutlevel(L) >> 1) << 6) - 4256, bits 16-31
    mov     a0, (0x1C, sp)
    call    ScaleOutLevel, 0, 0
    lsr     1, d0
    asl     6, d0
    add     -4256, d0
    asl     16, d0
    mov     d0, (a3)
    mov     (0x1C, sp), a0
    movbu   (a0), d0                # the rate: qrate (R x 41 >> 6) at bits 8-13, R at 0-6
    mov     d0, d1
    mov     41, d0
    mulu    d1, d0
    lsr     6, d0
    asl     8, d0
    or      d1, d0
    mov     (a3), d1
    or      d1, d0
    mov     d0, (a3)
    add     4, a3
    inc     d2
    cmp     4, d2
    bcs     pi_env
    inc     d3
    cmp     6, d3
    bcs     pi_envop
    clr     d3                      # per operator: GSHX, MODE, AMS, NC
pi_opw:
    mov     (PI_ALG, sp), d0
    add     d3, d0
    mov     T_ALGFLAGS, a0
    add     d0, a0
    movbu   (a0), d0
    and     3, d0                   # 0: a carrier
    mov     GAIN_EXP + GSHIFT_CAR, d1
    cmp     0, d0
    beq     pi_gsh
    mov     GAIN_EXP + GSHIFT_MOD, d1
pi_gsh:
    mov     d1, (a3)
    mov     d3, d0
    mov     OP_LEN, d1
    mulu    d1, d0
    mov     a2, a0
    add     d0, a0                  # a0 = the operator's bytes
    movbu   (17, a0), d0            # MODE
    mov     d0, (4, a3)
    movbu   (14, a0), d0            # AMS -> msfa's ampmodsenstab
    and     3, d0
    asl     2, d0
    mov     T_AMSENS, a1
    add     d0, a1
    mov     (a1), d0
    mov     d0, (8, a3)
    mov     (a3), d0                # NC: 0 for a carrier, else bits 26 and 28
    clr     d1
    cmp     GAIN_EXP + GSHIFT_CAR, d0
    beq     pi_car
    mov     0x14000000, d1
    mov     d1, (12, a3)
    bra     pi_opnext
pi_car:
    mov     d1, (12, a3)
    movbu   (7, a0), d0             # a carrier with L4 > 0: the voice never ends by itself
    cmp     0, d0
    beq     pi_opnext
    clr     d0
    mov     d0, (PI_ENDW, sp)
pi_opnext:
    add     16, a3
    inc     d3
    cmp     6, d3
    bcs     pi_opw
    clr     d3                      # the pitch envelope: rates, then levels
pi_pegr:
    mov     a2, a0
    add     d3, a0
    movbu   (VO_PR, a0), d0
    asl     2, d0
    mov     T_PEGRATE, a1
    add     d0, a1
    mov     (a1), d0
    mov     d0, (a3)
    movbu   (VO_PL, a0), d0
    asl     2, d0
    mov     T_PEGLEVEL, a1
    add     d0, a1
    mov     (a1), d0
    mov     d0, (16, a3)
    add     4, a3
    inc     d3
    cmp     4, d3
    bcs     pi_pegr
    add     16, a3
    movbu   (VO_LPMD, a2), d0       # PMDS = ((LPMD x 165) >> 6) x pitchmodsenstab[LPMS]
    mov     165, d1
    mulu    d1, d0
    lsr     6, d0
    movbu   (VO_LPMS, a2), d1
    and     7, d1
    asl     2, d1
    mov     T_PMSENS, a1
    add     d1, a1
    mov     (a1), d1
    mulu    d1, d0
    mov     d0, (a3)
    movbu   (VO_LAMD, a2), d0       # AMD = (LAMD x 165) >> 6
    mov     165, d1
    mulu    d1, d0
    lsr     6, d0
    mov     d0, (4, a3)
    movbu   (VO_LFS, a2), d0        # the LFO's step
    asl     2, d0
    mov     T_LFODELTA, a1
    add     d0, a1
    mov     (a1), d0
    mov     d0, (8, a3)
    movbu   (VO_LFD, a2), d1        # its delay (msfa Lfo::reset): a = 99 - LFD
    mov     99, d0
    sub     d1, d0
    cmp     99, d0
    bne     pi_delay
    mov     0xFFFFFF, d0            # no delay: both steps the largest
    mov     d0, (12, a3)
    mov     d0, (16, a3)
    bra     pi_wave
pi_delay:
    mov     d0, d2                  # a = (16 + (a & 15)) << (1 + (a >> 4))
    and     15, d0
    add     16, d0
    lsr     4, d2
    inc     d2
    asl     d2, d0
    mov     d0, d2
    mov     LFO_UNIT, d1
    mulu    d1, d0
    lsr     8, d0
    mov     d0, (12, a3)
    mov     0xFF80, d0              # a & 0xff80, at least 0x80
    and     d0, d2
    cmp     0x80, d2
    bcc     pi_d2
    mov     0x80, d2
pi_d2:
    mov     d2, d0
    mov     LFO_UNIT, d1
    mulu    d1, d0
    lsr     8, d0
    mov     d0, (16, a3)
pi_wave:
    movbu   (VO_LFW, a2), d0
    mov     d0, (20, a3)
    movbu   (VO_LFKS, a2), d0
    mov     d0, (24, a3)
    movbu   (VO_OKS, a2), d0
    mov     d0, (28, a3)
    mov     (PI_ENDW, sp), d0
    mov     d0, (32, a3)
    movbu   (VO_FB, a2), d1         # feedback: FBKM = 1 << (22 + fb), FBKC = 1 << (23 + fb), 0 when fb = 0
    and     7, d1
    clr     d0
    mov     d0, (36, a3)
    mov     d0, (40, a3)
    cmp     0, d1
    beq     pi_ret
    add     22, d1
    mov     1, d0
    asl     d1, d0
    mov     d0, (36, a3)
    add     d0, d0
    mov     d0, (40, a3)
pi_ret:
    ret     [d2,d3,a2,a3], 0x30

# ==============================================================================================================
# The bank. A DX7 32-voice dump: F0 43 0n 09 20 00, 4,096 bytes (32 voices of 128, VMEM), the checksum, F7.

# BankUnpack(a0 = 4,096 bytes of VMEM) -> BANK: the 32 voices unpacked (fm_model.Patch.from_packed), every field
# limited to the DX7's range (clamp_voice): a damaged bank must not crash anything.
BankUnpack:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a2                  # a2 = the packed voice
    mov     BANK, a3                # a3 = the unpacked one
    mov     32, d0
    mov     d0, (0x14, sp)
bu_voice:
    clr     d3                      # the operators
bu_op:
    clr     d2                      # bytes 0-10 as they are
bu_cpy:
    mov     a2, a0
    add     d2, a0
    movbu   (a0), d0
    mov     a3, a0
    add     d2, a0
    movbu   d0, (a0)
    inc     d2
    cmp     11, d2
    bcs     bu_cpy
    movbu   (11, a2), d0            # LC, RC
    mov     d0, d1
    and     3, d0
    movbu   d0, (11, a3)
    lsr     2, d1
    and     3, d1
    movbu   d1, (12, a3)
    movbu   (12, a2), d0            # RS, DET
    mov     d0, d1
    and     7, d0
    movbu   d0, (13, a3)
    lsr     3, d1
    and     15, d1
    movbu   d1, (20, a3)
    movbu   (13, a2), d0            # AMS, KVS
    mov     d0, d1
    and     3, d0
    movbu   d0, (14, a3)
    lsr     2, d1
    and     7, d1
    movbu   d1, (15, a3)
    movbu   (14, a2), d0            # OL
    movbu   d0, (16, a3)
    movbu   (15, a2), d0            # MODE, FC
    mov     d0, d1
    and     1, d0
    movbu   d0, (17, a3)
    lsr     1, d1
    and     31, d1
    movbu   d1, (18, a3)
    movbu   (16, a2), d0            # FF
    movbu   d0, (19, a3)
    add     17, a2
    add     OP_LEN, a3
    inc     d3
    cmp     6, d3
    bcs     bu_op
    clr     d2                      # the voice: PR1-4, PL1-4
bu_vcpy:
    mov     a2, a0
    add     d2, a0
    movbu   (a0), d0
    mov     a3, a0
    add     d2, a0
    movbu   d0, (a0)
    inc     d2
    cmp     8, d2
    bcs     bu_vcpy
    movbu   (8, a2), d0             # ALG
    and     31, d0
    movbu   d0, (8, a3)
    movbu   (9, a2), d0             # FB, OKS
    mov     d0, d1
    and     7, d0
    movbu   d0, (9, a3)
    lsr     3, d1
    and     1, d1
    movbu   d1, (10, a3)
    movbu   (10, a2), d0            # LFS, LFD, LPMD, LAMD
    movbu   d0, (11, a3)
    movbu   (11, a2), d0
    movbu   d0, (12, a3)
    movbu   (12, a2), d0
    movbu   d0, (13, a3)
    movbu   (13, a2), d0
    movbu   d0, (14, a3)
    movbu   (14, a2), d0            # LFKS, LFW, LPMS
    mov     d0, d1
    and     1, d0
    movbu   d0, (15, a3)
    mov     d1, d0
    lsr     1, d0
    and     7, d0
    movbu   d0, (16, a3)
    lsr     4, d1
    and     7, d1
    movbu   d1, (17, a3)
    movbu   (15, a2), d0            # TRNSP
    movbu   d0, (18, a3)
    clr     d2                      # the name
bu_name:
    mov     a2, a0
    add     d2, a0
    movbu   (16, a0), d0
    mov     a3, a0
    add     d2, a0
    movbu   d0, (19, a0)
    inc     d2
    cmp     10, d2
    bcs     bu_name
    add     -VO_PR, a3              # back to the voice's start, to clamp it
    mov     a3, a0
    call    ClampVoice, 0, 0
    add     VCED_LEN, a3
    add     26, a2
    mov     (0x14, sp), d0
    add     -1, d0
    mov     d0, (0x14, sp)
    bne     bu_voice
    ret     [d2,d3,a2,a3], 0x30

# ClampVoice(a0 = a VCED): every field at most the DX7's maximum (T_OPMAX, T_VOICEMAX), the name printable. Leaf.
ClampVoice:
    clr     d1                      # the operators' 126 bytes: field = byte index mod 21
cv_op:
    mov     d1, d0
    mov     a0, (V_TMP2)
    mov     T_OPMAX, a1
cv_mod:
    cmp     OP_LEN, d0
    bcs     cv_opf
    add     -OP_LEN, d0
    bra     cv_mod
cv_opf:
    add     d0, a1
    movbu   (a1), d0                # the maximum
    mov     (V_TMP2), a1
    add     d1, a1
    mov     d1, (V_TMP)
    movbu   (a1), d1
    cmp     d0, d1
    bls     cv_opok
    movbu   d0, (a1)
cv_opok:
    mov     (V_TMP), d1
    inc     d1
    cmp     VO_PR, d1
    bcs     cv_op
    clr     d1                      # the voice's 19
cv_v:
    mov     T_VOICEMAX, a1
    add     d1, a1
    movbu   (a1), d0
    mov     a0, a1
    add     d1, a1
    mov     d1, (V_TMP)
    movbu   (VO_PR, a1), d1
    cmp     d0, d1
    bls     cv_vok
    movbu   d0, (VO_PR, a1)
cv_vok:
    mov     (V_TMP), d1
    inc     d1
    cmp     19, d1
    bcs     cv_v
    clr     d1                      # the name: 32-126, else a space
cv_n:
    mov     a0, a1
    add     d1, a1
    movbu   (VO_NAME, a1), d0
    cmp     32, d0
    bcs     cv_sp
    cmp     127, d0
    bcs     cv_nok
cv_sp:
    mov     32, d0
    movbu   d0, (VO_NAME, a1)
cv_nok:
    inc     d1
    cmp     10, d1
    bcs     cv_n
    rets

# BankFromFile(d0 = bytes in FILEBUF) -> d0 = 0 when it held a 32-voice bank (a DX7 bulk dump, or a bare 4,096-byte
# VMEM image), now unpacked into BANK; else 1, BANK untouched.
BankFromFile:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     FILEBUF, a0
    cmp     4096, d0
    beq     bff_go
    cmp     4102, d0                # at least the header and the body
    bcs     bff_bad
    movbu   (a0), d1
    cmp     0xF0, d1
    bne     bff_bad
    movbu   (1, a0), d1
    cmp     0x43, d1
    bne     bff_bad
    movbu   (3, a0), d1
    cmp     0x09, d1
    bne     bff_bad
    add     6, a0
bff_go:
    call    BankUnpack, 0, 0
    clr     d0
    bra     bff_ret
bff_bad:
    mov     1, d0
bff_ret:
    ret     [d2,d3,a2,a3], 0x30

# BankToFile -> FILEBUF: BANK as a DX7 32-voice dump (SYX_LEN bytes; fm_model.Patch.packed, Bank.sysex).
BankToFile:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     FILEBUF, a0
    mov     0xF0, d0
    movbu   d0, (a0)
    mov     0x43, d0
    movbu   d0, (1, a0)
    clr     d0
    movbu   d0, (2, a0)
    mov     0x09, d0
    movbu   d0, (3, a0)
    mov     0x20, d0
    movbu   d0, (4, a0)
    clr     d0
    movbu   d0, (5, a0)
    mov     BANK, a2                # a2 = the unpacked voice
    mov     FILEBUF + 6, a3         # a3 = the packed one
    mov     32, d0
    mov     d0, (0x14, sp)
bt_voice:
    clr     d3
bt_op:
    clr     d2
bt_cpy:
    mov     a2, a0
    add     d2, a0
    movbu   (a0), d0
    mov     a3, a0
    add     d2, a0
    movbu   d0, (a0)
    inc     d2
    cmp     11, d2
    bcs     bt_cpy
    movbu   (12, a2), d0            # RC << 2 | LC
    asl     2, d0
    movbu   (11, a2), d1
    or      d1, d0
    movbu   d0, (11, a3)
    movbu   (20, a2), d0            # DET << 3 | RS
    asl     3, d0
    movbu   (13, a2), d1
    or      d1, d0
    movbu   d0, (12, a3)
    movbu   (15, a2), d0            # KVS << 2 | AMS
    asl     2, d0
    movbu   (14, a2), d1
    or      d1, d0
    movbu   d0, (13, a3)
    movbu   (16, a2), d0            # OL
    movbu   d0, (14, a3)
    movbu   (18, a2), d0            # FC << 1 | MODE
    add     d0, d0
    movbu   (17, a2), d1
    or      d1, d0
    movbu   d0, (15, a3)
    movbu   (19, a2), d0            # FF
    movbu   d0, (16, a3)
    add     OP_LEN, a2
    add     17, a3
    inc     d3
    cmp     6, d3
    bcs     bt_op
    clr     d2                      # PR1-4, PL1-4, ALG
bt_vcpy:
    mov     a2, a0
    add     d2, a0
    movbu   (a0), d0
    mov     a3, a0
    add     d2, a0
    movbu   d0, (a0)
    inc     d2
    cmp     9, d2
    bcs     bt_vcpy
    movbu   (10, a2), d0            # OKS << 3 | FB
    asl     3, d0
    movbu   (9, a2), d1
    or      d1, d0
    movbu   d0, (9, a3)
    movbu   (11, a2), d0            # LFS, LFD, LPMD, LAMD
    movbu   d0, (10, a3)
    movbu   (12, a2), d0
    movbu   d0, (11, a3)
    movbu   (13, a2), d0
    movbu   d0, (12, a3)
    movbu   (14, a2), d0
    movbu   d0, (13, a3)
    movbu   (17, a2), d0            # LPMS << 4 | LFW << 1 | LFKS
    asl     3, d0
    movbu   (16, a2), d1
    or      d1, d0
    add     d0, d0
    movbu   (15, a2), d1
    or      d1, d0
    movbu   d0, (14, a3)
    movbu   (18, a2), d0            # TRNSP
    movbu   d0, (15, a3)
    clr     d2
bt_name:
    mov     a2, a0
    add     d2, a0
    movbu   (19, a0), d0
    mov     a3, a0
    add     d2, a0
    movbu   d0, (16, a0)
    inc     d2
    cmp     10, d2
    bcs     bt_name
    add     VCED_LEN - VO_PR, a2
    add     26, a3
    mov     (0x14, sp), d0
    add     -1, d0
    mov     d0, (0x14, sp)
    bne     bt_voice
    mov     FILEBUF + 6, a0         # the checksum: -(the body's sum), 7 bits
    clr     d1
    mov     4096, d2
bt_sum:
    movbu   (a0), d0
    add     d0, d1
    inc     a0
    add     -1, d2
    bne     bt_sum
    clr     d0
    sub     d1, d0
    and     0x7F, d0
    movbu   d0, (a0)
    mov     0xF7, d0
    movbu   d0, (1, a0)
    ret     [d2,d3,a2,a3], 0x30

# ==============================================================================================================
# Notes. The keybed's notes reach the MIDI OUT 1 transmit ring (Another World's port reads its input there),
# MIDI IN's the MIDI IN 1 receive ring; each is read from a read pointer of our own (the firmware's consumer is
# never touched; Another World's interp.s NotePollSource, here giving events instead of a held-notes mask).

# NotesInit: both parsers pointed at their rings, read pointers 0: the first poll starts from the newest byte.
NotesInit:
    mov     (FWV_MIDIOUT1), d0
    mov     d0, (NOTES_KBD + NS_HDR)
    mov     (FWV_MIDIIN1), d0
    mov     d0, (NOTES_MIDI + NS_HDR)
    clr     d0
    mov     d0, (NOTES_KBD + NS_RP)
    mov     d0, (NOTES_MIDI + NS_RP)
    rets

# NotePoll: both rings' new bytes, as MIDI.
NotePoll:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     NOTES_KBD, a0
    call    NotePollSource, 0, 0
    mov     NOTES_MIDI, a0
    call    NotePollSource, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# NotePollSource(a0 = parser): its ring's new bytes from its own read pointer: note-on (velocity 0 = note-off),
# note-off, CC 64 (sustain) and CC 120/123 (all notes off). Running status as MIDI has it: realtime bytes skipped,
# system bytes cancel it. A read pointer outside the ring, or a backlog of more than half the ring (the ring was
# reset under us), starts again from the newest byte.
.equ NP_WR,         0x14            # the producer's write pointer, sampled once
NotePollSource:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     a0, a3                  # a3 = the parser
    mov     (NS_HDR, a3), a0
    cmp     0, a0
    beq     nps_ret
    mov     (QH_START, a0), d2      # d2 = start
    mov     (QH_LAST, a0), d3       # d3 = last
    mov     (QH_WR, a0), d1
    mov     d1, (NP_WR, sp)
    cmp     0, d2
    beq     nps_ret
    cmp     d2, d1
    blt     nps_ret
    cmp     d3, d1
    bgt     nps_ret
    mov     (NS_RP, a3), a2         # a2 = our read pointer
    mov     a2, d0
    cmp     d2, d0
    blt     nps_resync
    cmp     d3, d0
    bgt     nps_resync
    sub     d0, d1                  # the backlog
    bge     nps_nowrap
    add     d3, d1
    sub     d2, d1
    inc     d1
nps_nowrap:
    mov     d3, d0
    sub     d2, d0
    lsr     1, d0
    cmp     d0, d1
    bgt     nps_resync
nps_loop:
    mov     a2, d0
    mov     (NP_WR, sp), d1
    cmp     d1, d0
    beq     nps_done
    movbu   (a2), d0
    mov     a2, d1
    cmp     d3, d1
    bne     nps_inc
    mov     d2, a2
    bra     nps_byte
nps_inc:
    inc     a2
nps_byte:
    cmp     0xF8, d0
    bge     nps_loop                # realtime
    cmp     0xF0, d0
    bge     nps_sys
    cmp     0x80, d0
    bge     nps_status
    movbu   (NS_STATUS, a3), d1     # a data byte
    cmp     0, d1
    beq     nps_loop
    movbu   (NS_CNT, a3), d1
    cmp     0, d1
    bne     nps_second
    movbu   d0, (NS_D1, a3)
    movbu   (NS_STATUS, a3), d1
    and     0xE0, d1                # program change, channel pressure: one data byte
    cmp     0xC0, d1
    beq     nps_loop
    mov     1, d1
    movbu   d1, (NS_CNT, a3)
    bra     nps_loop
nps_sys:
    clr     d1
    movbu   d1, (NS_STATUS, a3)
    movbu   d1, (NS_CNT, a3)
    bra     nps_loop
nps_status:
    movbu   d0, (NS_STATUS, a3)
    clr     d1
    movbu   d1, (NS_CNT, a3)
    bra     nps_loop
nps_second:
    clr     d1
    movbu   d1, (NS_CNT, a3)
    mov     a2, (NS_RP, a3)         # (the events below make calls: the read pointer and the ring's bounds kept)
    mov     d2, (0x18, sp)
    mov     d3, (0x1C, sp)
    mov     d0, d1                  # d1 = the second data byte
    movbu   (NS_D1, a3), d0         # d0 = the first
    movbu   (NS_STATUS, a3), d2
    and     0xF0, d2
    cmp     0x90, d2
    beq     nps_on
    cmp     0x80, d2
    beq     nps_off
    cmp     0xB0, d2
    bne     nps_back
    cmp     64, d0                  # control change: sustain, all notes off
    beq     nps_sus
    cmp     120, d0
    beq     nps_all
    cmp     123, d0
    bne     nps_back
nps_all:
    call    AllNotesOff, 0, 0
    bra     nps_back
nps_sus:
    clr     d0
    cmp     64, d1
    bcs     nps_sus2
    mov     1, d0
nps_sus2:
    call    Sustain, 0, 0
    bra     nps_back
nps_on:
    cmp     0, d1
    beq     nps_off
    call    NoteOn, 0, 0
    bra     nps_back
nps_off:
    call    NoteOff, 0, 0
nps_back:
    mov     (NS_RP, a3), a2
    mov     (0x18, sp), d2
    mov     (0x1C, sp), d3
    bra     nps_loop
nps_done:
    mov     a2, (NS_RP, a3)
nps_ret:
    ret     [d2,d3,a2,a3], 0x30
nps_resync:
    mov     (NP_WR, sp), d0
    mov     d0, (NS_RP, a3)
    clr     d0
    movbu   d0, (NS_STATUS, a3)
    movbu   d0, (NS_CNT, a3)
    bra     nps_ret

# ==============================================================================================================
# The voices. A key gets the voice that already plays it, else a free one, else the one released longest ago,
# else (all held) the one held longest: msfa's Dexed keeps a note per voice the same way.

# VoiceRec(d0 = voice) -> a0 = its record. Leaf.
VoiceRec:
    asl     3, d0
    mov     VOICEV, a0
    add     d0, a0
    rets

# NoteOn(d0 = key, d1 = velocity 1-127)
NoteOn:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, (0x14, sp)
    mov     d1, (0x18, sp)
    mov     (V_AGE), d0
    inc     d0
    mov     d0, (V_AGE)
    clr     d2                      # the voice that plays this key
non_same:
    mov     d2, d0
    call    VoiceRec, 0, 0
    movbu   (VR_STATE, a0), d0
    cmp     VS_FREE, d0
    beq     non_snext
    movbu   (VR_NOTE, a0), d0
    mov     (0x14, sp), d1
    cmp     d1, d0
    beq     non_take
non_snext:
    inc     d2
    mov     (V_NV), d0
    cmp     d0, d2
    bcs     non_same
    mov     VS_FREE, d0             # else the oldest in the first state that has one: free, released,
    call    OldestIn, 0, 0          #   sustained, held
    cmp     0, d0
    bge     non_got
    mov     VS_REL, d0
    call    OldestIn, 0, 0
    cmp     0, d0
    bge     non_got
    mov     VS_SUST, d0
    call    OldestIn, 0, 0
    cmp     0, d0
    bge     non_got
    mov     VS_HELD, d0
    call    OldestIn, 0, 0
    cmp     0, d0
    blt     non_ret
    mov     (V_STEALS), d1
    inc     d1
    mov     d1, (V_STEALS)
non_got:
    mov     d0, d2
non_take:
    mov     d2, d0
    call    VoiceRec, 0, 0
    mov     (0x14, sp), d0
    movbu   d0, (VR_NOTE, a0)
    mov     VS_HELD, d0
    movbu   d0, (VR_STATE, a0)
    mov     (V_AGE), d0
    mov     d0, (VR_AGE, a0)
    mov     (0x14, sp), d0
    mov     (0x18, sp), d1
    call    NoteWords, 0, 0
    mov     d2, d0
    call    DspKeyOn, 0, 0
    mov     (V_NOTES), d0
    inc     d0
    mov     d0, (V_NOTES)
non_ret:
    ret     [d2,d3,a2,a3], 0x30

# OldestIn(d0 = VS_* state) -> d0 = the voice in that state with the smallest age, or -1. Leaf.
OldestIn:
    mov     d0, a1                  # a1 = the state
    mov     -1, d0
    mov     d0, (V_TMP)             # the voice found
    mov     d0, (V_TMP2)            # its age
    clr     d1
oi_loop:
    mov     d1, d0
    asl     3, d0
    mov     VOICEV, a0
    add     d0, a0
    movbu   (VR_STATE, a0), d0
    cmp     a1, d0
    bne     oi_next
    mov     (VR_AGE, a0), d0
    mov     (V_TMP2), a0
    cmp     a0, d0
    bcc     oi_next
    mov     d0, (V_TMP2)
    mov     d1, (V_TMP)
oi_next:
    inc     d1
    mov     (V_NV), d0
    cmp     d0, d1
    bcs     oi_loop
    mov     (V_TMP), d0
    rets

# NoteOff(d0 = key): its held voices released, or kept by the sustain pedal.
NoteOff:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, d3                  # d3 = the key
    clr     d2                      # d2 = the voice
    mov     0, a2                   # a2 = the voices to key off
nof_loop:
    mov     d2, d0
    call    VoiceRec, 0, 0
    movbu   (VR_STATE, a0), d0
    cmp     VS_HELD, d0
    bne     nof_next
    movbu   (VR_NOTE, a0), d0
    cmp     d3, d0
    bne     nof_next
    mov     (V_SUSTAIN), d0
    cmp     0, d0
    beq     nof_rel
    mov     VS_SUST, d0
    movbu   d0, (VR_STATE, a0)
    bra     nof_next
nof_rel:
    mov     VS_REL, d0
    movbu   d0, (VR_STATE, a0)
    mov     (V_AGE), d0
    inc     d0
    mov     d0, (V_AGE)
    mov     d0, (VR_AGE, a0)
    mov     1, d0
    asl     d2, d0
    mov     a2, d1
    or      d0, d1
    mov     d1, a2
nof_next:
    inc     d2
    mov     (V_NV), d0
    cmp     d0, d2
    bcs     nof_loop
    mov     a2, d0
    call    DspKeyOff, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# Sustain(d0 = 1 pedal down, 0 up): up releases every voice the pedal kept.
Sustain:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, (V_SUSTAIN)
    cmp     0, d0
    bne     sus_ret
    mov     VS_SUST, d0
    call    ReleaseState, 0, 0
sus_ret:
    ret     [d2,d3,a2,a3], 0x30

# AllNotesOff: every voice released.
AllNotesOff:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     VS_SUST, d0
    call    ReleaseState, 0, 0
    mov     VS_HELD, d0
    call    ReleaseState, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# ReleaseState(d0 = VS_*): every voice in that state released (VS_REL), keyed off together.
ReleaseState:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x20, sp
    mov     d0, d3
    clr     d2
    mov     0, a2
rst_loop:
    mov     d2, d0
    call    VoiceRec, 0, 0
    movbu   (VR_STATE, a0), d0
    cmp     d3, d0
    bne     rst_next
    mov     VS_REL, d0
    movbu   d0, (VR_STATE, a0)
    mov     (V_AGE), d0
    inc     d0
    mov     d0, (V_AGE)
    mov     d0, (VR_AGE, a0)
    mov     1, d0
    asl     d2, d0
    mov     a2, d1
    or      d0, d1
    mov     d1, a2
rst_next:
    inc     d2
    mov     (V_NV), d0
    cmp     d0, d2
    bcs     rst_loop
    mov     a2, d0
    call    DspKeyOff, 0, 0
    ret     [d2,d3,a2,a3], 0x30

# ==============================================================================================================
# The keybed's own sound: muted with expression (CC 11) 0 on MIDI channels 1-3, the panel's parts, put into the
# MIDI IN 1 receive ring as if received; back to 127 at the end (Another World's audio.s KeysSync, MidiPut).

# MidiPut(d0 = status | data1 << 8 | data2 << 16): one three-byte channel message into the MIDI IN 1 receive ring,
# whole or not at all (V_DROPS counts what did not fit), with interrupts off as the ISR's own enqueue has them.
MidiPut:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x10, sp
    mov     (FWV_MIDIIN1), a0
    cmp     0, a0
    beq     mp_ret
    mov     3, d2
    mov     psw, d1
    mov     d1, (0, sp)
    and     0xF7FF, psw
    nop
    nop
    movhu   (QH_FREE, a0), d1
    cmp     d2, d1
    bcs     mp_full
    sub     d2, d1
    movhu   d1, (QH_FREE, a0)
    mov     (QH_WR, a0), a1
    mov     (QH_LAST, a0), d3
mp_byte:
    movbu   d0, (a1)
    lsr     8, d0
    mov     a1, d1
    cmp     d3, d1
    bne     mp_inc
    mov     (QH_START, a0), a1
    bra     mp_next
mp_inc:
    inc     a1
mp_next:
    add     -1, d2
    bne     mp_byte
    mov     a1, (QH_WR, a0)
    bra     mp_unmask
mp_full:
    mov     (V_DROPS), d1
    add     d2, d1
    mov     d1, (V_DROPS)
mp_unmask:
    mov     (0, sp), d1
    mov     d1, psw
    nop
mp_ret:
    ret     [d2,d3,a2,a3], 0x20

# KeysSync -> d0 = 0 once the instrument has been told V_KEYWANT (expression 0 = muted, 127), else 1 (the ring was
# full: try again later).
KeysSync:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x10, sp
    mov     (V_KEYWANT), d2
    mov     (V_KEYSENT), d0
    cmp     d2, d0
    beq     ks_ok
    mov     (V_DROPS), d3
    mov     127 << 16, a2
    cmp     0, d2
    beq     ks_val
    mov     0, a2
ks_val:
    clr     d2
ks_ch:
    mov     d2, d0
    or      0xB0 | (KEY_CC << 8), d0
    mov     a2, d1
    or      d1, d0
    call    MidiPut, 0, 0
    inc     d2
    cmp     KEY_CHS, d2
    bcs     ks_ch
    mov     (V_DROPS), d0
    cmp     d3, d0
    bne     ks_retry
    mov     (V_KEYWANT), d0
    mov     d0, (V_KEYSENT)
ks_ok:
    clr     d0
    bra     ks_ret
ks_retry:
    mov     1, d0
ks_ret:
    ret     [d2,d3,a2,a3], 0x20

# KeysRestore: the keybed's own sound back on; a few tries 2 ms apart while the ring is full.
KeysRestore:
    movm    [d2,d3,a2,a3], (sp)
    add     -0x10, sp
    clr     d0
    mov     d0, (V_KEYWANT)
    mov     10, d2
kr_try:
    call    KeysSync, 0, 0
    cmp     0, d0
    beq     kr_ret
    add     -1, d2
    beq     kr_ret
    mov     2, d0
    mov     (FW_DLY), a0
    calls   (a0)
    bra     kr_try
kr_ret:
    ret     [d2,d3,a2,a3], 0x20

# ---- data: the DX7's limits per field (fm_model.OP_MAX, VOICE_MAX) ---------------------------------------------
T_OPMAX:
    .byte   99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14
T_VOICEMAX:
    .byte   99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48
    .align  4
