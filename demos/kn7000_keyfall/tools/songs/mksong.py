#!/usr/bin/env python3
# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# mksong.py IN.txt OUT.MID -- a song written as text (tools/songs/README.md) made into a Standard MIDI File, format 1,
# 480 ticks a quarter note: a first track with the title, the tempo and the time signature, then one track per `track:`.
# Each bar (between two `|`) is checked against the time signature; a bar of another length is an error, but the
# first (a pick-up) and the last.
import re
import struct
import sys

PPQ = 480
NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def die(where, msg):
    sys.exit(f'{where}: {msg}')


def pitch(tok, where):
    if re.fullmatch(r'\d+', tok):
        return int(tok)
    m = re.fullmatch(r'([A-Ga-g])([#b]?)(-?\d)', tok)
    if not m:
        die(where, f'not a note: {tok!r}')
    n = NAMES[m.group(1).upper()] + {'#': 1, 'b': -1, '': 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + n  # C4 = 60


def length(spec, default, where):
    """'/4' a quarter, '/8.' a dotted eighth, '/2' a half, '/1' a whole; '' the default"""
    if not spec:
        return default
    m = re.fullmatch(r'/(\d+)(\.?)', spec)
    if not m or int(m.group(1)) not in (1, 2, 4, 8, 16, 32):
        die(where, f'not a length: {spec!r}')
    t = PPQ * 4 // int(m.group(1))
    return t * 3 // 2 if m.group(2) else t


def tokens(text):
    return re.findall(r'\(|\)\*\d+|\||\[[^\]]*\][^\s|()]*|[^\s|()]+', text)


def notes_of(toks, default, where, vel):
    """-> [(start, dur, [pitches], vel)], length; bars checked by the caller from the bar marks"""
    out, t, bars = [], 0, []
    i = 0

    def run(seq):
        nonlocal t
        for tok in seq:
            if tok == '|':
                bars.append(t)
                continue
            m = re.fullmatch(r'\[([^\]]*)\](.*)', tok)
            if m:
                ps, spec = m.group(1).split(), m.group(2)
            else:
                m = re.fullmatch(r'([^/]+)(/.*)?', tok)
                ps, spec = [m.group(1)], m.group(2) or ''
            v = vel
            vm = re.fullmatch(r'(.*)!(\d+)', spec)
            if vm:
                spec, v = vm.group(1), int(vm.group(2))
            d = length(spec, default, where)
            if ps != ['R'] and ps != ['r']:
                out.append((t, d, [pitch(p, where) for p in ps], v))
            t += d

    # groups: ( ... )*N repeats
    stack = [[]]
    for tok in toks:
        if tok == '(':
            stack.append([])
        elif tok.startswith(')*'):
            body = stack.pop()
            stack[-1].extend(body * int(tok[2:]))
        else:
            stack[-1].append(tok)
    if len(stack) != 1:
        die(where, 'a ( without its )*N')
    run(stack[0])
    return out, t, bars


def vlq(v):
    b = [v & 0x7F]
    v >>= 7
    while v:
        b.append(0x80 | (v & 0x7F))
        v >>= 7
    return bytes(reversed(b))


def track(events):
    """events: [(tick, order, bytes)] -> an MTrk chunk"""
    events.sort(key=lambda e: (e[0], e[1]))
    data, now = bytearray(), 0
    for tick, _, b in events:
        data += vlq(tick - now) + b
        now = tick
    data += vlq(0) + b'\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(data)) + data


def main():
    src, dst = sys.argv[1], sys.argv[2]
    head = {'title': 'Untitled', 'tempo': '120', 'time': '4/4'}
    tracks, cur = [], None
    for ln, line in enumerate(open(src, encoding='utf-8'), 1):
        line = line.rstrip()
        if not line.strip() or line.lstrip().startswith('#'):  # (a comment: a line of its own; '#' is a sharp)
            continue
        m = re.match(r'\s*(\w+):\s*(.*)$', line)
        if m and m.group(1) in ('title', 'composer', 'tempo', 'time', 'source') and cur is None:
            head[m.group(1)] = m.group(2)
            continue
        if m and m.group(1) == 'track':
            cur = {'name': m.group(2), 'channel': 1, 'program': 0, 'len': '/4', 'vel': 90, 'notes': [], 'line': ln}
            tracks.append(cur)
            continue
        if m and cur is not None and m.group(1) in ('channel', 'program', 'len', 'vel'):
            cur[m.group(1)] = m.group(2).strip()
            continue
        if cur is None:
            die(f'{src}:{ln}', 'notes before a track:')
        cur['notes'].append((ln, line))
    num, den = (int(x) for x in head['time'].split('/'))
    bar = PPQ * 4 * num // den
    bpm = float(head['tempo'])
    conductor = [(0, 0, b'\xff\x03' + vlq(len(head['title'].encode())) + head['title'].encode()),
                 (0, 1, b'\xff\x51\x03' + struct.pack('>I', round(60e6 / bpm))[1:]),
                 (0, 2, b'\xff\x58\x04' + bytes([num, {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[den], 24, 8]))]
    if head.get('composer'):
        c = head['composer'].encode()
        conductor.append((0, 3, b'\xff\x02' + vlq(len(c)) + c))
    chunks = [track(conductor)]
    for tr in tracks:
        where = f'{src}:{tr["line"]}'
        ch = int(tr['channel']) - 1
        default = length(tr['len'], PPQ, where)
        toks = [t for _, line in tr['notes'] for t in tokens(line)]
        notes, total, bars = notes_of(toks, default, where, int(tr['vel']))
        marks = [0] + bars + ([total] if not bars or bars[-1] != total else [])
        for k in range(1, len(marks)):
            got = marks[k] - marks[k - 1]
            if got != bar and k != 1 and k != len(marks) - 1:
                die(where, f'track {tr["name"]!r}: bar {k} is {got / PPQ} quarters, not {bar / PPQ}')
        ev = [(0, 0, b'\xff\x03' + vlq(len(tr['name'].encode())) + tr['name'].encode()),
              (0, 1, bytes([0xC0 | ch, int(tr['program'])]))]
        for start, d, ps, v in notes:
            for p in ps:
                off = start + d - (10 if d > 20 else 0)  # (a little gap: the same key struck again is two notes)
                ev.append((off, 2, bytes([0x80 | ch, p, 0])))   # note-offs first at a tick
                ev.append((start, 3, bytes([0x90 | ch, p, v])))
        chunks.append(track(ev))
        print(f'{dst}: track {tr["name"]!r} ch {ch + 1}: {len(notes)} notes/chords, {total / bar:.2f} bars')
    with open(dst, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 1, len(chunks), PPQ))
        for c in chunks:
            f.write(c)


main()
