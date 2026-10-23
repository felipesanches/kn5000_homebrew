#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# fm_model.py -- the reference model of KN7FM, the DX7-style FM synthesizer for the KN7000 (README.md).
#
# Three things live here, and everything else in the project is built from them or checked against them:
#
#   1. The DX7 voice formats: the 155-byte "unpacked" voice (VCED, what a single-voice SysEx dump carries), the
#      128-byte packed voice of a 32-voice bank (VMEM), and the two SysEx dumps that carry them (Patch, Bank).
#   2. What the main CPU computes (the MN10300 code in src/ does exactly this, in integers): per patch, the words
#      the DSP reads while voices sound (patch_words); per note, each operator's pitch, output level and rate
#      scaling (note_words). These follow msfa, the DX7 engine Raph Levien wrote at Google (Apache License 2.0),
#      as Dexed carries it (github.com/asb2m10/dexed, source/msfa: dx7note.cc, env.cc, lfo.cc, pitchenv.cc,
#      fm_core.cc). The tables below are msfa's, and so are the formulas, unless a comment says otherwise.
#   3. The DSP engine, bit for bit: the fixed-point arithmetic the SHARC program (tools/fm_dsp.py) runs every
#      frame, 44,100 a second. tools/fm_sim.py runs the generated SHARC words in a simulator of the instructions
#      they use and must produce exactly the samples Engine.frame() produces here.
#
# The engine follows msfa's structure with the DSP's budget in mind:
#   * each operator: a 24-bit phase (Q24: 1 << 24 is one cycle) stepped by its frequency every frame; a sine from a
#     256-entry table with linear interpolation; times a gain that moves linearly from one control value to the next
#     over 64 frames (msfa interpolates over its 64-sample blocks the same way);
#   * the 32 algorithms as msfa encodes them (two buses, an output, self-feedback on one operator): FmCore below.
#     The DSP runs code generated for the patch's algorithm; it is not interpreted per sample;
#   * the control path (envelopes, gains, frequencies, the pitch envelope and the LFO) runs one operator of one voice
#     a frame, so every operator is updated once every 64 frames: msfa's control rate (N = 64) exactly, which keeps
#     msfa's envelope increments as they are;
#   * gains and frequencies are 2 ** x: computed in float (a cubic for 2 ** f on [0, 1), then SCALB), as the SHARC
#     does it, where msfa uses tables.
# Where it departs from msfa (named so the next reader does not have to find them):
#   * every operator is computed every frame (msfa skips operators whose gain is under -83 dB);
#   * the sine is 256 points with linear interpolation (msfa: 1,024 with interpolation); the gain's exp2 is a cubic
#     (msfa: 1,024 points with interpolation);
#   * carriers are scaled 1/32 of a modulator (their outputs are Q19 where msfa's are Q24) so that eight voices of six
#     carriers sum without overflow; one carrier at full level (output level 99: gain 2.0) is then -18 dBFS at the
#     DSP's output, the level of one of Another World's voices, which Felipe found "close to a piano note's".
import math
import struct
import sys

import numpy as np

# ---- msfa's tables (Apache License 2.0, Google; via Dexed source/msfa) -----------------------------------------
COARSEMUL = [
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
    50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
    65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
    74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032,
    81503396, 82323963, 83117622]
VELOCITY_DATA = [
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
    166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202,
    205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232,
    233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251,
    252, 253, 254]
EXP_SCALE_DATA = [
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
    80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250]
LEVELLUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46]
# msfa env.cc ACCURATE_ENVELOPE: how many samples a stage whose level does not change lasts, by rate (+ rate scaling),
# "empirically gathered using 2 TF1s" up to rate 76; from 77 on, 20 x (99 - rate)
STATICS = [
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250,
    882000, 705600, 705600, 584325, 507150, 502740, 441000, 418950,
    352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530,
    145530, 125685, 110250, 110250, 88200, 88200, 74970, 61740,
    61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562,
    22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717,
    6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249,
    1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705,
    573, 573, 529, 441, 441]
STATICS += [20 * (99 - r) for r in range(len(STATICS), 100)]
PITCHMODSENSTAB = [0, 10, 20, 33, 55, 92, 153, 255]
AMPMODSENSTAB = [0, 4342338, 7171437, 16777216]
PITCHENV_RATE = [
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
    12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47,
    49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82,
    85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147,
    153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255]
PITCHENV_TAB = [
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43,
    -41, -39, -37, -35, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24,
    -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10,
    -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73,
    82, 92, 103, 115, 127]
LFO_SOURCE = [
    0.062541, 0.125031, 0.312393, 0.437120, 0.624610,
    0.750694, 0.936330, 1.125302, 1.249609, 1.436782,
    1.560915, 1.752081, 1.875117, 2.062494, 2.247191,
    2.374451, 2.560492, 2.686728, 2.873976, 2.998950,
    3.188013, 3.369840, 3.500175, 3.682224, 3.812065,
    4.000800, 4.186202, 4.310716, 4.501260, 4.623209,
    4.814636, 4.930480, 5.121901, 5.315191, 5.434783,
    5.617346, 5.750431, 5.946717, 6.062811, 6.248438,
    6.431695, 6.564264, 6.749460, 6.868132, 7.052186,
    7.250580, 7.375719, 7.556294, 7.687577, 7.877738,
    7.993605, 8.181967, 8.372405, 8.504848, 8.685079,
    8.810573, 8.986341, 9.122423, 9.300595, 9.500285,
    9.607994, 9.798158, 9.950249, 10.117361, 11.251125,
    11.384335, 12.562814, 13.676149, 13.904338, 15.092062,
    16.366612, 16.638935, 17.869907, 19.193858, 19.425019,
    20.833333, 21.034918, 22.502250, 24.003841, 24.260068,
    25.746653, 27.173913, 27.578599, 29.052876, 30.693677,
    31.191516, 32.658393, 34.317090, 34.674064, 36.416606,
    38.197097, 38.550501, 40.387722, 40.749796, 42.625746,
    44.326241, 44.883303, 46.772685, 48.590865, 49.261084]
# The 32 algorithms, msfa's FmCore::algorithms: six flag bytes, OP6 first. Bits 0-1: the bus the operator writes
# (0 = the output, 1, 2); bit 2: add to it (else replace); bits 4-5: the bus it reads (0 = none); bits 6+7 (0xC0):
# feedback on this operator (msfa treats algorithms 4 and 6, whose loop spans two operators, as self-feedback too).
ALGORITHMS = [
    [0xc1, 0x11, 0x11, 0x14, 0x01, 0x14], [0x01, 0x11, 0x11, 0x14, 0xc1, 0x14],
    [0xc1, 0x11, 0x14, 0x01, 0x11, 0x14], [0xc1, 0x11, 0x94, 0x01, 0x11, 0x14],
    [0xc1, 0x14, 0x01, 0x14, 0x01, 0x14], [0xc1, 0x94, 0x01, 0x14, 0x01, 0x14],
    [0xc1, 0x11, 0x05, 0x14, 0x01, 0x14], [0x01, 0x11, 0xc5, 0x14, 0x01, 0x14],
    [0x01, 0x11, 0x05, 0x14, 0xc1, 0x14], [0x01, 0x05, 0x14, 0xc1, 0x11, 0x14],
    [0xc1, 0x05, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x05, 0x14, 0xc1, 0x14],
    [0xc1, 0x05, 0x05, 0x14, 0x01, 0x14], [0xc1, 0x05, 0x11, 0x14, 0x01, 0x14],
    [0x01, 0x05, 0x11, 0x14, 0xc1, 0x14], [0xc1, 0x11, 0x02, 0x25, 0x05, 0x14],
    [0x01, 0x11, 0x02, 0x25, 0xc5, 0x14], [0x01, 0x11, 0x11, 0xc5, 0x05, 0x14],
    [0xc1, 0x14, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x14, 0xc1, 0x14, 0x14],
    [0x01, 0x14, 0x14, 0xc1, 0x14, 0x14], [0xc1, 0x14, 0x14, 0x14, 0x01, 0x14],
    [0xc1, 0x14, 0x14, 0x01, 0x14, 0x04], [0xc1, 0x14, 0x14, 0x14, 0x04, 0x04],
    [0xc1, 0x14, 0x14, 0x04, 0x04, 0x04], [0xc1, 0x05, 0x14, 0x01, 0x14, 0x04],
    [0x01, 0x05, 0x14, 0xc1, 0x14, 0x04], [0x04, 0xc1, 0x11, 0x14, 0x01, 0x14],
    [0xc1, 0x14, 0x01, 0x14, 0x04, 0x04], [0x04, 0xc1, 0x11, 0x14, 0x04, 0x04],
    [0xc1, 0x14, 0x04, 0x04, 0x04, 0x04], [0xc4, 0x04, 0x04, 0x04, 0x04, 0x04]]

# ---- the engine's constants (shared with tools/fm_dsp.py and the MN10300 code) ---------------------------------
FS = 44100
NVOICES = 8
CTRL_N = 64                   # frames between two control updates of one operator (msfa's block size N)
LG_N = 6
SIN_BITS = 8                  # the sine table: 2 ** SIN_BITS points (+1 for the wrap), Q30
SIN_N = 1 << SIN_BITS
GSHIFT_MOD = 1                # a modulator's gain: msfa's Q24 gain x 2 (its output is Q24, as msfa adds it to a phase)
GSHIFT_CAR = -4               # a carrier's: / 16 (its output Q19: at full level, gain 2.0, one carrier is -18 dBFS)
GAIN_EXP = 10                 # 2 ** (level / 2 ** 24 - 14) in Q24, as msfa's Exp2::lookup(level - (14 << 24)): + 24 - 14
FREQ_K = round(math.log2((1 << 24) / FS) * (1 << 24))   # logfreq + FREQ_K -> the Q24 phase step a frame
PITCH_BASE = 50857777         # Dexed's standard tuning: midinote_to_logfreq(n) = PITCH_BASE + n * PITCH_STEP
PITCH_STEP = (1 << 24) // 12
FEEDBACK_BITDEPTH = 8
OUT_MAX = (1 << 23) - 1       # the DSP's output words are 24-bit
MASK24 = (1 << 24) - 1


def _sin_table():
    return [int(round(math.sin(2 * math.pi * i / SIN_N) * (1 << 30))) for i in range(SIN_N + 1)]


SIN_TABLE = _sin_table()


def _exp2_cubic():
    """the cubic p(f) = 1 + c1 f + c2 f^2 + c3 f^3 closest to 2 ** f on [0, 1] with p(1) = 2 exactly (least squares,
    in float32 as the SHARC will hold it)"""
    f = np.linspace(0, 1, 4001)
    y = 2 ** f - 1 - f            # with c1 = 1 - c2 - c3: p(f) - 1 - f = c2 (f^2 - f) + c3 (f^3 - f)
    a = np.stack([f ** 2 - f, f ** 3 - f], axis=1)
    c2, c3 = np.linalg.lstsq(a, y, rcond=None)[0]
    c1 = 1 - c2 - c3
    return [float(np.float32(c)) for c in (c1, c2, c3)]


EXP2_C = _exp2_cubic()

F32 = np.float32
LOG2E = 1 / math.log(2)
AM_K0 = round((12.2 * LOG2E - 24) * (1 << 24))         # msfa's AM: 2 ** (AM_K0 + AM_K1 sensamp), in Q24
AM_K1 = round(0.07 / 262144 * LOG2E * (1 << 24) * 16)   # per 16 of sensamp


def exp2_f32(x):
    """2 ** (x / 2 ** 24) as the float32 the SHARC holds before FIX (exp2_q24 without the last step)"""
    i = x >> 24
    f = F32(x & MASK24) * F32(1.0 / (1 << 24))
    c1, c2, c3 = (F32(c) for c in EXP2_C)
    p = F32(F32(F32(F32(c3 * f) + c2) * f) + c1)
    p = F32(F32(p * f) + F32(1.0))
    return F32(float(p) * (2.0 ** i))


def fix(v):
    """the SHARC's FIX with MODE1.TRUNC = 0, as the kernel leaves it: round to nearest, ties to even (MAME:
    nearbyintf, ROUND_ROUND)"""
    return int(round(float(v)))


def exp2_q24(x, shift):
    """2 ** (x / 2 ** 24) * 2 ** shift the way the SHARC computes it: i = x >> 24 (floor), f = (x & 0xFFFFFF) as a
    float32 / 2 ** 24, p = ((c3 f + c2) f + c1) f + 1 in float32, then FIX p BY (i + shift)"""
    i = x >> 24
    f = F32(x & MASK24) * F32(1.0 / (1 << 24))
    c1, c2, c3 = (F32(c) for c in EXP2_C)
    p = F32(F32(F32(F32(c3 * f) + c2) * f) + c1)
    p = F32(F32(p * f) + F32(1.0))
    v = float(p) * (2.0 ** (i + shift))
    return fix(v) if v < 2 ** 31 else 2 ** 31 - 1


# ---- the DX7 voice ---------------------------------------------------------------------------------------------
# The unpacked voice (VCED): 21 bytes per operator, OP6 first, then the voice's own.
OP_FIELDS = ['R1', 'R2', 'R3', 'R4', 'L1', 'L2', 'L3', 'L4', 'BP', 'LD', 'RD', 'LC', 'RC', 'RS', 'AMS', 'KVS', 'OL',
             'MODE', 'FC', 'FF', 'DET']
OP_MAX = [99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14]
VOICE_FIELDS = ['PR1', 'PR2', 'PR3', 'PR4', 'PL1', 'PL2', 'PL3', 'PL4', 'ALG', 'FB', 'OKS', 'LFS', 'LFD', 'LPMD',
                'LAMD', 'LFKS', 'LFW', 'LPMS', 'TRNSP']
VOICE_MAX = [99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48]
VCED_LEN = 155


class Patch:
    """an unpacked DX7 voice: .data = 155 bytes (126 operator bytes, OP6 first; 19 voice bytes; 10 name bytes)"""

    def __init__(self, data=None):
        self.data = bytearray(data) if data is not None else bytearray(INIT_VOICE)
        assert len(self.data) == VCED_LEN

    def op(self, n, field):
        """operator n (1..6, as the DX7 names them), a field of OP_FIELDS"""
        return self.data[(6 - n) * 21 + OP_FIELDS.index(field)]

    def set_op(self, n, field, v):
        self.data[(6 - n) * 21 + OP_FIELDS.index(field)] = v

    def get(self, field):
        return self.data[126 + VOICE_FIELDS.index(field)]

    def set(self, field, v):
        self.data[126 + VOICE_FIELDS.index(field)] = v

    @property
    def name(self):
        return bytes(self.data[145:155]).decode('ascii', 'replace')

    @name.setter
    def name(self, s):
        self.data[145:155] = s.encode('ascii')[:10].ljust(10)

    def packed(self):
        """the 128-byte packed form of a 32-voice bank (VMEM)"""
        d, out = self.data, bytearray()
        for k in range(6):
            o = d[k * 21:(k + 1) * 21]
            out += o[0:11]
            out.append((o[12] << 2) | o[11])            # RC, LC
            out.append((o[20] << 3) | o[13])            # DET, RS
            out.append((o[15] << 2) | o[14])            # KVS, AMS
            out.append(o[16])                           # OL
            out.append((o[18] << 1) | o[17])            # FC, MODE
            out.append(o[19])                           # FF
        v = d[126:145]
        out += v[0:8]
        out.append(v[8])                                # ALG
        out.append((v[10] << 3) | v[9])                 # OKS, FB
        out += v[11:15]                                 # LFS, LFD, LPMD, LAMD
        out.append((v[17] << 4) | (v[16] << 1) | v[15])  # LPMS, LFW, LFKS
        out.append(v[18])                               # TRNSP
        out += d[145:155]
        assert len(out) == 128
        return bytes(out)

    @classmethod
    def from_packed(cls, p):
        d = bytearray()
        for k in range(6):
            o = p[k * 17:(k + 1) * 17]
            d += o[0:11]
            d += bytes([o[11] & 3, (o[11] >> 2) & 3, o[12] & 7, o[13] & 3, (o[13] >> 2) & 7, o[14],
                        o[15] & 1, (o[15] >> 1) & 31, o[16], (o[12] >> 3) & 15])
        v = p[102:118]
        d += v[0:8]
        d += bytes([v[8] & 31, v[9] & 7, (v[9] >> 3) & 1, v[10], v[11], v[12], v[13], v[14] & 1,
                    (v[14] >> 1) & 7, (v[14] >> 4) & 7, v[15]])
        d += p[118:128]
        return cls(clamp_voice(d))

    def sysex(self, channel=0):
        """a single-voice dump: F0 43 0n 00 01 1B, the 155 bytes, the checksum, F7"""
        return bytes([0xF0, 0x43, channel & 15, 0x00, 0x01, 0x1B]) + bytes(self.data) + bytes(
            [(-sum(self.data)) & 0x7F, 0xF7])


def clamp_voice(d):
    """every field of an unpacked voice limited to the DX7's range (a damaged bank must not crash anything)"""
    d = bytearray(d)
    for k in range(6):
        for f, m in enumerate(OP_MAX):
            d[k * 21 + f] = min(d[k * 21 + f], m)
    for f, m in enumerate(VOICE_MAX):
        d[126 + f] = min(d[126 + f], m)
    for k in range(145, 155):
        d[k] = d[k] if 32 <= d[k] < 127 else 32
    return d


# the DX7's INIT VOICE: OP1 a sine at full level, the others silent
INIT_VOICE = bytearray()
for _op in range(6, 0, -1):
    INIT_VOICE += bytes([99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 99 if _op == 1 else 0, 0, 1, 0, 7])
INIT_VOICE += bytes([99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24]) + b'INIT VOICE'


class Bank:
    """32 voices; .sysex() is the DX7's 32-voice bulk dump (4,104 bytes)"""

    def __init__(self, patches=None):
        self.patches = list(patches) if patches else [Patch() for _ in range(32)]
        while len(self.patches) < 32:
            self.patches.append(Patch())

    def sysex(self, channel=0):
        body = b''.join(p.packed() for p in self.patches[:32])
        return bytes([0xF0, 0x43, channel & 15, 0x09, 0x20, 0x00]) + body + bytes([(-sum(body)) & 0x7F, 0xF7])

    @classmethod
    def from_sysex(cls, b):
        """a 32-voice bulk dump; also accepts a bare 4,096-byte VMEM image"""
        if len(b) == 4096:
            body = b
        else:
            k = b.find(b'\xF0\x43')
            if k < 0 or b[k + 3] != 0x09:
                raise ValueError('not a DX7 32-voice dump')
            body = b[k + 6:k + 6 + 4096]
            if len(body) != 4096:
                raise ValueError('short dump')
        return cls([Patch.from_packed(body[i * 128:(i + 1) * 128]) for i in range(32)])


# ---- the main CPU's side: what it sends the DSP ----------------------------------------------------------------
def scaleoutlevel(ol):
    return 28 + ol if ol >= 20 else LEVELLUT[ol]


def scale_velocity(velocity, sensitivity):
    clamped = max(0, min(127, velocity))
    vel_value = VELOCITY_DATA[clamped >> 1] - 239
    return ((sensitivity * vel_value + 7) >> 3) << 4


def scale_rate(note, sensitivity):
    x = min(31, max(0, note // 3 - 7))
    return (sensitivity * x) >> 3


def scale_curve(group, depth, curve):
    if curve == 0 or curve == 3:
        scale = (group * depth * 329) >> 12
    else:
        raw = EXP_SCALE_DATA[min(group, len(EXP_SCALE_DATA) - 1)]
        scale = (raw * depth * 329) >> 15
    return -scale if curve < 2 else scale


def _div_trunc(a, b):
    """C's integer division (towards zero), which msfa's ScaleLevel uses on a negative offset"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def scale_level(note, bp, ld, rd, lc, rc):
    offset = note - bp - 17
    if offset >= 0:
        return scale_curve(_div_trunc(offset + 1, 3), rd, rc)
    return scale_curve(_div_trunc(-(offset - 1), 3), ld, lc)


def detune_table():
    """msfa's detune for a ratio-mode operator, per MIDI note: round(0.0209 e^(-0.396 L) / 7 x logfreq) at detune 8,
    with L the note's log frequency in octaves; the MN10300 multiplies it by (detune - 7) (a table: no float there)"""
    out = []
    for n in range(128):
        lf = PITCH_BASE + n * PITCH_STEP
        ratio = 0.0209 * math.exp(-0.396 * (lf / (1 << 24))) / 7
        out.append(ratio * lf)
    return out


DETUNE = detune_table()
FINE_TAB = [0] + [int(math.floor(24204406.323123 * math.log(1 + 0.01 * f) + 0.5)) for f in range(1, 100)]


def osc_freq(note, mode, coarse, fine, detune):
    """msfa Dx7Note::osc_freq, standard tuning: an operator's log frequency, Q24 octaves (and with detune in integers:
    the per-note table times (detune - 7), truncated as msfa's int conversion truncates)"""
    if mode == 0:
        logfreq = PITCH_BASE + note * PITCH_STEP
        logfreq += int(DETUNE[note] * (detune - 7))
        logfreq += COARSEMUL[coarse & 31]
        logfreq += FINE_TAB[fine]
    else:
        logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3
        logfreq += 13457 * (detune - 7) if detune > 7 else 0
    return logfreq


def note_words(patch, note, velocity):
    """per operator (OP6 first): its log frequency, its output level and its rate scaling, as msfa's Dx7Note::init
    computes them. The note is the key plus the patch's TRANSPOSE (24 = none)."""
    note = max(0, min(127, note + patch.get('TRNSP') - 24))
    pitch, outlevel, rs = [], [], []
    for k in range(6):
        d = patch.data[k * 21:(k + 1) * 21]
        ol = scaleoutlevel(d[16])
        ol += scale_level(note, d[8], d[9], d[10], d[11], d[12])
        ol = min(127, ol) << 5
        ol += scale_velocity(velocity, d[15])
        outlevel.append(max(0, ol))
        rs.append(scale_rate(note, d[13]))
        pitch.append(osc_freq(note, d[17], d[18], d[19], d[20]))
    return pitch, outlevel, rs


def peg_unit():
    """msfa PitchEnv::init: unit_ = N (1 << 24) / (21.3 fs) + 0.5"""
    return int(CTRL_N * (1 << 24) / (21.3 * FS) + 0.5)


def lfo_words(patch):
    """the LFO's per-patch words (msfa Lfo::reset) in the DSP's units: the phase step a control tick and the two delay
    steps, each msfa's 32-bit value >> 8 (the DSP keeps the LFO's phase and delay state in 24 bits: its adds saturate,
    so nothing may rely on a 32-bit wrap), the waveform, sync"""
    lforatio = 4437500000.0 * CTRL_N / FS
    delta = int(LFO_SOURCE[patch.get('LFS')] * lforatio) & 0xFFFFFFFF
    a = 99 - patch.get('LFD')
    unit = int(CTRL_N * 25190424 / FS + 0.5)
    if a == 99:
        d1 = d2 = 0xFFFFFFFF
    else:
        a = (16 + (a & 15)) << (1 + (a >> 4))
        d1 = (unit * a) & 0xFFFFFFFF
        a &= 0xff80
        a = max(0x80, a)
        d2 = (unit * a) & 0xFFFFFFFF
    return delta >> 8, d1 >> 8, d2 >> 8, patch.get('LFW'), patch.get('LFKS')


def patch_words(patch):
    """what the DSP reads from the patch while voices sound (the MN10300 uploads it at every patch change and edit)"""
    alg = patch.get('ALG') & 31
    flags = ALGORITHMS[alg]
    w = {'alg': alg}
    w['lvl'] = [[((scaleoutlevel(patch.data[k * 21 + 4 + s]) >> 1) << 6) - 4256 for s in range(4)] for k in range(6)]
    w['qrate'] = [[(patch.data[k * 21 + s] * 41) >> 6 for s in range(4)] for k in range(6)]
    w['l4zero'] = [patch.data[k * 21 + 7] == 0 for k in range(6)]
    w['r'] = [[patch.data[k * 21 + s] for s in range(4)] for k in range(6)]
    w['l'] = [[patch.data[k * 21 + 4 + s] for s in range(4)] for k in range(6)]
    w['gshift'] = [GSHIFT_CAR if (f & 3) == 0 else GSHIFT_MOD for f in flags]
    w['mode'] = [patch.data[k * 21 + 17] for k in range(6)]
    w['ams'] = [AMPMODSENSTAB[patch.data[k * 21 + 14] & 3] for k in range(6)]
    fb = patch.get('FB')
    w['fb_shift'] = FEEDBACK_BITDEPTH - fb if fb != 0 else 16
    unit = peg_unit()
    w['peg_rate'] = [PITCHENV_RATE[patch.get('PR%d' % (s + 1))] * unit for s in range(4)]
    w['peg_level'] = [PITCHENV_TAB[patch.get('PL%d' % (s + 1))] << 19 for s in range(4)]
    pmd = (patch.get('LPMD') * 165) >> 6
    pms = PITCHMODSENSTAB[patch.get('LPMS') & 7]
    # msfa: ((pmd x lfo_delay) x (pms x (lfo_val - 2^23))) >> 39; here one float32 factor for the DSP's product
    w['pm_k'] = float(F32(pmd * pms / float(1 << 39)))
    w['amd'] = (patch.get('LAMD') * 165) >> 6
    w['lfo'] = lfo_words(patch)
    w['oks'] = patch.get('OKS')
    return w


# ---- the DSP engine, as the SHARC program computes it ----------------------------------------------------------
def i32(x):
    x &= 0xFFFFFFFF
    return x - (1 << 32) if x & 0x80000000 else x


def ssfr(a, b):
    """the multiplier's fractional product: floor(a * b / 2 ** 31), the low 32 bits (MAME: mul_32x32_shift / DMULS)"""
    return i32((a * b) >> 31)


class Voice:
    def __init__(self):
        self.ph = [0] * 6
        self.inc = [0] * 6
        self.g = [0] * 6
        self.dg = [0] * 6
        self.fb = [0, 0]
        self.level = [0] * 6        # envelope level, Q24 log units as msfa (actual level << 16)
        self.ix = [4] * 6           # envelope stage: 0-3, 4 = done
        self.static = [0] * 6       # msfa's staticcount_: samples left of a stage whose level does not change
        self.target = [0] * 6
        self.einc = [0] * 6
        self.rising = [False] * 6
        self.pitch = [0] * 6
        self.outlevel = [0] * 6
        self.rs = [0] * 6
        self.peg_level = 0
        self.peg_ix = 4
        self.peg_target = 0
        self.peg_inc = 0
        self.peg_rising = False
        self.pmod = 0
        self.down = False
        self.active = False


class Engine:
    """NVOICES voices; frame() returns one output sample (24-bit), as the DSP writes it"""

    def __init__(self, nvoices=NVOICES):
        self.v = [Voice() for _ in range(nvoices)]
        self.frame_no = 0
        self.pw = None
        self.lfo_phase = 0
        self.lfo_val = 1 << 23
        self.lfo_delay_state = 0
        self.lfo_delay = 0
        self.lfo_rand = 0
        self.pitch_base = 0

    def set_patch(self, patch):
        self.pw = patch_words(patch)

    # -- envelopes (msfa Env) --
    def _advance(self, v, k, newix):
        pw = self.pw
        v.ix[k] = newix
        if newix < 4:
            actual = pw['lvl'][k][newix] + v.outlevel[k]
            actual = max(16, actual)
            v.target[k] = actual << 16
            v.rising[k] = v.target[k] > v.level[k]
            if v.target[k] == v.level[k] or (newix == 0 and pw['l'][k][0] == 0):
                r = min(pw['r'][k][newix] + v.rs[k], 99)
                n = STATICS[r]
                if r < 77 and newix == 0 and pw['l'][k][0] == 0:
                    n //= 20
                v.static[k] = n
            else:
                v.static[k] = 0
            q = min(pw['qrate'][k][newix] + v.rs[k], 63)
            v.einc[k] = (4 + (q & 3)) << (2 + LG_N + (q >> 2))

    def _env_tick(self, v, k):
        if v.static[k]:
            v.static[k] -= CTRL_N
            if v.static[k] <= 0:
                v.static[k] = 0
                self._advance(v, k, v.ix[k] + 1)
        ix = v.ix[k]
        if ix < 3 or (ix < 4 and not v.down):
            if v.static[k]:
                pass
            elif v.rising[k]:
                jump = 1716 << 16
                if v.level[k] < jump:
                    v.level[k] = jump
                v.level[k] += (((17 << 24) - v.level[k]) >> 24) * v.einc[k]
                if v.level[k] >= v.target[k]:
                    v.level[k] = v.target[k]
                    self._advance(v, k, ix + 1)
            else:
                v.level[k] -= v.einc[k]
                if v.level[k] <= v.target[k]:
                    v.level[k] = v.target[k]
                    self._advance(v, k, ix + 1)
        return v.level[k]

    def _peg_advance(self, v, newix):
        v.peg_ix = newix
        if newix < 4:
            v.peg_target = self.pw['peg_level'][newix]
            v.peg_rising = v.peg_target > v.peg_level
            v.peg_inc = self.pw['peg_rate'][newix]

    def _peg_tick(self, v):
        ix = v.peg_ix
        if ix < 3 or (ix < 4 and not v.down):
            if v.peg_rising:
                v.peg_level += v.peg_inc
                if v.peg_level >= v.peg_target:
                    v.peg_level = v.peg_target
                    self._peg_advance(v, ix + 1)
            else:
                v.peg_level -= v.peg_inc
                if v.peg_level <= v.peg_target:
                    v.peg_level = v.peg_target
                    self._peg_advance(v, ix + 1)
        return v.peg_level

    # -- the LFO (msfa Lfo), one step every CTRL_N frames --
    def _lfo_tick(self):
        """msfa Lfo::getsample and getdelay with a 24-bit phase (msfa's 32-bit phase >> 8) and delay state"""
        delta, d1, d2, wave, sync = self.pw['lfo']
        self.lfo_phase = (self.lfo_phase + delta) & MASK24
        ph = self.lfo_phase
        top = ph & 0x800000
        if wave == 0:                                   # triangle
            val = ((ph << 1) ^ (MASK24 if top else 0)) & MASK24
        elif wave == 1:                                 # saw down
            val = (~ph ^ 0x800000) & MASK24
        elif wave == 2:                                 # saw up
            val = (ph ^ 0x800000) & MASK24
        elif wave == 3:                                 # square
            val = 0 if top else 1 << 24
        elif wave == 4:                                 # sine
            val = (1 << 23) + (SIN_TABLE[ph >> 16] >> 7)
        else:                                           # sample and hold
            if ph < delta:
                self.lfo_rand = (self.lfo_rand * 179 + 17) & 0xff
            val = ((self.lfo_rand ^ 0x80) + 1) << 16
        self.lfo_val = val
        dd = self.lfo_delay_state + (d1 if self.lfo_delay_state < 0x800000 else d2)
        if dd > MASK24:
            self.lfo_delay = 1 << 24
        else:
            self.lfo_delay_state = dd
            self.lfo_delay = 0 if dd < 0x800000 else (dd << 1) & MASK24

    def _voice_pitch(self, v):
        """msfa Dx7Note::compute's pitch: the pitch envelope plus the LFO's pitch modulation (no wheel, no bend); the
        LFO term in float32 (FLOAT, two multiplies, FIX), and the voice's end"""
        a = F32(float(self.lfo_delay))
        b = F32(float(self.lfo_val - (1 << 23)))
        pm = fix(F32(F32(a * b) * F32(self.pw['pm_k'])))
        v.pmod = self._peg_tick(v) + pm + self.pitch_base
        if not v.down:
            flags = ALGORITHMS[self.pw['alg']]
            if all(v.ix[k] >= 4 and self.pw['l4zero'][k] for k in range(6) if (flags[k] & 3) == 0):
                v.active = False

    def _amp_mod(self):
        """msfa: amod_1 = ((amd x lfo_delay) >> 8) x ((1 << 24) - lfo_val) >> 24, Q24 (amd <= 255: the first product
        fits 32 bits; the second is taken as (a >> 8) x (lv >> 8) >> 8, which also does)"""
        lv = (1 << 24) - self.lfo_val
        a = (self.pw['amd'] * self.lfo_delay) >> 8
        return ((a >> 8) * (lv >> 8)) >> 8

    def _op_control(self, v, k):
        pw = self.pw
        level = self._env_tick(v, k)
        if pw['ams'][k]:
            # msfa: level -= level x (exp(sensamp / 262144 x 0.07 + 12.2) << 4) >> 28 = level x 2 ** (AM_K0 + AM_K1
            # sensamp) / 2 ** 24 (AM_K*, Q24); here level x exp2_q24(x, 0) >> 24 with x = AM_K0 + (sensamp >> 4) AM_K1
            sensamp = ((self._amp_mod() >> 8) * (pw['ams'][k] >> 8)) >> 8
            x = AM_K0 + (sensamp >> 4) * AM_K1
            level -= fix(F32(F32(float(level)) * exp2_f32(x)))
        g = exp2_q24(level, GAIN_EXP + pw['gshift'][k])
        v.dg[k] = (g - v.g[k]) >> LG_N
        if pw['mode'][k]:
            v.inc[k] = exp2_q24(v.pitch[k] + self.pitch_base + FREQ_K, 0)
        else:
            v.inc[k] = exp2_q24(v.pitch[k] + v.pmod + FREQ_K, 0)

    # -- the host's commands --
    def key_on(self, n, patch, note, velocity):
        v = self.v[n]
        v.pitch, v.outlevel, v.rs = note_words(patch, note, velocity)
        if self.pw['oks']:
            v.ph = [0] * 6
        v.down = True
        v.active = True
        for k in range(6):
            v.level[k] = 0
            self._advance(v, k, 0)
        v.peg_level = self.pw['peg_level'][3]
        self._peg_advance(v, 0)
        if self.pw['lfo'][4]:
            self.lfo_phase = 0x7FFFFFFF
        self.lfo_delay_state = 0

    def key_off(self, n):
        v = self.v[n]
        v.down = False
        for k in range(6):
            self._advance(v, k, 3)
        self._peg_advance(v, 3)

    # -- one frame --
    def _control(self):
        slot = self.frame_no & (CTRL_N - 1)
        vn, k = slot >> 3, slot & 7
        if slot == CTRL_N - 1:
            self._lfo_tick()
        if vn < len(self.v):
            v = self.v[vn]
            if k == 0:
                self._voice_pitch(v)
            elif k <= 6:
                self._op_control(v, k - 1)

    def _sine(self, x):
        i = (x >> 16) & (SIN_N - 1)
        f = (x & 0xFFFF) << 15
        t0 = SIN_TABLE[i]
        return t0 + ssfr(SIN_TABLE[i + 1] - t0, f)

    def _voice_audio(self, v):
        flags = ALGORITHMS[self.pw['alg']]
        bus = [0, 0, 0]
        out = 0
        for k in range(6):
            fl = flags[k]
            inbus, outbus, add = (fl >> 4) & 3, fl & 3, fl & 4
            v.g[k] = i32(v.g[k] + v.dg[k])
            if (fl & 0xC0) == 0xC0:
                fbs = self.pw['fb_shift']
                mod = (v.fb[0] + v.fb[1]) >> (fbs + 1) if outbus else ((v.fb[0] + v.fb[1]) << 4) >> (fbs + 1)
                if fbs >= 16:
                    mod = 0
            else:
                mod = bus[inbus] if inbus else 0
            y = ssfr(self._sine((v.ph[k] + mod) & 0xFFFFFFFF), v.g[k])
            if (fl & 0xC0) == 0xC0:
                v.fb[0], v.fb[1] = v.fb[1], y
            if outbus == 0:
                out += y
            elif add:
                bus[outbus] += y
            else:
                bus[outbus] = y
            v.ph[k] = (v.ph[k] + v.inc[k]) & MASK24
        return out

    def frame(self):
        self._control()
        out = 0
        for v in self.v:
            if v.active:
                out += self._voice_audio(v)
        self.frame_no += 1
        return max(-OUT_MAX - 1, min(OUT_MAX, out))


def render(patch, events, seconds, nvoices=NVOICES):
    """events: [(time s, 'on'/'off', voice, note, velocity)] -> a list of 24-bit samples"""
    e = Engine(nvoices)
    e.set_patch(patch)
    ev = sorted(events)
    out, k = [], 0
    for n in range(int(seconds * FS)):
        t = n / FS
        while k < len(ev) and ev[k][0] <= t:
            _, what, vn, note, vel = ev[k]
            if what == 'on':
                e.key_on(vn, patch, note, vel)
            else:
                e.key_off(vn)
            k += 1
        out.append(e.frame())
    return out


def write_wav(path, samples, fs=FS):
    import wave
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(fs)
    w.writeframes(b''.join(struct.pack('<h', max(-32768, min(32767, s >> 8))) for s in samples))
    w.close()


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='render a DX7 patch with the KN7FM engine model')
    ap.add_argument('bank', help='a DX7 32-voice .syx (or "init")')
    ap.add_argument('index', type=int)
    ap.add_argument('out')
    ap.add_argument('--note', type=int, default=60)
    ap.add_argument('--vel', type=int, default=100)
    ap.add_argument('--hold', type=float, default=1.0)
    ap.add_argument('--len', type=float, default=2.0)
    a = ap.parse_args()
    p = Patch() if a.bank == 'init' else Bank.from_sysex(open(a.bank, 'rb').read()).patches[a.index]
    s = render(p, [(0.0, 'on', 0, a.note, a.vel), (a.hold, 'off', 0, a.note, 0)], a.len)
    write_wav(a.out, s)
    print('%s: %s, peak %d' % (a.out, p.name, max(abs(x) for x in s)))
