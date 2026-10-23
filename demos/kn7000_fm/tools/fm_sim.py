#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# fm_sim.py -- runs KN7FM's DSP words (tools/fm_dsp.py) in a simulator of the SHARC instructions they use, beside
# the model (tools/fm_model.Engine), and stops at the first difference: does the DSP program compute, word for word,
# what the model computes?
#
#   python3 -I tools/fm_sim.py [--seed S] [--algs 1-32] [--seconds T] [--nv N] [--bank BANK.syx]
# For each algorithm: a random voice of compare.py's kind (tests/msfa_ref) with that algorithm, N voices played with
# notes starting and ending at random times (velocities, key-offs, voice steals), T seconds; every frame the
# sample (MIX) and every voice's words are compared with the model's. Prints per algorithm the frames run and the
# cycles a frame took (the most, and the mean), and stops with the first difference.
#
# The simulator decodes the instruction words themselves (not the assembler's text) as MAME's SHARC cores do
# (src/devices/cpu/sharc: sharcops.hxx, compute.hxx, sharcdrc.cpp), for the forms tools/sharc_enc.py emits. Where
# the chip and MAME differ the program must not go, and the simulator stops there: a fixed-point add, subtract or
# integer product that overflows (ALUSAT saturates on the chip, MAME's recompiler wraps), a read or write outside
# the memory the program owns, an instruction it does not know. Cycles: one an instruction, two more for each
# branch taken that is not delayed (the chip's pipeline), the kernel's own work not counted.
import argparse
import os
import random
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm_model as m  # noqa: E402
import fm_dsp as d  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'msfa_ref'))

F32 = np.float32
MASK = 0xFFFFFFFF
I4_BASE = 0x9800          # the unit's I4 in the simulation: its input at +0x20/0x21, its return pair at +0/+1
SENTINEL = 0xFFFF00       # the return address the simulated kernel CALLs a unit from


class SimError(Exception):
    pass


def s32(x):
    x &= MASK
    return x - (1 << 32) if x & 0x80000000 else x


def f_of(x):
    return struct.unpack('<f', struct.pack('<I', x & MASK))[0]


def bits_of(f):
    return struct.unpack('<I', struct.pack('<f', float(F32(f))))[0]


class Sharc:
    def __init__(self):
        self.r = [0] * 16                 # unsigned 32-bit words
        self.i = [0] * 16
        self.dm = {}
        self.pm = {}                      # address -> (word, closure)
        self.pc = 0
        self.stack = []
        self.az = self.an = self.af = self.sz = False
        self.cycles = 0
        self.writable = []                # [(lo, hi)] DM ranges the program may write
        self.readable = []

    # -- memory --
    def rd(self, a):
        if not any(lo <= a < hi for lo, hi in self.readable):
            raise SimError(f"DM read outside the program's memory: 0x{a:X} (PC 0x{self.pc:04X})")
        return self.dm.get(a, 0)

    def wr(self, a, v):
        if not any(lo <= a < hi for lo, hi in self.writable):
            raise SimError(f"DM write outside the program's memory: 0x{a:X} (PC 0x{self.pc:04X})")
        self.dm[a] = v & MASK

    def ureg_get(self, u):
        g, n = u >> 4, u & 15
        if g == 0:
            return self.r[n]
        if g == 1:
            return self.i[n]
        raise SimError(f"ureg {u:02X} (PC 0x{self.pc:04X})")

    def ureg_set(self, u, v):
        g, n = u >> 4, u & 15
        if g == 0:
            self.r[n] = v & MASK
        elif g == 1:
            if n not in (4, 5, 7, 8):
                raise SimError(f"the program writes I{n} (PC 0x{self.pc:04X})")
            self.i[n] = v & MASK
        else:
            raise SimError(f"ureg {u:02X} (PC 0x{self.pc:04X})")

    def cond(self, c):
        if c == 31:
            return True
        lt = (self.an and not self.az) if self.af else self.an
        le = self.az or self.an
        return {0: self.az, 16: not self.az, 1: lt, 17: not lt, 2: le, 18: not le, 8: self.sz,
                24: not self.sz}[c]

    # -- compute --
    def alu_flags(self, v, f=False):
        self.az = (v & 0x7FFFFFFF) == 0 if f else (v & MASK) == 0
        self.an = bool(v & 0x80000000)
        self.af = f

    def fixed(self, v):
        if not -(1 << 31) <= v < (1 << 31):
            raise SimError(f"fixed-point overflow ({v}) at PC 0x{self.pc:04X}: the chip saturates, MAME's recompiler "
                           f"wraps")
        v &= MASK
        self.alu_flags(v)
        return v

    def compute(self, field, check=True):
        r = self.r
        if field >> 22:                                     # multifunction
            op = (field >> 16) & 0x3F
            assert op in (4, 5), hex(op)
            rm, ra = (field >> 12) & 15, (field >> 8) & 15
            xm, ym = (field >> 6) & 3, ((field >> 4) & 3) + 4
            xa, ya = ((field >> 2) & 3) + 8, (field & 3) + 12
            prod = (s32(r[xm]) * s32(r[ym])) >> 31
            alu = (s32(r[xa]) + s32(r[ya])) if op == 4 else (s32(r[xa]) - s32(r[ya]))
            r[rm] = prod & MASK
            r[ra] = alu & MASK                              # the dummy half: never read, never checked
            self.alu_flags(alu & MASK)
            return
        cu, op = (field >> 20) & 3, (field >> 12) & 0xFF
        rn, rx, ry = (field >> 8) & 15, (field >> 4) & 15, field & 15
        X, Y = s32(r[rx]), s32(r[ry])
        if cu == 0:
            if op == 0x01:
                r[rn] = self.fixed(X + Y)
            elif op == 0x02:
                r[rn] = self.fixed(X - Y)
            elif op == 0x0A:
                self.fixed(X - Y)
            elif op == 0x21:
                r[rn] = self.fixed(X)
            elif op == 0x22:
                r[rn] = self.fixed(-X)
            elif op == 0x29:
                r[rn] = self.fixed(X + 1)
            elif op == 0x2A:
                r[rn] = self.fixed(X - 1)
            elif op == 0x40:
                r[rn] = self.fixed(s32(X & Y))
            elif op == 0x41:
                r[rn] = self.fixed(s32(X | Y))
            elif op == 0x42:
                r[rn] = self.fixed(s32(X ^ Y))
            elif op == 0x43:
                r[rn] = self.fixed(s32(~X))
            elif op == 0x61:
                r[rn] = self.fixed(min(X, Y))
            elif op == 0x62:
                r[rn] = self.fixed(max(X, Y))
            elif op == 0x63:
                a = abs(Y)
                r[rn] = self.fixed(max(-a, min(a, X)))
            elif op == 0x81:
                v = bits_of(F32(f_of(r[rx])) + F32(f_of(r[ry])))
                r[rn] = v
                self.alu_flags(v, True)
            elif op == 0xBD:
                v = bits_of(F32(f_of(r[rx]) * 2.0 ** Y))
                r[rn] = v
                self.alu_flags(v, True)
            elif op in (0xC9, 0xD9):
                f = float(F32(f_of(r[rx])))
                if op == 0xD9:
                    f = float(F32(f * 2.0 ** Y))
                v = int(np.rint(f))                         # nearbyintf, FE_TONEAREST
                r[rn] = self.fixed(v)
            elif op in (0xCA, 0xDA):
                f = F32(float(X))
                if op == 0xDA:
                    f = F32(float(f) * 2.0 ** Y)
                v = bits_of(f)
                r[rn] = v
                self.alu_flags(v, True)
            else:
                raise SimError(f"ALU op {op:02X} (PC 0x{self.pc:04X})")
        elif cu == 1:
            if op == 0x30:
                r[rn] = bits_of(F32(f_of(r[rx])) * F32(f_of(r[ry])))
            elif op == 0x70:
                v = X * Y
                if not -(1 << 31) <= v < (1 << 31):
                    raise SimError(f"integer product overflow ({X} x {Y}) at PC 0x{self.pc:04X}")
                r[rn] = v & MASK
            else:
                raise SimError(f"multiplier op {op:02X} (PC 0x{self.pc:04X})")
        elif cu == 2:
            if op == 0x00:
                sh = Y
                v = (r[rx] >> -sh if sh > -32 else 0) if sh < 0 else ((r[rx] << sh) & MASK if sh < 32 else 0)
            elif op == 0x40:
                pos, ln = Y & 0x3F, (Y >> 6) & 0x3F
                v = (r[rx] >> pos) & ((1 << ln) - 1) if ln and pos < 32 else 0
            else:
                raise SimError(f"shifter op {op:02X} (PC 0x{self.pc:04X})")
            r[rn] = v
            self.sz = v == 0
        else:
            raise SimError(f"compute unit {cu} (PC 0x{self.pc:04X})")

    def shift_imm(self, w):
        op = (w >> 16) & 0x3F
        data = ((w >> 8) & 0xFF) | (((w >> 27) & 0xF) << 8)
        rn, rx = (w >> 4) & 15, w & 15
        x = self.r[rx]
        if op in (0x00, 0x01):
            sh = data & 0xFF
            sh = sh - 256 if sh & 0x80 else sh
            assert -32 < sh < 32, sh
            if op == 0x00:
                v = x >> -sh if sh < 0 else (x << sh) & MASK
            else:
                v = (s32(x) >> -sh) & MASK if sh < 0 else (x << sh) & MASK
                if sh > 0 and s32(v) >> sh != s32(x):
                    raise SimError(f"ASHIFT left overflow at PC 0x{self.pc:04X}")
            self.r[rn] = v
        elif op == 0x10:
            pos, ln = data & 0x3F, (data >> 6) & 0x3F
            v = (x >> pos) & ((1 << ln) - 1)
            self.r[rn] = v
        elif op == 0x11:
            pos, ln = data & 0x3F, (data >> 6) & 0x3F
            v = ((x & ((1 << ln) - 1)) << pos) & MASK
            self.r[rn] = v
        elif op == 0x30:
            v = x | (1 << (data & 31))
            self.r[rn] = v
        elif op == 0x31:
            v = x & ~(1 << (data & 31)) & MASK
            self.r[rn] = v
        elif op == 0x33:
            v = x & (1 << (data & 31))
        else:
            raise SimError(f"immediate shift op {op:02X} (PC 0x{self.pc:04X})")
        self.sz = v == 0

    # -- instructions --
    def step(self):
        w = self.pm.get(self.pc)
        if w is None:
            raise SimError(f"no instruction at PC 0x{self.pc:04X}")
        self.cycles += 1
        nxt = self.pc + 1
        top = w >> 40
        if w == 0:                                          # NOP
            pass
        elif w == d.EPILOGUE:                               # R4 = DM(I1, M5), R7 = PM(I12, M8): the kernel's
            pass
        elif top == 0x01:                                   # type 2: IF cond compute
            if self.cond((w >> 33) & 31):
                self.compute(w & 0x7FFFFF)
        elif (w >> 44) == 0x6:                              # type 4: compute, dreg <-> DM(data6, Ia)
            i = (w >> 41) & 7
            write = (w >> 39) & 1
            off = (w >> 27) & 0x3F
            off = off - 64 if off & 0x20 else off
            dreg = (w >> 23) & 15
            if self.cond((w >> 33) & 31):
                a = self.i[i] + off
                if write:
                    v = self.r[dreg]
                    if w & 0x7FFFFF:
                        self.compute(w & 0x7FFFFF)
                    self.wr(a, v)
                else:
                    v = self.rd(a)
                    if w & 0x7FFFFF:
                        self.compute(w & 0x7FFFFF)
                    self.r[dreg] = v
        elif (w >> 44) == 0x7:                              # type 5: compute, ureg = ureg
            if self.cond((w >> 31) & 31):
                v = self.ureg_get((w >> 36) & 0xFF)
                if w & 0x7FFFFF:
                    self.compute(w & 0x7FFFFF)
                self.ureg_set((w >> 23) & 0xFF, v)
        elif top == 0x02:                                   # type 6: immediate shift
            if self.cond((w >> 33) & 31):
                self.shift_imm(w)
        elif top in (0x06, 0x07):                           # type 8: jump / call
            if self.cond((w >> 33) & 31):
                if top == 0x06:
                    target = w & 0xFFFFFF
                else:
                    off = w & 0xFFFFFF
                    target = self.pc + (off - (1 << 24) if off & 0x800000 else off)
                db = (w >> 26) & 1
                assert not db
                if (w >> 39) & 1 and top == 0x06:
                    self.stack.append(nxt)
                nxt = target
                self.cycles += 2
        elif top == 0x0A:                                   # type 11: RTS [(DB)]
            if (w >> 26) & 1:
                ret = self.stack.pop()
                for _ in range(2):                          # the two delayed instructions
                    self.pc += 1
                    self.step_one_inline()
                nxt = ret
            else:
                nxt = self.stack.pop()
                self.cycles += 2
        elif top in (0x10, 0x11):                           # type 14: ureg <-> DM(addr32)
            a = w & MASK
            if top == 0x10:
                self.ureg_set((w >> 32) & 0xFF, self.rd(a))
            else:
                self.wr(a, self.ureg_get((w >> 32) & 0xFF))
        elif (w >> 45) == 0b101:                            # type 15: ureg <-> DM(data32, Ia)
            i = (w >> 41) & 7
            a = (self.i[i] + (w & MASK)) & MASK
            if (w >> 40) & 1:
                self.wr(a, self.ureg_get((w >> 32) & 0xFF))
            else:
                self.ureg_set((w >> 32) & 0xFF, self.rd(a))
        elif top == 0x0F:                                   # type 17: ureg = data32
            self.ureg_set((w >> 32) & 0xFF, w & MASK)
        elif top == 0x16:                                   # type 19: MODIFY (Ia, data32)
            n = ((w >> 32) & 7) + (8 if (w >> 38) & 1 else 0)
            if n != 8:
                self.i[n] = (self.i[n] + (w & MASK)) & MASK
        else:
            raise SimError(f"unknown instruction {w:012X} at PC 0x{self.pc:04X}")
        self.pc = nxt

    def step_one_inline(self):
        w = self.pm[self.pc]
        if (w >> 40) in (0x06, 0x07, 0x0A):
            raise SimError(f"a branch in a delay slot at PC 0x{self.pc:04X}")
        save = self.stack[:]
        self.step()
        self.stack = save
        self.pc -= 1                                        # step() moved on; the caller moves

    def call(self, entry, limit=20000):
        self.stack.append(SENTINEL)
        self.pc = entry
        n = 0
        while self.pc != SENTINEL:
            self.step()
            n += 1
            if n > limit:
                raise SimError("runaway")
        if self.stack:
            raise SimError(f"stack not empty on return: {self.stack}")


class Rig:
    """the DSP program loaded as the host loads it, and the model beside it"""

    def __init__(self, patch, nv):
        self.s = s = Sharc()
        w1, w2, self.labels, self.algs = d.build()
        for pc, w, _ in w1 + w2:
            s.pm[pc] = w
        self.buf = 0
        self.load_alg(patch.get('ALG') & 31)
        for a, words in d.dm_tables().items():
            for k, x in enumerate(words):
                s.dm[a + k] = x & MASK
        for k, x in enumerate(d.initial_globals(nv)):
            s.dm[d.GLOBALS + k] = x
        for v in range(m.NVOICES):
            for k, x in enumerate(d.voice_image()):
                s.dm[d.VOICES + v * d.VOICE_LEN + k] = x
        self.set_patch(patch)
        s.writable = [(d.VOICES, d.VOICES + m.NVOICES * d.VOICE_LEN), (d.GLOBALS, d.GLOBALS_END),
                      (d.MSGR6, d.MSGR7 + 1), (I4_BASE, I4_BASE + 2)]
        s.readable = s.writable + [(d.SINE, d.SINE + len(m.SIN_TABLE)), (d.PATCH, d.PATCH_END),
                                   (d.STATICS, d.STATICS + 100), (d.STAGE, d.STAGE_END),
                                   (I4_BASE + 0x20, I4_BASE + 0x22)]
        s.dm[I4_BASE + 0x20] = 0
        s.dm[I4_BASE + 0x21] = 0
        self.e = m.Engine(nv)
        self.e.set_patch(patch)
        s.dm[d.G["VOL"]] = self.e.vol = m.volume_word(m.VOL_DEFAULT)    # the editor's default, not full
        self.patch = patch

    def load_alg(self, alg):
        """as the host does: the kernel into the buffer not running, then the trampoline"""
        self.buf ^= 1
        base = d.PM_ALG[self.buf]
        for k, (_, w, _) in enumerate(self.algs[alg]):
            self.s.pm[base + k] = w
        self.s.pm[self.labels["TRAMP"]] = d.jump_abs(base)[0]

    def set_patch(self, patch):
        for k, x in enumerate(d.patch_image(m.patch_words(patch))):
            self.s.dm[d.PATCH + k] = x

    def key_on(self, v, note, vel):
        pitch, olrs, state = m.note_words(self.patch, note, vel)
        for k, x in enumerate(pitch + olrs + state):
            self.s.dm[d.STAGE + k] = x & MASK
        self.s.dm[d.MSGR6] = self.s.dm.get(d.MSGR6, 0) | (1 << v)
        self.e.key_on(v, self.patch, note, vel)

    def key_off(self, v):
        self.s.dm[d.MSGR6] = self.s.dm.get(d.MSGR6, 0) | (1 << (8 + v))
        self.e.key_off(v)

    def frame(self):
        s = self.s
        s.i[4] = I4_BASE
        c0 = s.cycles
        s.call(self.labels["E0"])
        s.i[4] = I4_BASE
        s.call(self.labels["E9"])
        if s.dm.get(d.MSGR6, 0):
            raise SimError("MSGR6 not cleared")
        out = self.e.frame()
        got = s32(s.dm[d.G["MIX"]])
        return got, out, s.cycles - c0

    def compare_state(self):
        """every word of every voice, and the LFO's, against the model"""
        s, e = self.s, self.e
        for vn, v in enumerate(e.v):
            b = d.VOICES + vn * d.VOICE_LEN + 32
            for k in range(6):
                for name, off, val in (("PH", d.O_PH, v.ph[k]), ("INC", d.O_INC, v.inc[k]), ("G", d.O_G, v.g[k]),
                                       ("DG", d.O_DG, v.dg[k]), ("LEVEL", d.O_LEVEL, v.level[k]),
                                       ("STATE", d.O_STATE, v.state[k]), ("PITCH", d.O_PITCH, v.pitch[k]),
                                       ("OLRS", d.O_OLRS, v.olrs[k]), ("PMOD", d.O_PMOD, v.pmod[k])):
                    if s.dm.get(b + off + k, 0) != val & MASK:
                        return f"voice {vn} op {k} {name}: DSP 0x{s.dm.get(b + off + k, 0):08X}, model 0x{val & MASK:08X}"
            for name, off, val in (("FB0", d.O_FB0, v.fb[0]), ("FB1", d.O_FB1, v.fb[1]),
                                   ("ACTIVE", d.O_ACTIVE, int(v.active)), ("PEGL", d.O_PEGL, v.peg_level),
                                   ("PEGIX", d.O_PEGIX, v.peg_ix)):
                if s.dm.get(b + off, 0) != val & MASK:
                    return f"voice {vn} {name}: DSP 0x{s.dm.get(b + off, 0):08X}, model 0x{val & MASK:08X}"
        for name, val in (("LFO_PH", e.lfo_phase), ("LFO_VAL", e.lfo_val), ("LFO_DST", e.lfo_delay_state),
                          ("LFO_DLY", e.lfo_delay), ("LFO_RAND", e.lfo_rand), ("AMOD", e.amod)):
            if s.dm.get(d.G[name], 0) != val & MASK:
                return f"{name}: DSP 0x{s.dm.get(d.G[name], 0):08X}, model 0x{val & MASK:08X}"
        return None


def events(rng, nv, frames):
    """random notes: (frame, 'on'/'off', voice, note, velocity), a key-on only on a frame without another command
    pending (the host waits for MSGR6 = 0), voices stolen when all are busy"""
    ev, busy = [], {}
    f = 0
    while f < frames:
        f += rng.randint(30, 3000)
        if f >= frames:
            break
        free = [v for v in range(nv) if v not in busy]
        if free and rng.random() < 0.7:
            v = rng.choice(free)
            ev.append((f, 'on', v, rng.randint(36, 96), rng.randint(1, 127)))
            busy[v] = f
        elif busy:
            v = rng.choice(sorted(busy))
            ev.append((f, 'off', v, 0, 0))
            del busy[v]
        else:
            v = rng.randrange(nv)
            ev.append((f, 'on', v, rng.randint(36, 96), rng.randint(1, 127)))
            busy[v] = f
    return ev


def run(patch, ev, frames, nv, every=1):
    rig = Rig(patch, nv)
    by = {}
    for x in ev:
        by.setdefault(x[0], []).append(x)
    worst, total = 0, 0
    for f in range(frames):
        for _, what, v, note, vel in by.get(f, []):
            if what == 'on':
                rig.key_on(v, note, vel)
            else:
                rig.key_off(v)
        got, want, cyc = rig.frame()
        worst, total = max(worst, cyc), total + cyc
        if got != want:
            raise SimError(f"frame {f}: DSP sample {got}, model {want}")
        if f % every == 0:
            diff = rig.compare_state()
            if diff:
                raise SimError(f"frame {f}: {diff}")
    return worst, total / frames


def parse_range(s):
    if '-' in s:
        a, b = s.split('-')
        return list(range(int(a), int(b) + 1))
    return [int(x) for x in s.split(',')]


def main():
    import compare
    ap = argparse.ArgumentParser()
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--algs', default='1-32')
    ap.add_argument('--seconds', type=float, default=0.5)
    ap.add_argument('--nv', type=int, default=m.NVOICES)
    ap.add_argument('--every', type=int, default=1, help='compare the state every N frames')
    ap.add_argument('--bank')
    a = ap.parse_args()
    frames = int(a.seconds * m.FS)
    worst_all = 0
    for alg in parse_range(a.algs):
        rng = random.Random(a.seed * 1000 + alg)
        if a.bank:
            patch = m.Bank.from_sysex(open(a.bank, 'rb').read()).patches[(alg - 1) % 32]
        else:
            patch = compare.random_patch(rng, alg - 1)
        ev = events(rng, a.nv, frames)
        try:
            worst, mean = run(patch, ev, frames, a.nv, a.every)
        except SimError as x:
            sys.exit(f"alg {alg:2d} ({patch.name}): {x}")
        worst_all = max(worst_all, worst)
        print(f"alg {alg:2d} {patch.name}: {frames} frames, {len(ev)} key events identical; cycles a frame: most "
              f"{worst}, mean {mean:.0f}", flush=True)
    print(f"all identical; the most cycles in a frame: {worst_all}")


if __name__ == '__main__':
    main()
