#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# fm_dsp.py -- KN7FM's program for the KN7000's effects DSP (IC306, an ADSP-21065L "SHARC"): the FM engine of
# tools/fm_model.py, word for word, and the include files the main CPU's program (src/kn7fm.s) carries it in.
#
#   python3 -I tools/fm_dsp.py OUT_DIR [--check UNIDASM] [--listing OUT.lst]
# writes OUT_DIR/dsp_equ.inc (addresses, offsets, sizes) and OUT_DIR/dsp_data.inc (the resident program, the 32
# algorithm kernels, the tables). --check runs MAME's SHARC disassembler over every word and compares each line with
# the text it was meant to be: any difference stops the build. tools/fm_sim.py runs these words against
# fm_model.Engine.
#
# It is an effect unit of the firmware's resident kernel, with the rules Another World's mixer
# (technics-anotherworld kn7000/tools/dsp_mixer.py) and DSPTONE established and for the same reasons:
#   * CALLed from the kernel's unit slots 0 (PM 0x8080, the REVERB) and 9 (PM 0x80A0, the CHORUS), returning as every
#     effect does: RTS (DB); R4 = DM(I1, M5), R7 = PM(I12, M8); MODIFY (I8, 0x50);
#   * no M registers; I5 and I7 the only index registers, saved and put back; the unit's return pair DM(0/1, I4),
#     then I4 + 2;
#   * MODE1.ALUSAT is set (adds saturate on the chip, MAME's recompiler wraps them): nothing may overflow, and
#     fm_sim.py stops at any fixed-point add or subtract that does;
#   * only operations both of MAME's SHARC cores implement (an unimplemented one is a fatal error in either): no
#     single-function fractional multiply (the multifunction SSFR form is used, whose ALU half is a harmless add into
#     a dead register), no ASHIFT or FDEP by a register.
# MAME's multifunction SSFR truncates (floor(a b / 2^31)) and its floats are 32-bit; the chip rounds the product and,
# with MODE1.RND32 clear as the kernel leaves it, keeps 40-bit floats. fm_model.py computes what MAME computes; on
# the chip the samples differ in their last bits.
#
# The frame (unit 0, E0; 44,100 a second):
#   1. heartbeat MSGR7 + 1; the unit's input kept;
#   2. commands, when MSGR6 is not 0: bits 8-15 key off voice 0-7 (each operator's state gets "key-off pending",
#      the key flag cleared; the pitch envelope goes to stage 3), bits 0-7 key on voice 0-7 from the staging block
#      STAGE (pitch, output level and rate scaling, envelope state, per operator: fm_model.note_words); then
#      MSGR6 = 0 (the host waits for 0 before staging the next note);
#   3. control: one step of one thing a frame, slot = FRAME mod 64: voice slot >> 3, k = slot & 7; k = 0 the voice's
#      pitch envelope, LFO pitch modulation and end; k = 1-6 operator k - 1's envelope, gain and frequency; slot 63
#      the LFO (fm_model.Engine._control);
#   4. audio: CALL TRAMP, a JUMP the host points at the current algorithm's kernel (PM ALG0 or ALG1, two buffers so
#      that the host can write one while the other runs); the kernel loops over NV voices, the active ones, and
#      leaves the sum of their carriers in R13; MIX = CLIP(sum, 24 bits);
#   5. unit 0's return: its input + MIX (ROUTE bit 0, as Another World's), clipped. Unit 9's (E9): MIX.
import os
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm_model as m  # noqa: E402
from sharc_enc import (Program, alu, fmul, mul_ssi, shift_reg, mf_ssfr, compute, shift_imm, field_imm,  # noqa: E402
                       bit_imm, compute_dm, ureg_move, ureg_imm, dm_read_abs, dm_write_abs, dm_read_off,
                       dm_write_off, jump_abs, rts, modify_imm)

# ---- PM -------------------------------------------------------------------------------------------------------
PM_CODE = 0x8102          # the resident program: PM 0x8102-0x82FF is never written by the firmware (pass 7)
PM_CODE_END = 0x8300
PM_CODE2 = 0x8340         # its second part: PM 0x8340-0x83FF (no record loads it, no instruction refers to it)
PM_CODE2_END = 0x8400
PM_ALG = (0x8E00, 0x8F00) # the algorithm kernel's two buffers (no record loads them, no instruction refers to them)
PM_ALG_LEN = 0x100
PM_SLOT0 = 0x8080         # the kernel's unit-0 CALL slot (REVERB): CALL (0x8400) (DB) on the owner's unit
PM_SLOT9 = 0x80A0         # the kernel's unit-9 CALL slot (CHORUS): CALL (0x8D00) (DB)
DEF_TARGET0 = 0x8400
DEF_TARGET9 = 0x8D00
CALL_DB = 0x06BE04000000  # "CALL (addr) (DB)", unconditional, absolute: the slot word less its target
EPILOGUE = 0x234903800000 # "R4 = DM(I1, M5),  R7 = PM(I12, M8)" (rec05 0x8404, as DSPTONE and Another World)
MSGR4, MSGR5, MSGR6, MSGR7 = 0x0C, 0x0D, 0x0E, 0x0F

# ---- DM -------------------------------------------------------------------------------------------------------
# a voice: 64 words at VOICES + 64 v, addressed from I5 = its block + 32 (type-4 offsets reach -32..31); per operator
# k (OP6 first) the word at field + k
VOICES = 0x9D00
VOICE_LEN = 64
O_PH, O_INC, O_G, O_DG, O_LEVEL, O_STATE, O_PITCH, O_OLRS, O_PMOD = -32, -26, -20, -14, -8, -2, 4, 10, 16
O_FB0, O_FB1, O_ACTIVE, O_PEGL, O_PEGIX = 22, 23, 24, 25, 26
SINE = 0x9B20             # 257 words, Q30 (fm_model.SIN_TABLE)
GLOBALS = 0x9C21          # to 0x9C3F
GLOBAL_NAMES = ["SAVEI5", "SAVEI7", "MIX", "HB9", "ROUTE", "FRAME", "CNT", "NV", "LFO_PH", "LFO_VAL", "LFO_DST",
                "LFO_DLY", "LFO_RAND", "AMOD", "PBASE", "IN_L", "IN_R"]
G = {n: GLOBALS + k for k, n in enumerate(GLOBAL_NAMES)}
GLOBALS_END = GLOBALS + len(GLOBAL_NAMES)
PATCH = 0x9C60            # to 0x9CFF; written by the host at every patch change and edit
P_ENV = PATCH             # 6 x 4: operator k's stage s at P_ENV + 4 k + s (fm_model.env_word)
P_OPW = PATCH + 24        # 6 x 4: operator k's GSHX, MODE, AMS, NC at P_OPW + 4 k + 0..3
OPW_GSHX, OPW_MODE, OPW_AMS, OPW_NC = range(4)
P_PEGR = PATCH + 48       # 4
P_PEGL = PATCH + 52       # 4
PATCH_SCALARS = ["PMDS", "AMD", "LFDELTA", "LFD1", "LFD2", "LFW", "LFSYNC", "OKS", "ENDW", "FBKM", "FBKC"]
P = {n: PATCH + 56 + k for k, n in enumerate(PATCH_SCALARS)}
PATCH_END = PATCH + 56 + len(PATCH_SCALARS)
STATICS = 0x9F00          # 100 words: fm_model.STATICS
STAGE = 0x9F64            # 18 words: a key-on's PITCH, OLRS, STATE per operator (fm_model.note_words)
STAGE_END = STAGE + 18
MASK24 = 0xFFFFFF
FEXT_IDX = (8 << 6) | 16  # FEXT spec: the sine index, bits 16-23 of the phase argument
F32BITS = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]  # noqa: E731


def LD(dst, addr):
    return dm_read_abs(dst, addr)


def ST(addr, src):
    return dm_write_abs(addr, src)


def LDI(dst, off, ireg="I5", comp=None, cond="TRUE"):
    return compute_dm(comp, dst, off, ireg, False, cond)


def STI(off, src, ireg="I5", comp=None, cond="TRUE"):
    return compute_dm(comp, src, off, ireg, True, cond)


def A(op, rn, rx, ry="R0", cond="TRUE"):
    return compute(alu(op, rn, rx, ry), cond)


def IMM(dst, value):
    return ureg_imm(dst, value)


def dummy():
    """the ALU half of a multifunction SSFR whose result nobody reads: R2 = R8 + R12"""
    return ("R2", "R8", "R12")


def mf(rm, rxm, rym):
    ra, rxa, rya = dummy()
    return compute(mf_ssfr(rm, rxm, rym, ra, rxa, rya))


def exp2_core(e):
    """R8 = x (Q24) -> F11 = the cubic 2 ** f (float), R10 = i = x >> 24 (fm_model.exp2_f32 without its SCALB);
    clobbers R8, R12"""
    c1, c2, c3 = m.EXP2_C
    e(shift_imm("ASHIFT", "R10", "R8", -24))
    e(IMM("R12", MASK24))
    e(A("AND", "R8", "R8", "R12"))
    e(IMM("R12", -24))
    e(A("FLOATBY", "F8", "R8", "R12"))
    e(IMM("R11", F32BITS(c3)))
    e(compute(fmul("F11", "F11", "F8")))
    e(IMM("R12", F32BITS(c2)))
    e(A("FADD", "F11", "F11", "F12"))
    e(compute(fmul("F11", "F11", "F8")))
    e(IMM("R12", F32BITS(c1)))
    e(A("FADD", "F11", "F11", "F12"))
    e(compute(fmul("F11", "F11", "F8")))
    e(IMM("R12", F32BITS(1.0)))
    e(A("FADD", "F11", "F11", "F12"))


def env_target(e):
    """R3 = the stage, R14 = 4 k, R11 = the output level -> R7 = the stage's word (P_ENV), R6 = its target level
    (fm_model.Engine._target); clobbers R5, I7"""
    e(A("ADD", "R5", "R14", "R3"))
    e(ureg_move("I7", "R5"))
    e(IMM("R6", 16))                                        # (I7 written: a cycle before its use)
    e(dm_read_off("R7", P_ENV, "I7"))
    e(shift_imm("ASHIFT", "R5", "R7", -16))
    e(A("ADD", "R5", "R5", "R11"))
    e(A("MAX", "R5", "R5", "R6"))
    e(shift_imm("LSHIFT", "R6", "R5", 16))


def program():
    """the resident program: [(pc, word, text)], labels"""
    p = Program(PM_CODE)
    p2 = Program(PM_CODE2)
    e = p.emit

    # ---- E0: unit 0 ---------------------------------------------------------------------------------------
    p.label("E0")
    e(ST(G["SAVEI7"], "I7"))
    e(ST(G["SAVEI5"], "I5"))
    e(dm_read_off("R0", 0x20, "I4"))                        # unit 0's input, left
    e(dm_read_off("R1", 0x21, "I4"))                        # and right
    e(ST(G["IN_L"], "R0"))
    e(ST(G["IN_R"], "R1"))
    e(LD("R2", MSGR7))                                      # heartbeat
    e(A("INC", "R2", "R2"))
    e(ST(MSGR7, "R2"))
    # -- commands
    e(LD("R2", MSGR6))
    e(A("PASS", "R2", "R2"))
    p.jump("ctl", "EQ")
    e(IMM("R3", 1))                                         # this voice's key-on bit
    e(IMM("R4", VOICES + 32))                               # its block + 32
    e(IMM("R6", m.NVOICES))
    p.label("cl")
    e(shift_imm("LSHIFT", "R7", "R3", 8))                   # key off?
    e(A("AND", "R7", "R2", "R7"))
    p.jump("con", "EQ")
    e(ureg_move("I5", "R4"))
    e(IMM("R8", m.ST_PEND | m.ST_DOWN))                     # (I5 written: a cycle before its use)
    e(IMM("R9", m.ST_DOWN))
    for k in range(6):
        e(LDI("R10", O_STATE + k))
        e(A("OR", "R10", "R10", "R8"))
        e(A("XOR", "R10", "R10", "R9"))                     # pending set, key flag clear
        e(STI(O_STATE + k, "R10"))
    e(IMM("R10", 3))
    e(STI(O_PEGIX, "R10"))
    p.label("con")
    e(A("AND", "R7", "R2", "R3"))                           # key on?
    p.jump("cnext", "EQ")
    e(ureg_move("I5", "R4"))
    e(IMM("R0", 0))
    for k in range(6):
        e(LD("R10", STAGE + k))
        e(STI(O_PITCH + k, "R10"))
        e(LD("R10", STAGE + 6 + k))
        e(STI(O_OLRS + k, "R10"))
        e(LD("R10", STAGE + 12 + k))
        e(STI(O_STATE + k, "R10"))
        e(STI(O_LEVEL + k, "R0"))
    e(LD("R10", P["OKS"]))
    e(A("PASS", "R10", "R10"))
    p.jump("noks", "EQ")
    for k in range(6):
        e(STI(O_PH + k, "R0"))
    p.label("noks")
    e(LD("R10", P_PEGL + 3))
    e(STI(O_PEGL, "R10"))
    e(STI(O_PEGIX, "R0"))
    e(IMM("R10", 1))
    e(STI(O_ACTIVE, "R10"))
    e(ST(G["LFO_DST"], "R0"))
    e(LD("R10", P["LFSYNC"]))
    e(A("PASS", "R10", "R10"))
    p.jump("cnext", "EQ")
    e(IMM("R10", 0x7FFFFF))                                 # msfa: (1 << 31) - 1, >> 8
    e(ST(G["LFO_PH"], "R10"))
    p.label("cnext")
    e(shift_imm("LSHIFT", "R3", "R3", 1))
    e(IMM("R10", VOICE_LEN))
    e(A("ADD", "R4", "R4", "R10"))
    e(A("DEC", "R6", "R6"))
    p.jump("cl", "NE")
    e(IMM("R10", 0))
    e(ST(MSGR6, "R10"))                                     # done: the host waits for 0
    # -- control: slot = FRAME & 63
    p.label("ctl")
    e(LD("R0", G["FRAME"]))
    e(A("INC", "R1", "R0"))
    e(ST(G["FRAME"], "R1"))
    e(IMM("R1", 63))
    e(A("AND", "R2", "R0", "R1"))
    e(A("COMP", "R0", "R2", "R1"))
    p.jump("lfo2", "EQ")
    e(shift_imm("LSHIFT", "R3", "R2", -3))                  # the voice
    e(LD("R4", G["NV"]))
    e(A("COMP", "R0", "R3", "R4"))
    p.jump("audio", "GE")
    e(shift_imm("LSHIFT", "R5", "R3", 6))
    e(IMM("R6", VOICES + 32))
    e(A("ADD", "R5", "R5", "R6"))                           # its block + 32
    e(IMM("R6", 7))
    e(A("AND", "R4", "R2", "R6"))                           # k
    p.jump("vctl", "EQ")
    e(A("COMP", "R0", "R4", "R6"))
    p.jump("audio", "EQ")
    e(A("DEC", "R4", "R4"))                                 # the operator
    e(A("ADD", "R5", "R5", "R4"))
    e(ureg_move("I5", "R5"))                                # its words at the voice's offsets
    e(shift_imm("LSHIFT", "R14", "R4", 2))                  # 4 k: its patch words
    p.jump("opctl")
    p.label("vctl")
    e(ureg_move("I5", "R5"))
    p.jump("voicectl")

    # -- audio: the algorithm's kernel over NV voices
    p.label("audio")
    e(IMM("R13", 0))
    e(IMM("R15", MASK24))
    e(IMM("R11", FEXT_IDX))
    e(LD("R0", G["NV"]))
    e(ST(G["CNT"], "R0"))
    e(A("PASS", "R0", "R0"))
    p.jump("mixed", "EQ")
    e(IMM("I5", VOICES + 32))
    p.call("TRAMP")
    p.label("mixed")
    e(IMM("R14", 0x7FFFFF))
    e(A("CLIP", "R13", "R13", "R14"))
    e(ST(G["MIX"], "R13"))
    e(LD("R0", G["IN_L"]))
    e(LD("R1", G["IN_R"]))
    e(LD("R9", G["ROUTE"]))
    e(IMM("R10", 1))
    e(A("AND", "R9", "R9", "R10"))
    p.jump("clip", "EQ")
    e(A("ADD", "R0", "R0", "R13"))
    e(A("ADD", "R1", "R1", "R13"))
    p.label("clip")
    e(A("CLIP", "R0", "R0", "R14"))
    e(A("CLIP", "R1", "R1", "R14"))
    e(dm_write_off(0, "I4", "R0"))                          # unit 0's return pair; I4 on by two
    e(dm_write_off(1, "I4", "R1"))
    e(modify_imm("I4", 2))
    e(LD("I5", G["SAVEI5"]))
    e(LD("I7", G["SAVEI7"]))
    e(rts(db=True))                                         # the kernel's effect epilogue
    e((EPILOGUE, "R4 = DM(I1, M5),  R7 = PM(I12, M8)"))
    e(modify_imm("I8", 0x50))

    # ---- E9: unit 9 ---------------------------------------------------------------------------------------
    p.label("E9")
    e(LD("R0", G["HB9"]))                                   # its own heartbeat
    e(A("INC", "R0", "R0"))
    e(ST(G["HB9"], "R0"))
    e(LD("R0", G["MIX"]))
    e(dm_write_off(0, "I4", "R0"))                          # MIX alone (a chorus return is wet only)
    e(dm_write_off(1, "I4", "R0"))
    e(modify_imm("I4", 2))
    e(rts(db=True))
    e((EPILOGUE, "R4 = DM(I1, M5),  R7 = PM(I12, M8)"))
    e(modify_imm("I8", 0x50))

    # ---- the trampoline: the host points it at the current kernel ------------------------------------------
    p.label("TRAMP")
    e(jump_abs(PM_ALG[0]))

    # ---- voice control (k = 0), I5 = the voice's block + 32 ------------------------------------------------
    p.label("voicectl")
    e(LDI("R0", O_PEGIX))
    e(LDI("R1", O_PEGL))
    e(LDI("R2", O_STATE))                                   # OP6's: the key flag
    e(IMM("R3", 3))
    e(A("COMP", "R0", "R0", "R3"))
    p.jump("p_go", "LT")
    p.jump("p_done", "GT")
    e(bit_imm("BTST", "R0", "R2", 29))
    p.jump("p_done", "NOT SZ")                              # stage 3 holds while the key is down
    p.label("p_go")
    e(ureg_move("I7", "R0"))
    e(IMM("R3", 0))                                         # (I7 written: a cycle before its use)
    e(dm_read_off("R4", P_PEGL, "I7"))                      # target
    e(dm_read_off("R5", P_PEGR, "I7"))                      # rate
    e(A("COMP", "R0", "R4", "R1"))
    p.jump("p_fall", "LE")
    e(A("ADD", "R1", "R1", "R5"))
    e(A("COMP", "R0", "R1", "R4"))
    p.jump("p_done", "LT")
    p.jump("p_hit")
    p.label("p_fall")
    e(A("SUB", "R1", "R1", "R5"))
    e(A("COMP", "R0", "R1", "R4"))
    p.jump("p_done", "GT")
    p.label("p_hit")
    e(A("PASS", "R1", "R4"))
    e(A("INC", "R0", "R0"))
    p.label("p_done")
    e(STI(O_PEGIX, "R0"))
    e(STI(O_PEGL, "R1"))
    e(LD("R6", G["LFO_DLY"]))                               # the LFO's pitch modulation, in float
    e(A("FLOAT", "F6", "R6"))
    e(LD("R7", G["LFO_VAL"]))
    e(IMM("R8", 0x800000))
    e(A("SUB", "R7", "R7", "R8"))
    e(A("FLOAT", "F7", "R7"))
    e(compute(fmul("F6", "F6", "F7")))
    e(LD("R7", P["PMDS"]))
    e(IMM("R8", -39))
    e(A("FLOATBY", "F7", "R7", "R8"))
    e(compute(fmul("F6", "F6", "F7")))
    e(A("FIX", "R6", "F6"))
    e(A("ADD", "R1", "R1", "R6"))
    e(LD("R7", G["PBASE"]))
    e(A("ADD", "R1", "R1", "R7"))
    for k in range(6):
        e(STI(O_PMOD + k, "R1"))
    e(bit_imm("BTST", "R0", "R2", 29))                      # the voice's end, once the key is up
    p.jump("audio", "NOT SZ")
    e(LD("R3", P["ENDW"]))
    e(IMM("R4", m.ST_PEND))
    for k in range(6):
        e(LDI("R5", O_STATE + k))
        e(A("XOR", "R5", "R5", "R4"))
        e(LD("R6", P_OPW + 4 * k + OPW_NC))
        e(A("OR", "R5", "R5", "R6"))
        e(A("AND", "R3", "R3", "R5"))
    e(field_imm("FEXT", "R3", "R3", 26, 3))
    e(IMM("R5", 5))
    e(A("AND", "R3", "R3", "R5"))
    e(A("COMP", "R0", "R3", "R5"))
    p.jump("audio", "NE")
    e(IMM("R3", 0))
    e(STI(O_ACTIVE, "R3"))
    p.jump("audio")

    # ---- operator control (k = 1-6): I5 = the voice's block + 32 + the operator, R14 = 4 x the operator ------
    #      R0 the state, R1 the level, R11 the output level, R12 the rate scaling (fm_model.Engine._op_control)
    p.label("opctl")
    e(LDI("R0", O_STATE))
    e(LDI("R1", O_LEVEL))
    e(LDI("R2", O_OLRS))
    e(shift_imm("LSHIFT", "R11", "R2", -8))
    e(field_imm("FEXT", "R12", "R2", 0, 8))
    e(bit_imm("BTST", "R0", "R0", 28))
    p.jump("t_nopend", "SZ")
    e(bit_imm("BCLR", "R0", "R0", 28))                      # the key-off: advance(3)
    e(IMM("R3", 3))
    p.call("adv")
    p.label("t_nopend")
    e(field_imm("FEXT", "R4", "R0", 0, 24))                 # static count
    p.jump("t_tick", "SZ")
    e(IMM("R5", m.CTRL_N))
    e(A("SUB", "R4", "R4", "R5"))
    e(IMM("R5", 0xFF000000))
    e(A("AND", "R0", "R0", "R5"))
    e(A("PASS", "R4", "R4"))
    p.jump("t_exp", "LE")
    e(A("OR", "R0", "R0", "R4"))
    p.jump("t_tick")
    p.label("t_exp")
    e(field_imm("FEXT", "R3", "R0", 24, 3))                 # expired: advance(ix + 1)
    e(A("INC", "R3", "R3"))
    p.call("adv")
    p.label("t_tick")
    e(field_imm("FEXT", "R3", "R0", 24, 3))                 # ix
    e(IMM("R5", 3))
    e(A("COMP", "R0", "R3", "R5"))
    p.jump("t_go", "LT")
    p.jump("t_done", "GT")
    e(bit_imm("BTST", "R0", "R0", 29))
    p.jump("t_done", "NOT SZ")                              # stage 3 holds while the key is down
    p.label("t_go")
    e(field_imm("FEXT", "R4", "R0", 0, 24))
    p.jump("t_done", "NOT SZ")                              # a static stage holds
    env_target(e)
    e(field_imm("FEXT", "R8", "R7", 8, 6))                  # qrate + rate scaling, at most 63
    e(A("ADD", "R8", "R8", "R12"))
    e(IMM("R9", 63))
    e(A("MIN", "R8", "R8", "R9"))
    e(field_imm("FEXT", "R9", "R8", 0, 2))                  # increment: (4 + (q & 3)) << (8 + (q >> 2))
    e(IMM("R10", 4))
    e(A("ADD", "R9", "R9", "R10"))
    e(field_imm("FEXT", "R10", "R8", 2, 4))
    e(IMM("R13", 2 + m.LG_N))
    e(A("ADD", "R10", "R10", "R13"))
    e(compute(shift_reg("LSHIFT", "R9", "R9", "R10")))
    e(A("COMP", "R0", "R6", "R1"))
    p.jump("t_fall", "LE")
    e(IMM("R10", 1716 << 16))                               # rising: from at least 1716 << 16, exponentially
    e(A("MAX", "R1", "R1", "R10"))
    e(IMM("R10", 17 << 24))
    e(A("SUB", "R10", "R10", "R1"))
    e(shift_imm("ASHIFT", "R10", "R10", -24))
    e(compute(mul_ssi("R10", "R10", "R9")))
    e(A("ADD", "R1", "R1", "R10"))
    e(A("COMP", "R0", "R1", "R6"))
    p.jump("t_done", "LT")
    p.jump("t_hit")
    p.label("t_fall")
    e(A("SUB", "R1", "R1", "R9"))
    e(A("COMP", "R0", "R1", "R6"))
    p.jump("t_done", "GT")
    p.label("t_hit")
    e(A("PASS", "R1", "R6"))                                # the target reached: advance(ix + 1)
    e(A("INC", "R3", "R3"))
    p.call("adv")
    p.label("t_done")
    e(STI(O_STATE, "R0"))
    e(STI(O_LEVEL, "R1"))
    e(ureg_move("I7", "R14"))
    e(A("PASS", "R8", "R1"))                                # (I7 written: a cycle before its use)
    e(dm_read_off("R2", P_OPW + OPW_AMS, "I7"))
    e(A("PASS", "R2", "R2"))
    p.jump("t_noams", "EQ")
    e(LD("R3", G["AMOD"]))                                  # amplitude modulation: level -= level x 2 ** x
    e(shift_imm("LSHIFT", "R3", "R3", -9))
    e(shift_imm("LSHIFT", "R2", "R2", -9))
    e(compute(mul_ssi("R3", "R3", "R2")))
    e(shift_imm("LSHIFT", "R3", "R3", -10))
    e(IMM("R4", m.AM_K1))
    e(compute(mul_ssi("R3", "R3", "R4")))
    e(IMM("R4", m.AM_K0))
    e(A("ADD", "R8", "R3", "R4"))
    exp2_core(e)
    e(A("SCALB", "F11", "F11", "R10"))
    e(A("FLOAT", "F10", "R1"))
    e(compute(fmul("F10", "F10", "F11")))
    e(A("FIX", "R10", "F10"))
    e(A("SUB", "R8", "R1", "R10"))
    p.label("t_noams")
    e(dm_read_off("R9", P_OPW + OPW_GSHX, "I7"))            # the gain, and its step over the next 64 frames
    p.call("exp2")
    e(LDI("R10", O_G))
    e(A("SUB", "R10", "R8", "R10"))
    e(shift_imm("ASHIFT", "R10", "R10", -m.LG_N))
    e(STI(O_DG, "R10"))
    e(LDI("R8", O_PITCH))                                   # the frequency
    e(dm_read_off("R10", P_OPW + OPW_MODE, "I7"))
    e(LDI("R9", O_PMOD))
    e(A("PASS", "R10", "R10"))
    p.jump("t_ratio", "EQ")
    e(LD("R9", G["PBASE"]))                                 # a fixed frequency: no pitch envelope, no LFO
    p.label("t_ratio")
    e(A("ADD", "R8", "R8", "R9"))
    e(IMM("R9", m.FREQ_K))
    e(A("ADD", "R8", "R8", "R9"))
    e(IMM("R9", 0))
    p.call("exp2")
    e(STI(O_INC, "R8"))
    p.jump("audio")

    # ---- advance(R3) (fm_model.Engine._advance): R0 the state, R1 the level, R11, R12, R14 as above;
    #      clobbers R5-R7, I7
    p.label("adv")
    e(IMM("R5", m.ST_KEEP))
    e(A("AND", "R0", "R0", "R5"))
    e(shift_imm("LSHIFT", "R5", "R3", 24))
    e(A("OR", "R0", "R0", "R5"))
    e(IMM("R5", 4))
    e(A("COMP", "R0", "R3", "R5"))
    p.jump("adv_ret", "GE")
    env_target(e)
    e(A("COMP", "R0", "R6", "R1"))
    p.jump("adv_ret", "NE")
    e(field_imm("FEXT", "R5", "R7", 0, 7))                  # a stage whose level does not change: msfa's
    e(A("ADD", "R5", "R5", "R12"))                          #   "static" time, by its rate + rate scaling
    e(IMM("R6", 99))
    e(A("MIN", "R5", "R5", "R6"))
    e(ureg_move("I7", "R5"))
    e(IMM("R6", 0))                                         # (I7 written: a cycle before its use)
    e(dm_read_off("R5", STATICS, "I7"))
    e(A("OR", "R0", "R0", "R5"))
    p.label("adv_ret")
    e(rts(db=False))

    # ---- exp2: R8 = x (Q24), R9 = a shift -> R8 = FIX(2 ** (x / 2 ** 24) BY R9) (fm_model.exp2_q24); clobbers
    #      R9-R12, F8-F12
    p.label("exp2")
    exp2_core(e)
    e(A("ADD", "R9", "R9", "R10"))
    e(A("FIXBY", "R8", "F11", "R9"))
    e(rts(db=False))

    # ---- the LFO (slot 63; fm_model.Engine._lfo_tick), in the second part of PM ------------------------------
    e2 = p2.emit
    p2.label("lfo2")
    e2(LD("R0", G["LFO_PH"]))
    e2(LD("R1", P["LFDELTA"]))
    e2(A("ADD", "R0", "R0", "R1"))
    e2(IMM("R15", MASK24))
    e2(A("AND", "R0", "R0", "R15"))
    e2(ST(G["LFO_PH"], "R0"))
    e2(LD("R2", P["LFW"]))
    e2(A("PASS", "R2", "R2"))
    p2.jump("w_tri", "EQ")
    for n, lab in ((1, "w_sawd"), (2, "w_sawu"), (3, "w_sq"), (4, "w_sin")):
        e2(IMM("R4", n))
        e2(A("COMP", "R0", "R2", "R4"))
        p2.jump(lab, "EQ")
    e2(A("COMP", "R0", "R0", "R1"))                          # sample and hold: a new value when the phase wraps
    p2.jump("sh_keep", "GE")
    e2(LD("R5", G["LFO_RAND"]))
    e2(IMM("R6", 179))
    e2(compute(mul_ssi("R5", "R5", "R6")))
    e2(IMM("R6", 17))
    e2(A("ADD", "R5", "R5", "R6"))
    e2(IMM("R6", 0xFF))
    e2(A("AND", "R5", "R5", "R6"))
    e2(ST(G["LFO_RAND"], "R5"))
    p2.label("sh_keep")
    e2(LD("R5", G["LFO_RAND"]))
    e2(IMM("R6", 0x80))
    e2(A("XOR", "R5", "R5", "R6"))
    e2(A("INC", "R5", "R5"))
    e2(shift_imm("LSHIFT", "R3", "R5", 16))
    p2.jump("w_done")
    p2.label("w_tri")
    e2(shift_imm("LSHIFT", "R3", "R0", 1))
    e2(bit_imm("BTST", "R0", "R0", 23))
    e2(A("XOR", "R3", "R3", "R15", cond="NOT SZ"))
    e2(A("AND", "R3", "R3", "R15"))
    p2.jump("w_done")
    p2.label("w_sawd")
    e2(A("NOT", "R3", "R0"))
    e2(IMM("R4", 0x800000))
    e2(A("XOR", "R3", "R3", "R4"))
    e2(A("AND", "R3", "R3", "R15"))
    p2.jump("w_done")
    p2.label("w_sawu")
    e2(IMM("R4", 0x800000))
    e2(A("XOR", "R3", "R0", "R4"))
    p2.jump("w_done")
    p2.label("w_sq")
    e2(IMM("R3", 1 << 24))
    e2(IMM("R4", 0))
    e2(bit_imm("BTST", "R0", "R0", 23))
    e2(A("PASS", "R3", "R4", cond="NOT SZ"))
    p2.jump("w_done")
    p2.label("w_sin")
    e2(shift_imm("LSHIFT", "R4", "R0", -16))
    e2(ureg_move("I7", "R4"))
    e2(IMM("R4", 0x800000))                                 # (I7 written: a cycle before its use)
    e2(dm_read_off("R3", SINE, "I7"))
    e2(shift_imm("ASHIFT", "R3", "R3", -7))
    e2(A("ADD", "R3", "R3", "R4"))
    p2.label("w_done")
    e2(ST(G["LFO_VAL"], "R3"))
    e2(LD("R4", G["LFO_DST"]))                               # the delay
    e2(IMM("R5", 0x800000))
    e2(LD("R6", P["LFD1"]))
    e2(LD("R7", P["LFD2"]))
    e2(A("COMP", "R0", "R4", "R5"))
    e2(A("PASS", "R6", "R7", cond="GE"))
    e2(A("ADD", "R6", "R4", "R6"))
    e2(A("COMP", "R0", "R6", "R15"))
    p2.jump("d_ok", "LE")
    e2(IMM("R8", 1 << 24))
    p2.jump("d_store")
    p2.label("d_ok")
    e2(ST(G["LFO_DST"], "R6"))
    e2(IMM("R8", 0))
    e2(A("COMP", "R0", "R6", "R5"))
    p2.jump("d_store", "LT")
    e2(shift_imm("LSHIFT", "R8", "R6", 1))
    e2(A("AND", "R8", "R8", "R15"))
    p2.label("d_store")
    e2(ST(G["LFO_DLY"], "R8"))
    e2(IMM("R9", 1 << 24))                                  # amplitude modulation depth
    e2(A("SUB", "R9", "R9", "R3"))
    e2(shift_imm("LSHIFT", "R9", "R9", -9))
    e2(shift_imm("LSHIFT", "R8", "R8", -8))
    e2(LD("R10", P["AMD"]))
    e2(compute(mul_ssi("R8", "R8", "R10")))
    e2(shift_imm("LSHIFT", "R8", "R8", -8))
    e2(compute(mul_ssi("R8", "R8", "R9")))
    e2(shift_imm("LSHIFT", "R8", "R8", -7))
    e2(ST(G["AMOD"], "R8"))
    p2.jump("audio")

    w1 = p.assemble(extern=p2.labels)
    w2 = p2.assemble(extern=p.labels)
    labels = dict(p.labels)
    labels.update(p2.labels)
    return w1, w2, labels


def alg_program(alg, base):
    """algorithm alg's kernel (0-31), position-independent (relative jumps only) for either buffer: the active voices
    from I5 = VOICES + 32, CNT of them; adds their carriers to R13. R15 = MASK24 and R11 = FEXT_IDX on entry.
    Registers: R0-R6, R8, R12 the operator's; R14 bus 1, R9 bus 2, R10 a feedback operator's modulation; R13 the
    sum (fm_model.Engine._voice_audio)."""
    p = Program(base)
    e = p.emit
    flags = m.ALGORITHMS[alg]
    busreg = {1: "R14", 2: "R9"}
    p.label("vloop")
    e(LDI("R0", O_ACTIVE))
    e(A("PASS", "R0", "R0"))
    p.jump("vskip", "EQ")
    written = set()
    for k in range(6):
        fl = flags[k]
        inbus, outbus, add = (fl >> 4) & 3, fl & 3, fl & 4
        fbop = (fl & 0xC0) == 0xC0
        if fbop:                                            # self-feedback: (FB0 + FB1) x FBK, FB0 = FB1
            e(LDI("R1", O_FB0))
            e(LDI("R2", O_FB1))
            e(STI(O_FB0, "R2", comp=alu("ADD", "R1", "R1", "R2")))
            if outbus == 0:
                e(shift_imm("ASHIFT", "R1", "R1", 4))       # a carrier's: x 16 (FBK_C)
            e(LD("R4", P["FBKC"] if outbus == 0 else P["FBKM"]))
            e(mf("R10", "R1", "R4"))
            mod = "R10"
        elif inbus:
            assert inbus in written, (alg, k)
            mod = busreg[inbus]
        else:
            mod = None
        e(LDI("R0", O_PH + k))
        if mod:
            e(LDI("R1", O_INC + k, comp=alu("ADD", "R8", "R0", mod)))
            arg = "R8"
        else:
            e(LDI("R1", O_INC + k))
            arg = "R0"
        e(LDI("R5", O_G + k, comp=alu("ADD", "R2", "R0", "R1")))          # the next phase
        e(LDI("R6", O_DG + k, comp=alu("AND", "R2", "R2", "R15")))
        e(STI(O_PH + k, "R2", comp=shift_reg("FEXT", "R3", arg, "R11")))  # the sine's index
        e(ureg_move("I7", "R3", comp=alu("ADD", "R5", "R5", "R6")))      # the gain's step
        e(field_imm("FDEP", "R4", arg, 15, 16))                           # the fraction, Q31
        e(dm_read_off("R12", SINE, "I7"))
        e(dm_read_off("R6", SINE + 1, "I7"))
        e(STI(O_G + k, "R5", comp=alu("SUB", "R2", "R6", "R12")))
        e(mf("R1", "R2", "R4"))
        e(A("ADD", "R3", "R12", "R1"))                                    # the sine
        if outbus and not add:
            d = busreg[outbus]
            e(mf(d, "R3", "R5"))
            written.add(outbus)
            if fbop:
                e(STI(O_FB1, d))
        else:
            e(mf("R1", "R3", "R5"))
            if outbus:
                assert outbus in written, (alg, k)
            acc = busreg[outbus] if outbus else "R13"
            if fbop:
                e(STI(O_FB1, "R1", comp=alu("ADD", acc, acc, "R1")))
            else:
                e(A("ADD", acc, acc, "R1"))
    p.label("vskip")
    e(modify_imm("I5", VOICE_LEN))
    e(LD("R0", G["CNT"]))
    e(A("DEC", "R0", "R0"))
    e(ST(G["CNT"], "R0"))
    p.jump("vloop", "NE")
    e(rts(db=False))
    return p.assemble()


def dm_tables():
    """{address: words}: what the host uploads once, before hooking"""
    return {SINE: list(m.SIN_TABLE), STATICS: list(m.STATICS)}


def patch_image(pw):
    """fm_model.patch_words -> the words of PATCH..PATCH_END"""
    out = []
    for k in range(6):
        out += pw['env'][k]
    for k in range(6):
        out += [pw['gshx'][k], pw['mode'][k], pw['ams'][k], pw['nc'][k]]
    out += pw['peg_rate'] + pw['peg_level']
    delta, d1, d2, wave, sync = pw['lfo']
    out += [pw['pmds'], pw['amd'], delta, d1, d2, wave, sync, pw['oks'], pw['endw'], pw['fbk_m'], pw['fbk_c']]
    assert len(out) == PATCH_END - PATCH
    return [x & 0xFFFFFFFF for x in out]


def voice_image():
    """a silent voice's 64 words (fm_model.Voice()): every envelope done (stage 4), the pitch envelope too"""
    v = [0] * VOICE_LEN
    for k in range(6):
        v[32 + O_STATE + k] = 4 << 24
    v[32 + O_PEGIX] = 4
    return v


def initial_globals(nv=m.NVOICES):
    v = [0] * (GLOBALS_END - GLOBALS)
    v[G["ROUTE"] - GLOBALS] = 1
    v[G["NV"] - GLOBALS] = nv
    v[G["LFO_VAL"] - GLOBALS] = 1 << 23
    return v


def check(words, unidasm, base):
    """unidasm -arch sharc over the words: each line must be the text it was meant to be"""
    with tempfile.NamedTemporaryFile(suffix=".bin") as t:
        t.write(b"".join(struct.pack("<Q", w) for _, w, _ in words))
        t.flush()
        out = subprocess.run([unidasm, t.name, "-arch", "sharc", "-basepc", hex(base)],
                             capture_output=True, text=True, check=True).stdout
    lines = [ln for ln in out.splitlines() if re.match(r"\s*[0-9a-f]+: [0-9a-f]{16}", ln)]
    bad = 0
    for (pc, w, text), ln in zip(words, lines):
        got = re.sub(r"\s+", " ", ln.split(None, 2)[2].strip())
        if got != re.sub(r"\s+", " ", text):
            print(f"fm_dsp: PM 0x{pc:04X} {w:012X}: unidasm says '{got}', meant '{text}'")
            bad += 1
    if len(lines) != len(words):
        print(f"fm_dsp: unidasm gave {len(lines)} lines for {len(words)} words")
        bad += 1
    return bad


def build():
    w1, w2, labels = program()
    assert PM_CODE + len(w1) <= PM_CODE_END, f"resident part 1: {len(w1)} words"
    assert PM_CODE2 + len(w2) <= PM_CODE2_END, f"resident part 2: {len(w2)} words"
    algs = [alg_program(a, PM_ALG[0]) for a in range(32)]
    for a, ws in enumerate(algs):
        assert len(ws) <= PM_ALG_LEN, (a, len(ws))
        # position independence: the same words at the other buffer
        assert [w for _, w, _ in ws] == [w for _, w, _ in alg_program(a, PM_ALG[1])], a
    assert GLOBALS_END <= 0x9C40 and SINE + len(m.SIN_TABLE) <= GLOBALS
    assert PATCH_END <= 0x9D00 and STAGE_END <= 0xA000 and STATICS + 100 <= STAGE
    return w1, w2, labels, algs


def write_inc(out, w1, w2, labels, algs):
    eq = [("DSP_CODE_ADDR", PM_CODE), ("DSP_CODE_WORDS", len(w1)), ("DSP_CODE2_ADDR", PM_CODE2),
          ("DSP_CODE2_WORDS", len(w2)), ("DSP_ENTRY0", labels["E0"]), ("DSP_ENTRY9", labels["E9"]),
          ("DSP_TRAMP", labels["TRAMP"]), ("DSP_ALG0", PM_ALG[0]), ("DSP_ALG1", PM_ALG[1]),
          ("DSP_SLOT0", PM_SLOT0), ("DSP_SLOT9", PM_SLOT9), ("DSP_DEF0", DEF_TARGET0), ("DSP_DEF9", DEF_TARGET9),
          ("DSP_CALLDB_HI", CALL_DB >> 32), ("DSP_JUMP_HI", jump_abs(0)[0] >> 32),
          ("DSP_SINE", SINE), ("DSP_SINE_WORDS", len(m.SIN_TABLE)), ("DSP_STATICS", STATICS),
          ("DSP_GLOBALS", GLOBALS), ("DSP_NGLOBALS", GLOBALS_END - GLOBALS), ("DSP_PATCH", PATCH),
          ("DSP_PATCH_WORDS", PATCH_END - PATCH), ("DSP_STAGE", STAGE), ("DSP_VOICES", VOICES),
          ("DSP_VOICE_LEN", VOICE_LEN), ("DSP_MSGR6", MSGR6), ("DSP_MSGR7", MSGR7)]
    eq += [("DSP_G_" + n, a) for n, a in G.items()]
    eq += [("DSP_V_ACTIVE", O_ACTIVE + 32)]
    with open(os.path.join(out, "dsp_equ.inc"), "w") as f:
        f.write("# GENERATED by tools/fm_dsp.py -- KN7FM's DSP program: addresses (do not edit)\n")
        for name, val in eq:
            f.write(f".equ {name + ',':<20} 0x{val:X}\n")
    with open(os.path.join(out, "dsp_data.inc"), "w") as f:
        f.write("# GENERATED by tools/fm_dsp.py -- KN7FM's DSP program (do not edit; see tools/fm_dsp.py)\n")
        f.write("# PM words as the host streams them: three 16-bit halves each, least significant first\n")

        def pm(label, words):
            f.write(f"\n    .align  4\n{label}:\n")
            for pc, w, text in words:
                f.write(f"    .word   0x{w & 0xFFFF:04X}, 0x{(w >> 16) & 0xFFFF:04X}, 0x{w >> 32:04X}"
                        f"    # {pc:04X}: {text}\n")

        pm("DspCode", w1)
        pm("DspCode2", w2)
        f.write("\n# the 32 algorithm kernels: a table of (offset from DspAlgs, words), then the words\n"
                "    .align  4\nDspAlgTab:\n")
        off = 0
        for ws in algs:
            f.write(f"    .long   {off * 6}, {len(ws)}\n")
            off += len(ws)
        f.write("DspAlgs:\n")
        for a, ws in enumerate(algs):
            f.write(f"# algorithm {a + 1}\n")
            for pc, w, text in ws:
                f.write(f"    .word   0x{w & 0xFFFF:04X}, 0x{(w >> 16) & 0xFFFF:04X}, 0x{w >> 32:04X}"
                        f"    # {text}\n")

        def dm(label, words):
            f.write(f"\n    .align  4\n{label}:\n")
            for k in range(0, len(words), 8):
                f.write("    .long   " + ", ".join(f"0x{x & 0xFFFFFFFF:08X}" for x in words[k:k + 8]) + "\n")

        dm("DspSine", m.SIN_TABLE)
        dm("DspStatics", m.STATICS)
        dm("DspGlobals", initial_globals())


def main():
    out = sys.argv[1]
    args = sys.argv[2:]
    w1, w2, labels, algs = build()
    if "--check" in args:
        u = args[args.index("--check") + 1]
        if not os.path.exists(u):
            sys.exit(f"fm_dsp: no unidasm at {u}; every word must be checked against it")
        bad = check(w1, u, PM_CODE) + check(w2, u, PM_CODE2)
        for ws in algs:
            bad += check(ws, u, PM_ALG[0])
        if bad:
            sys.exit(f"fm_dsp: {bad} instruction(s) do not disassemble as meant")
        print(f"fm_dsp: {len(w1) + len(w2) + sum(len(a) for a in algs)} instructions, each disassembles as meant "
              f"(unidasm -arch sharc)")
    if "--listing" in args:
        with open(args[args.index("--listing") + 1], "w") as f:
            for pc, w, text in w1 + w2:
                f.write(f"{pc:04x}: {w:012x}  {text}\n")
            for a, ws in enumerate(algs):
                f.write(f"\n# algorithm {a + 1}\n")
                for pc, w, text in ws:
                    f.write(f"{pc:04x}: {w:012x}  {text}\n")
    write_inc(out, w1, w2, labels, algs)
    print(f"fm_dsp: {out}/dsp_equ.inc, dsp_data.inc: resident {len(w1)} + {len(w2)} PM words, kernels "
          f"{min(len(a) for a in algs)}-{max(len(a) for a in algs)} words")


if __name__ == "__main__":
    main()
