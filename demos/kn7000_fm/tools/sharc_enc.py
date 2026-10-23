#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# sharc_enc.py -- a minimal ADSP-2106x / ADSP-21065L ("SHARC") instruction encoder: the instruction forms KN7FM's DSP
# program (tools/fm_dsp.py) uses, and nothing else.
#
# COPIED from technics-anotherworld kn7000/tools/sharc_enc.py (itself DSPTONE's, technics-roms-disasm
# kn7000/tools/dsptone), so that this program builds on its own. Added here: the ALU's ABS, NEG, NOT and its float
# operations (F+, F-, PASS, SCALB, FLOAT, FIX, both scaled), the multiplier's float product, the shifter's FEXT and
# FDEP by immediate field, the multifunction "Rm = R0-3 * R4-7 (SSFR), Ra = R8-11 + R12-15", compute with a data
# move at a 6-bit offset (type 4), and conditional CALLs.
#
# There is no SHARC assembler in these projects, so each form is encoded from the bit layout MAME's SHARC
# disassembler decodes (src/devices/cpu/sharc/sharc_dasm.cpp), and every instruction is checked against that
# disassembler (unidasm -arch sharc) by fm_dsp.py --check: an instruction whose text differs from what was meant
# stops the build. MAME's disassembler prints a fixed-point product without its mode ("R0 = R1 * R2" for SSI and SSF
# alike), so the fractional product used here is the multifunction form, whose text names it (SSFR), and the one
# single-function product is the integer one (mul_ssi, opcode 0x70), whose mode the disassembler text cannot show:
# fm_dsp.py checks its opcode field instead; and every
# operation used is one that both of MAME's SHARC cores implement (the interpreter, sharcops.hxx, and the
# recompiler, sharcdrc.cpp: an unimplemented one is a fatal error in either), see fm_dsp.py.
#
# A 48-bit instruction word is returned as an int. Registers are named as the disassembler names them.

UREG = {}
for _k, _p in enumerate("RIMLB"):
    for _n in range(16):
        UREG[f"{_p}{_n}"] = _k * 16 + _n

COND = {c: n for n, c in enumerate(
    ["EQ", "LT", "LE", "AC", "AV", "MV", "MS", "SV", "SZ", "FLAG0_IN", "FLAG1_IN", "FLAG2_IN", "FLAG3_IN",
     "TF", "BM", "NOT LCE", "NE", "GE", "GT", "NOT AC", "NOT AV", "NOT MV", "NOT MS", "NOT SV",
     "NOT SZ", "NOT FLAG0_IN", "NOT FLAG1_IN", "NOT FLAG2_IN", "NOT FLAG3_IN", "NOT TF", "NBM", "TRUE"])}

# single-function ALU operations (compute field, CU = 00): opcode, and the disassembler's text
ALU = {
    "ADD": (0x01, "R{n} = R{x} + R{y}"), "SUB": (0x02, "R{n} = R{x} - R{y}"), "COMP": (0x0A, "COMP(R{x}, R{y})"),
    "PASS": (0x21, "R{n} = PASS R{x}"), "NEG": (0x22, "R{n} = -R{x}"), "INC": (0x29, "R{n} = R{x} + 1"),
    "DEC": (0x2A, "R{n} = R{x} - 1"), "ABS": (0x30, "R{n} = ABS R{x}"), "AND": (0x40, "R{n} = R{x} AND R{y}"),
    "OR": (0x41, "R{n} = R{x} OR R{y}"), "XOR": (0x42, "R{n} = R{x} XOR R{y}"), "NOT": (0x43, "R{n} = NOT R{x}"),
    "MIN": (0x61, "R{n} = MIN(R{x}, R{y})"), "MAX": (0x62, "R{n} = MAX(R{x}, R{y})"),
    "CLIP": (0x63, "R{n} = CLIP R{x} BY R{y}"),
    "FADD": (0x81, "F{n} = F{x} + F{y}"), "FSUB": (0x82, "F{n} = F{x} - F{y}"), "FPASS": (0xA1, "F{n} = PASS F{x}"),
    "SCALB": (0xBD, "F{n} = SCALB F{x} BY R{y}"), "FIX": (0xC9, "R{n} = FIX F{x}"),
    "FIXBY": (0xD9, "R{n} = FIX F{x} BY R{y}"), "FLOAT": (0xCA, "F{n} = FLOAT R{x}"),
    "FLOATBY": (0xDA, "F{n} = FLOAT R{x} BY R{y}")}
SHIFT_IMM = {"LSHIFT": 0x00, "ASHIFT": 0x01}
FIELD_IMM = {"FEXT": 0x10, "FDEP": 0x11}
SHIFT_REG = {"LSHIFT": 0x00, "ASHIFT": 0x04}   # shifter (compute field CU = 10): Rn = LSHIFT/ASHIFT Rx BY Ry


def _r(name):
    assert name[0] in "RF" and 0 <= int(name[1:]) < 16, name
    return int(name[1:])


def _u(name):
    return UREG[name]


def _dag1(name):
    n = int(name[1:])
    assert name[0] in "IM" and 0 <= n < 8, f"{name}: a DAG1 register (I0-I7 / M0-M7)"
    return n


# ---- compute fields (23 bits); each returns (field, text) -------------------------------------------------------
def alu(op, rn, rx, ry="R0"):
    """single-function ALU: Rn = Rx op Ry (fixed or float, as ALU names it)"""
    code, fmt = ALU[op]
    return (code << 12) | (_r(rn) << 8) | (_r(rx) << 4) | _r(ry), fmt.format(n=_r(rn), x=_r(rx), y=_r(ry))


def fmul(fn, fx, fy):
    """single-function multiplier (CU = 01), opcode 0x30: Fn = Fx * Fy"""
    return (1 << 20) | (0x30 << 12) | (_r(fn) << 8) | (_r(fx) << 4) | _r(fy), \
        f"F{_r(fn)} = F{_r(fx)} * F{_r(fy)}"


def mul_ssi(rn, rx, ry):
    """single-function multiplier (CU = 01), opcode 0x70: Rn = Rx * Ry (SSI), signed integers, the low 32 bits (MAME's
    disassembler prints it without the mode)"""
    return (1 << 20) | (0x70 << 12) | (_r(rn) << 8) | (_r(rx) << 4) | _r(ry), f"R{_r(rn)} = R{_r(rx)} * R{_r(ry)}"


def shift_reg(op, rn, rx, ry):
    """single-function shifter (CU = 10): Rn = LSHIFT/ASHIFT Rx BY Ry (Ry's low byte, signed: < 0 shifts right)"""
    return (2 << 20) | (SHIFT_REG[op] << 12) | (_r(rn) << 8) | (_r(rx) << 4) | _r(ry), \
        f"R{_r(rn)} = {op} R{_r(rx)} BY R{_r(ry)}"


def mf_ssfr(rm, rxm, rym, ra, rxa, rya, sub=False):
    """multifunction: Rm = Rxm * Rym (SSFR), Ra = Rxa + Rya (or -); Rxm R0-R3, Rym R4-R7, Rxa R8-R11, Rya R12-R15"""
    xm, ym, xa, ya = _r(rxm), _r(rym), _r(rxa), _r(rya)
    assert 0 <= xm < 4 and 4 <= ym < 8 and 8 <= xa < 12 and 12 <= ya < 16, (rxm, rym, rxa, rya)
    op = 0x05 if sub else 0x04
    field = ((1 << 22) | (op << 16) | (_r(rm) << 12) | (_r(ra) << 8) | (xm << 6) | ((ym - 4) << 4)
             | ((xa - 8) << 2) | (ya - 12))
    return field, (f"R{_r(rm)} = R{xm} * R{ym} (SSFR),  R{_r(ra)} = R{xa} {'-' if sub else '+'} R{ya}")


# ---- instructions: each returns (word, text) --------------------------------------------------------------------
def _if(cond):
    return "" if cond == "TRUE" else f"IF {cond}, "


def nop():
    return 0, "NOP"


def compute(comp, cond="TRUE"):
    """type 2: IF cond compute"""
    field, text = comp
    return (0x01 << 40) | (COND[cond] << 33) | field, _if(cond) + text


def shift_imm(op, rn, rx, amount, cond="TRUE"):
    """type 6 without a data move: Rn = LSHIFT/ASHIFT Rx BY amount (signed 8 bits)"""
    assert -128 <= amount < 128
    d = amount & 0xFF
    return ((0x02 << 40) | (COND[cond] << 33) | (SHIFT_IMM[op] << 16) | (d << 8) | (_r(rn) << 4) | _r(rx),
            _if(cond) + f"R{_r(rn)} = {op} R{_r(rx)} BY {amount}")


def field_imm(op, rn, rx, pos, length, cond="TRUE"):
    """type 6 without a data move: Rn = FEXT Rx BY pos:len (the field at pos, to bit 0) / FDEP Rx BY pos:len (Rx's low
    len bits to pos); the 12-bit operand is len << 6 | pos, its low 8 bits at 15:8 and its high 4 at 30:27"""
    assert 0 <= pos < 64 and 0 < length < 64
    d = (length << 6) | pos
    return ((0x02 << 40) | (COND[cond] << 33) | (((d >> 8) & 0xF) << 27) | (FIELD_IMM[op] << 16)
            | ((d & 0xFF) << 8) | (_r(rn) << 4) | _r(rx), _if(cond) + f"R{_r(rn)} = {op} R{_r(rx)} BY {pos}:{length}")


def compute_dm(comp, dreg, offset, ireg, write, cond="TRUE"):
    """type 4: IF cond compute, dreg = DM(offset, Ia) / DM(offset, Ia) = dreg -- a 6-bit signed offset, Ia not
    modified (pre-modify); all reads of the instruction happen before its writes"""
    assert -32 <= offset < 32
    field, text = comp if comp is not None else (0, "")
    w = ((0b0110 << 44) | (_dag1(ireg) << 41) | ((1 if write else 0) << 39) | (COND[cond] << 33)
         | ((offset & 0x3F) << 27) | (_r(dreg) << 23) | field)
    o = f"-0x{-offset:02X}" if offset < 0 else f"0x{offset:02X}"
    mv = f"DM({o}, {ireg}) = R{_r(dreg)}" if write else f"R{_r(dreg)} = DM({o}, {ireg})"
    return w, _if(cond) + (text + ",  " if text else "") + mv


def ureg_move(dst, src, cond="TRUE"):
    """type 5: dst = src (any two universal registers)"""
    return (0x7 << 44) | (_u(src) << 36) | (COND[cond] << 31) | (_u(dst) << 23), _if(cond) + f"{dst} = {src}"


def ureg_imm(dst, value):
    """type 17: ureg = 32-bit immediate"""
    return (0x0F << 40) | (_u(dst) << 32) | (value & 0xFFFFFFFF), f"{dst} = 0x{value & 0xFFFFFFFF:08X}"


def dm_read_abs(dst, addr):
    """type 14: ureg = DM(addr32)"""
    return (0x10 << 40) | (_u(dst) << 32) | (addr & 0xFFFFFFFF), f"{dst} = DM(0x{addr & 0xFFFFFFFF:08X})"


def dm_write_abs(addr, src):
    """type 14: DM(addr32) = ureg"""
    return (0x11 << 40) | (_u(src) << 32) | (addr & 0xFFFFFFFF), f"DM(0x{addr & 0xFFFFFFFF:08X}) = {src}"


def dm_read_off(dst, offset, ireg):
    """type 15: ureg = DM(data32, Ia) -- address Ia + data32, Ia not updated"""
    return ((0b101 << 45) | (_dag1(ireg) << 41) | (_u(dst) << 32) | (offset & 0xFFFFFFFF),
            f"{dst} = DM(0x{offset & 0xFFFFFFFF:08X}, {ireg})")


def dm_write_off(offset, ireg, src):
    """type 15: DM(data32, Ia) = ureg"""
    return ((0b101 << 45) | (_dag1(ireg) << 41) | (1 << 40) | (_u(src) << 32) | (offset & 0xFFFFFFFF),
            f"DM(0x{offset & 0xFFFFFFFF:08X}, {ireg}) = {src}")


def jump_abs(addr, cond="TRUE", call=False, db=False):
    """type 8: [IF cond] JUMP/CALL (addr24) [(DB)]"""
    w = ((0x06 << 40) | ((1 if call else 0) << 39) | (COND[cond] << 33) | ((1 if db else 0) << 26)
         | (addr & 0xFFFFFF))
    return w, _if(cond) + f"{'CALL' if call else 'JUMP'} (0x{addr:08X})" + (" (DB)" if db else "")


def jump_rel(offset, cond="TRUE"):
    """type 8, PC-relative: [IF cond] JUMP (PC + offset)"""
    return (0x07 << 40) | (COND[cond] << 33) | (offset & 0xFFFFFF), None


def rts(db=True):
    """type 11: RTS [(DB)]"""
    return (0x0A << 40) | (COND["TRUE"] << 33) | ((1 if db else 0) << 26), "RTS" + (" (DB)" if db else "")


def modify_imm(ireg, value):
    """type 19: MODIFY (Ia, data32)"""
    n = int(ireg[1:])
    return ((0x16 << 40) | ((1 if n >= 8 else 0) << 38) | ((n & 7) << 32) | (value & 0xFFFFFFFF),
            f"MODIFY ({ireg}, 0x{value & 0xFFFFFFFF:08X})")


class Program:
    """a list of (instruction word, the text the disassembler must print), with labels for jumps and calls"""

    def __init__(self, base):
        self.base = base
        self.items = []          # [word or (kind, label, cond), expected text]
        self.labels = {}

    @property
    def pc(self):
        return self.base + len(self.items)

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.pc

    def emit(self, ins):
        word, text = ins
        assert text is not None
        self.items.append([word, text])

    def jump(self, label, cond="TRUE"):
        """IF cond JUMP label (PC-relative, not delayed)"""
        self.items.append([("jump", label, cond), None])

    def call(self, label, cond="TRUE"):
        """IF cond CALL label (absolute, not delayed)"""
        self.items.append([("call", label, cond), None])

    def assemble(self, extern=None):
        """-> [(pc, word, text)]; labels not defined here are looked up in extern (absolute addresses)"""
        out = []
        for k, (w, text) in enumerate(self.items):
            pc = self.base + k
            if isinstance(w, tuple):
                kind, label, cond = w
                target = self.labels[label] if label in self.labels else extern[label]
                if kind == "call":
                    w, text = jump_abs(target, cond=cond, call=True)
                elif label in self.labels:
                    w, _ = jump_rel(target - pc, cond)
                    text = _if(cond) + f"JUMP (0x{target:08X})"
                else:
                    w, text = jump_abs(target, cond=cond)
            out.append((pc, w, text))
        return out
