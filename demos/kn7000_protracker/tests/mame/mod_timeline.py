#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# mod_timeline.py MOD [SECONDS] -- the timeline pt_test.lua's logged positions (song pos, patt, row) are checked against: where a ProTracker module's song is (order, pattern, row) at each second, by PT 2.3D's
# sequencing rules (Fxx speed < 0x20 else BPM, Bxx, Dxx, E6x, EEx), the CIA timing (2.5/BPM s a tick).
import sys
d = open(sys.argv[1], 'rb').read()
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 60
L = d[950]; order = list(d[952:952 + 128])
def cell(p, r, c):
    o = 1084 + p * 1024 + r * 16 + 4 * c; b = d[o:o + 4]
    return b[2] & 15, b[3]
pos, row, speed, bpm, t = 0, 0, 6, 125, 0.0
loopRow = [0] * 4; loopCnt = [0] * 4
next_print = 0
while t < secs:
    p = order[pos]
    if t >= next_print:
        print(f"{t:6.2f} pos {pos} patt {p} row {row} speed {speed} bpm {bpm}"); next_print += 1
    brk = None; jump = None; delay = 0; newbpm = None; loopto = None
    for c in range(4):
        cmd, par = cell(p, row, c)
        if cmd == 0xF and par:
            if par < 32: speed = par
            else: newbpm = par
        elif cmd == 0xB: jump = par; brk = 0 if brk is None else brk
        elif cmd == 0xD: brk = (par >> 4) * 10 + (par & 15); brk = 0 if brk > 63 else brk
        elif cmd == 0xE and par >> 4 == 6:
            x = par & 15
            if x == 0: loopRow[c] = row
            else:
                if loopCnt[c] == 0: loopCnt[c] = x; loopto = loopRow[c]
                else:
                    loopCnt[c] -= 1
                    if loopCnt[c]: loopto = loopRow[c]
        elif cmd == 0xE and par >> 4 == 0xE: delay = (par & 15)
    t += speed * (1 + delay) * 2.5 / bpm
    if newbpm: bpm = newbpm   # (CIA: from the next tick; close enough)
    if loopto is not None: row = loopto; continue
    if jump is not None or brk is not None:
        pos = jump if jump is not None else pos + 1
        if pos >= L: pos = 0
        row = brk or 0; continue
    row += 1
    if row >= 64:
        row = 0; pos += 1
        if pos >= L: pos = 0
