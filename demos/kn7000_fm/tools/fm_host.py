#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# fm_host.py -- the main CPU's data for KN7FM (src/kn7fm.s): msfa's tables as fm_model.py holds them, the 32
# algorithms' diagrams for the screen, and the bank built into the program.
#
#   python3 -I tools/fm_host.py OUT_DIR [--wav DIR]
# writes OUT_DIR/fm_tables.inc. --wav renders each voice of the built-in bank (C4, velocity 100, 2 s held, 1 s of
# release) with the model, to listen to.
#
# The diagrams. Each algorithm's operators and connections come from msfa's flags (fm_model.ALGORITHMS): an
# operator reading a bus is modulated by the operators that wrote it since it was last replaced; the carriers are
# the operators writing the output; the feedback loop runs from the operator flagged 0x80 to the one flagged 0x40
# (algorithms 4 and 6 draw the DX7's loop over two and three operators, which msfa plays as OP6's own feedback).
# Layout: carriers on the bottom row, in operator order; each modulator a row above the highest operator it
# modulates, centred over them; operators that would sit in the same place are spread apart. Connections are drawn
# as horizontal and vertical strokes (rectangles), then the boxes over them.
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm_model as m  # noqa: E402

BOX_W, BOX_H = 22, 16       # an operator's box
COL, ROW = 30, 28           # the grid
DIAG_W, DIAG_H = 200, 132   # the diagram's area on the screen


def graph(alg):
    """-> carriers [op], modulators {op: [op modulating it]}, feedback (from op, to op)"""
    bus = {1: set(), 2: set()}
    mods, car = {}, []
    fbin = fbout = None
    for k, f in enumerate(m.ALGORITHMS[alg]):
        op = 6 - k
        inb, outb, add = (f >> 4) & 3, f & 3, f & 4
        if f & 0x40:
            fbin = op
        if f & 0x80:
            fbout = op
        mods[op] = sorted(bus[inb]) if inb else []
        if outb == 0:
            car.append(op)
        elif add:
            bus[outb] |= {op}
        else:
            bus[outb] = {op}
    return sorted(car), mods, (fbout, fbin)


def layout(alg):
    """-> boxes {op: (x, y)} (top left, in the diagram's area), strokes [(x, y, w, h)]"""
    car, mods, (fbout, fbin) = graph(alg)
    targets = {op: [t for t in range(1, 7) if op in mods[t]] for op in range(1, 7)}
    row = {}

    def r(op):
        if op not in row:
            row[op] = 0 if op in car else 1 + max(r(t) for t in targets[op])
        return row[op]

    for op in range(1, 7):
        r(op)
    x = {op: k * COL for k, op in enumerate(car)}
    for level in range(1, max(row.values()) + 1):
        ops = sorted(op for op in range(1, 7) if row[op] == level)
        want = {op: sum(x[t] for t in targets[op]) / len(targets[op]) for op in ops}
        groups = {}
        for op in ops:
            groups.setdefault(want[op], []).append(op)
        for w, g in groups.items():
            for k, op in enumerate(sorted(g)):
                x[op] = w + (k - (len(g) - 1) / 2) * COL
        order = sorted(ops, key=lambda op: (x[op], op))
        for a, b in zip(order, order[1:]):
            if x[b] < x[a] + COL:
                x[b] = x[a] + COL
    lo = min(x.values())
    hi = max(x.values())
    nrows = max(row.values()) + 1
    x0 = (DIAG_W - (hi - lo) - BOX_W) // 2 - lo
    top = (DIAG_H - 12 - (nrows - 1) * ROW - BOX_H) // 2
    # x even: the screen's text is drawn two pixels a 32-bit store
    boxes = {op: (int(round(x[op] + x0)) // 2 * 2, top + (nrows - 1 - row[op]) * ROW) for op in range(1, 7)}
    strokes = []

    def vline(xx, y0, y1):
        if y1 < y0:
            y0, y1 = y1, y0
        strokes.append((xx, y0, 1, y1 - y0 + 1))

    def hline(x0_, x1_, yy):
        if x1_ < x0_:
            x0_, x1_ = x1_, x0_
        strokes.append((x0_, yy, x1_ - x0_ + 1, 1))

    cx = {op: boxes[op][0] + BOX_W // 2 for op in boxes}
    for t in range(1, 7):
        for mo in mods[t]:
            y0 = boxes[mo][1] + BOX_H
            y1 = boxes[t][1] - 1
            if cx[mo] == cx[t]:
                vline(cx[t], y0, y1)
            else:
                ym = y1 - 5
                vline(cx[mo], y0, ym)
                hline(cx[mo], cx[t], ym)
                vline(cx[t], ym, y1)
    yb = boxes[car[0]][1] + BOX_H
    yout = yb + 6
    for c in car:
        vline(cx[c], yb, yout)
    hline(min(cx[c] for c in car), max(cx[c] for c in car), yout)
    xm = (min(cx[c] for c in car) + max(cx[c] for c in car)) // 2
    vline(xm, yout, yout + 5)
    # the feedback loop: out of the bottom of fbout, up its right side, into the top of fbin
    xo, yo = boxes[fbout]
    xi, yi = boxes[fbin]
    xr = max(xo, xi) + BOX_W + 4
    ylow = yo + BOX_H + 3
    yhigh = yi - 4
    vline(cx[fbout], yo + BOX_H, ylow)
    hline(cx[fbout], xr, ylow)
    vline(xr, yhigh, ylow)
    hline(cx[fbin], xr, yhigh)
    vline(cx[fbin], yhigh, yi - 1)
    for (sx, sy, w, h) in strokes + [(bx, by, BOX_W, BOX_H) for bx, by in boxes.values()]:
        assert 0 <= sx and sx + w <= DIAG_W and 0 <= sy and sy + h <= DIAG_H, (alg + 1, sx, sy, w, h)
    return boxes, strokes, car


# ---- the built-in bank: voices of our own (not Yamaha's), made with the model ------------------------------
def op(r, l, ol, fc=1, ff=0, det=7, kvs=0, rs=0, ams=0, mode=0, bp=39, ld=0, rd=0, lc=0, rc=0):
    """one operator, VCED order: R1-4, L1-4, BP, LD, RD, LC, RC, RS, AMS, KVS, OL, MODE, FC, FF, DET"""
    return list(r) + list(l) + [bp, ld, rd, lc, rc, rs, ams, kvs, ol, mode, fc, ff, det]


OFF = op((99, 99, 99, 99), (99, 99, 99, 0), 0)


def voice(name, alg, fb, ops, lfo=(35, 0, 0, 0, 1, 0, 3), peg=((99, 99, 99, 99), (50, 50, 50, 50)), oks=1,
          trnsp=24):
    """ops: OP1..OP6 (as the DX7 numbers them); lfo: speed, delay, PMD, AMD, sync, wave, PMS"""
    d = []
    for o in reversed(ops):                               # VCED: OP6 first
        d += o
    lfs, lfd, pmd, amd, sync, wave, pms = lfo
    d += list(peg[0]) + list(peg[1]) + [alg - 1, fb, oks, lfs, lfd, pmd, amd, sync, wave, pms, trnsp]
    d += name.ljust(10)[:10].encode('ascii')
    return m.Patch(m.clamp_voice(bytes(d)))


def bank():
    v = []
    v.append(voice("E.PIANO   ", 5, 6, [
        op((96, 29, 22, 64), (99, 80, 0, 0), 99, kvs=2, rs=3, det=7),
        op((95, 48, 30, 72), (99, 72, 0, 0), 60, kvs=6, rs=3),
        op((94, 22, 22, 52), (99, 92, 0, 0), 92, det=9, kvs=2, rs=2),
        op((95, 30, 22, 52), (99, 90, 0, 0), 76, fc=14, kvs=7, rs=3, rd=20, rc=0),
        op((94, 22, 22, 52), (99, 92, 0, 0), 84, det=5, kvs=2, rs=2),
        op((95, 30, 22, 52), (99, 90, 0, 0), 70, kvs=6)]))
    v.append(voice("FM BASS   ", 1, 5, [
        op((99, 35, 20, 70), (99, 86, 0, 0), 99, fc=0, kvs=2, rs=2),
        op((99, 55, 30, 70), (99, 60, 0, 0), 78, fc=0, kvs=4),
        op((99, 40, 25, 70), (99, 80, 0, 0), 82, fc=1, kvs=2),
        op((99, 60, 35, 70), (99, 50, 0, 0), 70, fc=1, kvs=5),
        op((99, 70, 40, 70), (99, 40, 0, 0), 62, fc=3),
        op((99, 80, 40, 70), (99, 30, 0, 0), 55, fc=1)]))
    v.append(voice("BRASS     ", 22, 7, [
        op((60, 40, 30, 55), (99, 94, 90, 0), 99, kvs=1),
        op((55, 35, 25, 55), (99, 88, 80, 0), 82, kvs=3),
        op((62, 40, 30, 55), (99, 94, 90, 0), 92, det=9),
        op((62, 40, 30, 55), (99, 94, 90, 0), 92, det=5),
        op((62, 40, 30, 55), (99, 94, 90, 0), 88, fc=2),
        op((50, 35, 25, 55), (99, 85, 78, 0), 74, kvs=3)],
        peg=((84, 95, 95, 60), (46, 50, 50, 50)), lfo=(32, 30, 4, 0, 0, 4, 3)))
    v.append(voice("STRINGS   ", 2, 3, [
        op((40, 30, 30, 45), (99, 95, 92, 0), 99, det=8),
        op((42, 30, 30, 45), (99, 90, 86, 0), 74, fc=1),
        op((40, 30, 30, 45), (99, 95, 92, 0), 96, det=6),
        op((45, 30, 30, 45), (99, 90, 86, 0), 70, fc=1),
        op((50, 30, 30, 45), (99, 85, 80, 0), 66, fc=3),
        op((50, 30, 30, 45), (99, 85, 80, 0), 60, fc=1)],
        lfo=(30, 40, 6, 0, 0, 4, 3)))
    v.append(voice("BELLS     ", 5, 0, [
        op((99, 30, 18, 30), (99, 70, 0, 0), 99, kvs=2),
        op((99, 32, 20, 30), (99, 70, 0, 0), 72, fc=3, ff=50),
        op((99, 25, 15, 30), (99, 70, 0, 0), 88, fc=2, det=9),
        op((99, 28, 16, 30), (99, 60, 0, 0), 68, fc=7),
        op((99, 25, 15, 30), (99, 70, 0, 0), 80, fc=4, det=5),
        op((99, 28, 16, 30), (99, 60, 0, 0), 62, fc=11)]))
    v.append(voice("ORGAN     ", 32, 0, [
        op((99, 99, 99, 80), (99, 99, 99, 0), 94, fc=1),
        op((99, 99, 99, 80), (99, 99, 99, 0), 90, fc=0),
        op((99, 99, 99, 80), (99, 99, 99, 0), 86, fc=2),
        op((99, 99, 99, 80), (99, 99, 99, 0), 80, fc=3),
        op((99, 99, 99, 80), (99, 99, 99, 0), 78, fc=4),
        op((99, 99, 99, 80), (99, 99, 99, 0), 72, fc=6)],
        lfo=(60, 0, 4, 0, 0, 4, 2), oks=0))
    v.append(voice("MARIMBA   ", 7, 0, [
        op((99, 45, 30, 50), (99, 0, 0, 0), 99, kvs=3, rs=3),
        op((99, 70, 40, 60), (99, 0, 0, 0), 66, fc=4, kvs=5),
        op((99, 50, 30, 50), (99, 0, 0, 0), 84, fc=4, kvs=3, rs=3),
        op((99, 75, 40, 60), (99, 0, 0, 0), 58, fc=10),
        op((99, 75, 40, 60), (99, 0, 0, 0), 50, fc=1),
        op((99, 75, 40, 60), (99, 0, 0, 0), 0, fc=1)]))
    v.append(voice("HARPSICH  ", 5, 4, [
        op((99, 40, 25, 60), (99, 60, 0, 0), 99, kvs=1, rs=4),
        op((99, 45, 30, 60), (99, 70, 0, 0), 80, fc=3, kvs=2),
        op((99, 40, 25, 60), (99, 60, 0, 0), 90, fc=2, det=9, rs=4),
        op((99, 45, 30, 60), (99, 70, 0, 0), 76, fc=5),
        op((99, 40, 25, 60), (99, 60, 0, 0), 84, fc=4, det=5, rs=4),
        op((99, 45, 30, 60), (99, 70, 0, 0), 72, fc=1)]))
    v.append(voice("FLUTE     ", 16, 0, [
        op((55, 40, 30, 60), (99, 96, 94, 0), 99),
        op((50, 40, 30, 60), (99, 90, 85, 0), 58, fc=1),
        op((60, 40, 30, 60), (99, 90, 85, 0), 52, fc=2),
        op((99, 99, 99, 60), (99, 99, 99, 0), 40, fc=5),
        op((60, 40, 30, 60), (99, 90, 85, 0), 46, fc=3),
        op((99, 99, 99, 60), (99, 99, 99, 0), 0, fc=1)],
        lfo=(34, 50, 5, 3, 0, 4, 3)))
    v.append(voice("CLAV      ", 3, 6, [
        op((99, 55, 40, 70), (99, 60, 0, 0), 99, kvs=4),
        op((99, 60, 45, 70), (99, 50, 0, 0), 78, fc=3, kvs=6),
        op((99, 65, 50, 70), (99, 40, 0, 0), 70, fc=5),
        op((99, 55, 40, 70), (99, 60, 0, 0), 90, fc=1, det=9, kvs=4),
        op((99, 60, 45, 70), (99, 50, 0, 0), 74, fc=2, kvs=6),
        op((99, 65, 50, 70), (99, 40, 0, 0), 66, fc=1)]))
    v.append(voice("TUB BELL  ", 5, 0, [
        op((99, 25, 12, 25), (99, 60, 0, 0), 99),
        op((99, 25, 12, 25), (99, 60, 0, 0), 76, fc=3, ff=50),
        op((99, 25, 12, 25), (99, 60, 0, 0), 90, det=10),
        op((99, 25, 12, 25), (99, 60, 0, 0), 72, fc=3, ff=50, det=4),
        op((99, 30, 15, 25), (99, 55, 0, 0), 80, fc=2),
        op((99, 30, 15, 25), (99, 55, 0, 0), 64, fc=7)]))
    v.append(voice("SYN LEAD  ", 18, 7, [
        op((90, 50, 40, 60), (99, 96, 94, 0), 99),
        op((85, 50, 40, 60), (99, 92, 90, 0), 76, fc=1),
        op((85, 50, 40, 60), (99, 92, 90, 0), 72, fc=2, det=9),
        op((85, 50, 40, 60), (99, 92, 90, 0), 70, fc=3),
        op((85, 50, 40, 60), (99, 92, 90, 0), 68, fc=1),
        op((85, 50, 40, 60), (99, 92, 90, 0), 64, fc=1)],
        lfo=(40, 45, 8, 0, 0, 4, 4)))
    v.append(voice("WARM PAD  ", 31, 4, [
        op((30, 25, 30, 35), (99, 94, 92, 0), 94, det=8),
        op((28, 25, 30, 35), (99, 94, 92, 0), 90, det=6),
        op((32, 25, 30, 35), (99, 94, 92, 0), 84, fc=2, det=9),
        op((30, 25, 30, 35), (99, 94, 92, 0), 82, fc=0),
        op((30, 25, 30, 35), (99, 94, 92, 0), 86),
        op((35, 25, 30, 35), (99, 90, 88, 0), 66, fc=1)],
        lfo=(25, 30, 5, 6, 0, 0, 3)))
    v.append(voice("KALIMBA   ", 5, 0, [
        op((99, 50, 30, 50), (99, 0, 0, 0), 99, kvs=3, rs=3),
        op((99, 75, 40, 60), (99, 0, 0, 0), 64, fc=6, kvs=4),
        op((99, 45, 28, 50), (99, 0, 0, 0), 80, fc=3, kvs=3, rs=3),
        op((99, 75, 40, 60), (99, 0, 0, 0), 56, fc=8),
        op((99, 50, 30, 50), (99, 0, 0, 0), 0),
        op((99, 75, 40, 60), (99, 0, 0, 0), 0)]))
    v.append(voice("STEEL DRM ", 5, 0, [
        op((99, 40, 25, 45), (99, 30, 0, 0), 99, kvs=2),
        op((99, 45, 30, 50), (99, 40, 0, 0), 70, fc=2, ff=50),
        op((99, 40, 25, 45), (99, 30, 0, 0), 86, fc=2, det=9),
        op((99, 45, 30, 50), (99, 40, 0, 0), 60, fc=5, ff=40),
        op((99, 40, 25, 45), (99, 30, 0, 0), 74, fc=4),
        op((99, 45, 30, 50), (99, 40, 0, 0), 50, fc=9)]))
    v.append(voice("VIBES     ", 5, 0, [
        op((99, 30, 20, 40), (99, 50, 0, 0), 99, kvs=2, ams=1),
        op((99, 40, 25, 45), (99, 40, 0, 0), 66, fc=4, kvs=4),
        op((99, 30, 20, 40), (99, 50, 0, 0), 86, fc=1, det=9, ams=1),
        op((99, 40, 25, 45), (99, 40, 0, 0), 58, fc=10),
        op((99, 30, 20, 40), (99, 50, 0, 0), 0),
        op((99, 40, 25, 45), (99, 40, 0, 0), 0)],
        lfo=(55, 0, 0, 40, 0, 4, 0)))
    while len(v) < 32:
        p = m.Patch()
        p.name = "INIT %02d   " % (len(v) + 1)
        v.append(p)
    return m.Bank(v)


def write_inc(out):
    with open(os.path.join(out, "fm_equ.inc"), "w") as f:
        f.write("# GENERATED by tools/fm_host.py -- KN7FM's constants for the main CPU (do not edit)\n")
        for name, val in (("LFO_UNIT", m.LFO_UNIT), ("GAIN_EXP", m.GAIN_EXP), ("GSHIFT_MOD", m.GSHIFT_MOD),
                          ("GSHIFT_CAR", m.GSHIFT_CAR), ("PITCH_BASE", m.PITCH_BASE), ("PITCH_STEP", m.PITCH_STEP),
                          ("DIAG_W", DIAG_W), ("DIAG_H", DIAG_H), ("BOX_W", BOX_W), ("BOX_H", BOX_H),
                          ("VOL_STEPS", m.VOL_STEPS), ("VOL_DEFAULT", m.VOL_DEFAULT)):
            f.write(f".equ {name + ',':<12} {val}\n")
    with open(os.path.join(out, "fm_tables.inc"), "w") as f:
        w = f.write
        w("# GENERATED by tools/fm_host.py -- KN7FM's tables for the main CPU (do not edit)\n")

        def tab(label, kind, vals, per=12):
            w(f"\n    .align  4\n{label}:\n")
            for k in range(0, len(vals), per):
                w(f"    {kind}   " + ", ".join(str(x) for x in vals[k:k + per]) + "\n")

        # msfa's tables (fm_model.py) for note_words and patch_words
        tab("T_LEVELLUT", ".byte", m.LEVELLUT)
        tab("T_VELOCITY", ".byte", m.VELOCITY_DATA)
        tab("T_EXPSCALE", ".byte", m.EXP_SCALE_DATA)
        tab("T_COARSE", ".long", m.COARSEMUL, 8)
        tab("T_FINE", ".long", m.FINE_TAB, 8)
        tab("T_DETUNE", ".long", m.DETUNE_Q, 8)
        tab("T_STATICS", ".long", m.STATICS, 8)
        unit = m.peg_unit()
        tab("T_PEGRATE", ".long", [r * unit for r in m.PITCHENV_RATE], 8)
        tab("T_PEGLEVEL", ".long", [lv << 19 for lv in m.PITCHENV_TAB], 8)
        tab("T_PMSENS", ".long", m.PITCHMODSENSTAB, 8)
        tab("T_AMSENS", ".long", m.AMPMODSENSTAB, 8)
        tab("T_LFODELTA", ".long", m.LFO_DELTA, 8)
        tab("T_ALGFLAGS", ".byte", [x for fl in m.ALGORITHMS for x in fl], 6)
        tab("T_VOLUME", ".long", [m.volume_word(v) for v in range(m.VOL_STEPS)], 8)

        # the diagrams: per algorithm a record: strokes, carriers mask, 6 boxes (x, y), OP1 first; then the strokes

        w("\n    .align  4\nT_DIAG:                 # per algorithm: the address of its record\n")
        for a in range(32):
            w(f"    .long   diag_{a + 1}\n")
        for a in range(32):
            boxes, strokes, car = layout(a)
            mask = sum(1 << (o - 1) for o in car)
            w(f"diag_{a + 1}:\n    .word   {len(strokes)}, {mask}\n")
            w("    .word   " + ", ".join(f"{boxes[o][0]}, {boxes[o][1]}" for o in range(1, 7)) + "\n")
            for s in strokes:
                w(f"    .word   {s[0]}, {s[1]}, {s[2]}, {s[3]}\n")
        # the built-in bank, as a 32-voice dump's 4,096 bytes (VMEM), unpacked as a file's
        body = b"".join(p.packed() for p in bank().patches)
        w("\n    .align  4\nT_BANK:                 # the built-in bank: 32 packed voices (VMEM)\n")
        for k in range(0, len(body), 16):
            w("    .byte   " + ", ".join(f"0x{x:02X}" for x in body[k:k + 16]) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--wav")
    a = ap.parse_args()
    for alg in range(32):
        layout(alg)
    write_inc(a.out)
    print(f"fm_host: {a.out}/fm_equ.inc, fm_tables.inc")
    if a.wav:
        os.makedirs(a.wav, exist_ok=True)
        for k, p in enumerate(bank().patches[:16]):
            s = m.render(p, [(0.0, 'on', 0, 60, 100), (2.0, 'off', 0, 60, 0)], 3.0, nvoices=1)
            path = os.path.join(a.wav, "%02d_%s.wav" % (k + 1, p.name.strip().replace(" ", "_").replace(".", "")))
            m.write_wav(path, s)
            peak = max(abs(x) for x in s)
            print(f"{path}: peak {20 * __import__('math').log10(peak / 8388607 + 1e-12):.1f} dBFS")


if __name__ == "__main__":
    main()
