/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * smf.c -- a Standard MIDI File (format 0 or 1, ticks per quarter note) read into a Song (kf.h):
 *   * every track's channel events that the accompaniment sends (note on and off, control change, program change,
 *     pitch bend) merged in time order, each with its time in ms by the file's tempo map; system exclusive messages are
 *     left out (a GM or GS reset would change the instrument's own settings), as are aftertouch and meta events;
 *   * the notes, each from its note-on to its note-off (a note struck again before its note-off ends there; one never
 *     let go ends with its track), and the parts they belong to: a part is a track's notes on one channel, named by
 *     the track's name (meta event 3);
 *   * the beats and bars, by the time signatures (4/4 until one says otherwise);
 *   * the title: the first track's name in format 1 (the tempo track's), or the file's first track name.
 * Two passes over each track: one counts, one fills. Everything comes from kf_alloc; what is only needed while loading
 * is given back at the end. */
#include "kf.h"

typedef struct Tempo {
  u32 tick, us;  /* from this tick, microseconds per quarter note */
  u64 usAt;      /* the time at that tick */
} Tempo;

typedef struct TimeSig {
  u32 tick;
  u8 num, den;  /* den: a power of two (2 = a quarter) */
} TimeSig;

typedef struct Track {
  const u8 *p, *end;
  char name[32];
  u32 events, notes, endTick;
  u16 chNotes[16];  /* notes on each channel */
} Track;

static struct {
  Track trk[KF_MAX_TRACKS];
  u32 nTrk;
  Tempo *tempo;
  u32 nTempo, maxTempo;
  TimeSig *sig;
  u32 nSig, maxSig;
  u16 ppq;
} L;

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }

/* a variable-length quantity; 0 past the end (*p then at end) */
static u32 vlq(const u8 **p, const u8 *end) {
  u32 v = 0;
  for (int i = 0; i < 4 && *p < end; ++i) {
    const u8 b = *(*p)++;
    v = v << 7 | (b & 0x7F);
    if (!(b & 0x80)) return v;
  }
  return v;
}

/* What one pass over a track does with what it finds. */
typedef struct Sink {
  int fill;          /* 0: count only */
  MEv *ev;           /* fill: the track's events go here, from 0 */
  Note *notes;       /* fill: the track's notes */
  s16 *open;         /* fill: [16][128] the open note's index, or -1 */
  const u8 *partOf;  /* fill: [16] the part of each channel's notes */
  Part *parts;
  u32 nEv, nNotes;
} Sink;

static void close_note(Sink *k, int ch, int note, u32 tick) {
  s16 *o = &k->open[ch * 128 + note];
  if (*o < 0) return;
  Note *n = &k->notes[*o];
  n->dur = tick - n->ms;  /* (ticks until the times are made) */
  *o = -1;
}

/* One pass over track t: -> 0, or < 0 (it is damaged). */
static int pass(u32 t, Sink *k) {
  Track *tr = &L.trk[t];
  const u8 *p = tr->p, *end = tr->end;
  u32 tick = 0;
  u8 status = 0;
  k->nEv = k->nNotes = 0;
  while (p < end) {
    tick += vlq(&p, end);
    if (p >= end) break;
    u8 b = *p;
    if (b >= 0x80) ++p, status = b < 0xF0 ? b : 0;
    else if (!status) return -1;  /* data with no running status */
    else b = status;
    if (b == 0xFF) {  /* meta */
      if (p >= end) return -1;
      const u8 type = *p++;
      const u32 len = vlq(&p, end);
      if (len > (u32)(end - p)) return -1;
      if (type == 0x2F) break;
      if (type == 0x03 && !k->fill && !tr->name[0]) {
        u32 n = len < sizeof tr->name - 1 ? len : sizeof tr->name - 1;
        for (u32 i = 0; i < n; ++i) tr->name[i] = (char)(p[i] >= 0x20 && p[i] < 0x7F ? p[i] : ' ');
        tr->name[n] = 0;
      }
      if (type == 0x51 && len == 3 && !k->fill) {
        if (L.tempo && L.nTempo < L.maxTempo)
          L.tempo[L.nTempo] = (Tempo){tick, (u32)p[0] << 16 | (u32)p[1] << 8 | p[2], 0};
        ++L.nTempo;
      }
      if (type == 0x58 && len >= 2 && !k->fill) {
        if (L.sig && L.nSig < L.maxSig) L.sig[L.nSig] = (TimeSig){tick, p[0], p[1]};
        ++L.nSig;
      }
      p += len;
      continue;
    }
    if (b == 0xF0 || b == 0xF7) {  /* system exclusive: left out */
      const u32 len = vlq(&p, end);
      if (len > (u32)(end - p)) return -1;
      p += len;
      continue;
    }
    if (b >= 0xF0) return -1;  /* (no other system message belongs in a file) */
    const u32 dl = (b & 0xE0) == 0xC0 ? 1 : 2;
    if ((u32)(end - p) < dl) return -1;
    const u8 d1 = p[0] & 0x7F, d2 = dl > 1 ? p[1] & 0x7F : 0;
    p += dl;
    const u8 kind = b & 0xF0, ch = b & 0x0F;
    if (kind == 0xA0 || kind == 0xD0) continue;  /* aftertouch: left out */
    const int off = kind == 0x80 || (kind == 0x90 && !d2);
    const int on = kind == 0x90 && d2 && (!k->fill || k->partOf[ch] != 0xFF);  /* (past KF_MAX_PARTS: no note) */
    if (k->fill) {
      k->ev[k->nEv] = (MEv){tick, tick, (u8)(off ? 0x80 | ch : b), d1, off ? 0 : d2, (u8)t};
      if (on) {
        close_note(k, ch, d1, tick);
        Note *n = &k->notes[k->nNotes];
        *n = (Note){tick, 0, d1, d2, k->partOf[ch], 0};
        k->open[ch * 128 + d1] = (s16)k->nNotes;
        Part *pt = &k->parts[k->partOf[ch]];
        if (d1 < pt->lo) pt->lo = d1;
        if (d1 > pt->hi) pt->hi = d1;
      } else if (off) {
        close_note(k, ch, d1, tick);
      }
    } else if (on) {
      ++tr->chNotes[ch];
    }
    ++k->nEv;
    if (on) ++k->nNotes;
  }
  if (k->fill)
    for (int ch = 0; ch < 16; ++ch)
      for (int n = 0; n < 128; ++n) close_note(k, ch, n, tick);
  tr->endTick = tick;
  return 0;
}

static void tempo_prepare(void) {
  /* sorted by tick (insertion: few), a 120 BPM start when none is at 0, the time at each */
  for (u32 i = 1; i < L.nTempo; ++i)
    for (u32 j = i; j > 0 && L.tempo[j - 1].tick > L.tempo[j].tick; --j) {
      const Tempo x = L.tempo[j];
      L.tempo[j] = L.tempo[j - 1];
      L.tempo[j - 1] = x;
    }
  u64 us = 0;
  u32 tick = 0, cur = 500000;
  for (u32 i = 0; i < L.nTempo; ++i) {
    us += (u64)(L.tempo[i].tick - tick) * cur / L.ppq;
    tick = L.tempo[i].tick;
    cur = L.tempo[i].us ? L.tempo[i].us : 500000;
    L.tempo[i].usAt = us;
  }
}

static u32 ms_of(u32 tick) {
  u64 us = 0;
  u32 from = 0, cur = 500000;
  for (u32 i = 0; i < L.nTempo && L.tempo[i].tick <= tick; ++i) {
    us = L.tempo[i].usAt;
    from = L.tempo[i].tick;
    cur = L.tempo[i].us ? L.tempo[i].us : 500000;
  }
  us += (u64)(tick - from) * cur / L.ppq;
  return (u32)(us / 1000);
}

u32 smf_ms_of(const Song *s, u32 tick) {
  (void)s;
  return ms_of(tick);
}

int smf_load(Song *s, const u8 *d, u32 bytes, const char **why) {
  kf_memset(s, 0, sizeof *s);
  kf_memset(&L, 0, sizeof L);
  *why = "";
  if (bytes < 14 || be32(d) != 0x4D546864 || be32(d + 4) < 6) return *why = "not a MIDI file", -1;
  s->format = (u16)be16(d + 8);
  s->tracks = (u16)be16(d + 10);
  const u32 div = be16(d + 12);
  if (div & 0x8000) return *why = "SMPTE time is not read", -1;
  if (!div) return *why = "no ticks per quarter note", -1;
  if (s->format > 1) return *why = "format 2 is not read", -1;
  s->ppq = L.ppq = (u16)div;
  /* the tracks */
  const u8 *p = d + 8 + be32(d + 4), *end = d + bytes;
  while (p + 8 <= end && L.nTrk < KF_MAX_TRACKS && L.nTrk < s->tracks) {
    const u32 len = be32(p + 4);
    const u8 *body = p + 8;
    if (len > (u32)(end - body)) return *why = "a track is cut short", -1;
    if (be32(p) == 0x4D54726B) L.trk[L.nTrk++] = (Track){body, body + len, "", 0, 0, 0, {0}};
    p = body + len;
  }
  if (!L.nTrk) return *why = "no tracks", -1;
  /* pass 1: counts, names, the tempo map and time signatures (counted, then read in a second count pass) */
  u32 nEv = 0, nNotes = 0;
  Sink k = {0};
  for (u32 t = 0; t < L.nTrk; ++t) {
    if (pass(t, &k) < 0) return *why = "a track is damaged", -1;
    L.trk[t].events = k.nEv, L.trk[t].notes = k.nNotes;
    nEv += k.nEv, nNotes += k.nNotes;
  }
  if (!nNotes) return *why = "no notes", -1;
  L.maxTempo = L.nTempo, L.maxSig = L.nSig;
  L.tempo = (Tempo *)kf_alloc((L.maxTempo + 1) * sizeof(Tempo));
  L.sig = (TimeSig *)kf_alloc((L.maxSig + 1) * sizeof(TimeSig));
  if (!L.tempo || !L.sig) return *why = "out of memory", -2;
  L.nTempo = L.nSig = 0;
  for (u32 t = 0; t < L.nTrk; ++t) {
    for (int ch = 0; ch < 16; ++ch) L.trk[t].chNotes[ch] = 0;
    L.trk[t].name[0] = 0;
    pass(t, &k);
  }
  tempo_prepare();
  /* the parts: a track's notes on one channel */
  u8 partOf[KF_MAX_TRACKS][16];
  for (u32 t = 0; t < L.nTrk; ++t) {
    int chans = 0;
    for (int ch = 0; ch < 16; ++ch) chans += L.trk[t].chNotes[ch] != 0;
    for (int ch = 0; ch < 16; ++ch) {
      partOf[t][ch] = 0xFF;
      if (!L.trk[t].chNotes[ch] || s->nParts >= KF_MAX_PARTS) continue;
      Part *pt = &s->part[s->nParts];
      partOf[t][ch] = (u8)s->nParts++;
      pt->trk = (u8)t, pt->ch = (u8)ch, pt->lo = 127, pt->hi = 0, pt->notes = L.trk[t].chNotes[ch];
      pt->drums = ch == 9;
      if (L.trk[t].name[0] && chans == 1) kf_strcpy(pt->name, L.trk[t].name, sizeof pt->name);
      else if (L.trk[t].name[0]) kf_fmt(pt->name, sizeof pt->name, "%s ch %d", L.trk[t].name, ch + 1);
      else kf_fmt(pt->name, sizeof pt->name, "Track %d ch %d", (s32)t + 1, ch + 1);
    }
  }
  /* the title */
  if (s->format == 1 && L.trk[0].name[0]) kf_strcpy(s->title, L.trk[0].name, sizeof s->title);
  else
    for (u32 t = 0; t < L.nTrk && !s->title[0]; ++t) kf_strcpy(s->title, L.trk[t].name, sizeof s->title);
  /* pass 2: the events and notes, a track after the other, then merged */
  s->ev = (MEv *)kf_alloc(nEv * sizeof(MEv));
  s->notes = (Note *)kf_alloc(nNotes * sizeof(Note));
  if (!s->ev || !s->notes) return *why = "out of memory", -2;
  const u32 mark = kf_mark();
  MEv *tev = (MEv *)kf_alloc(nEv * sizeof(MEv));
  Note *tno = (Note *)kf_alloc(nNotes * sizeof(Note));
  s16 *open = (s16 *)kf_alloc(16 * 128 * sizeof(s16));
  u32 *evAt = (u32 *)kf_alloc((L.nTrk + 1) * sizeof(u32)), *noAt = (u32 *)kf_alloc((L.nTrk + 1) * sizeof(u32));
  if (!tev || !tno || !open || !evAt || !noAt) return kf_release(mark), *why = "out of memory", -2;
  u32 e = 0, n = 0, endTick = 0;
  for (u32 t = 0; t < L.nTrk; ++t) {
    for (int i = 0; i < 16 * 128; ++i) open[i] = -1;
    Sink f = {1, tev + e, tno + n, open, partOf[t], s->part, 0, 0};
    pass(t, &f);
    evAt[t] = e, noAt[t] = n;
    e += f.nEv, n += f.nNotes;
    if (L.trk[t].endTick > endTick) endTick = L.trk[t].endTick;
  }
  evAt[L.nTrk] = e, noAt[L.nTrk] = n;
  /* merged by tick, a track before the next at the same tick (each track in its own order) */
  u32 cur[KF_MAX_TRACKS];
  for (u32 t = 0; t < L.nTrk; ++t) cur[t] = evAt[t];
  for (u32 out = 0; out < e; ++out) {
    u32 best = KF_MAX_TRACKS;
    for (u32 t = 0; t < L.nTrk; ++t)
      if (cur[t] < evAt[t + 1] && (best == KF_MAX_TRACKS || tev[cur[t]].tick < tev[cur[best]].tick)) best = t;
    MEv *x = &s->ev[out];
    *x = tev[cur[best]++];
    x->ms = ms_of(x->tick);
  }
  s->nEv = e;
  for (u32 t = 0; t < L.nTrk; ++t) cur[t] = noAt[t];
  for (u32 out = 0; out < n; ++out) {
    u32 best = KF_MAX_TRACKS;
    for (u32 t = 0; t < L.nTrk; ++t)
      if (cur[t] < noAt[t + 1] && (best == KF_MAX_TRACKS || tno[cur[t]].ms < tno[cur[best]].ms)) best = t;
    Note *x = &s->notes[out];
    *x = tno[cur[best]++];
    const u32 startTick = x->ms, endT = x->ms + x->dur;
    x->ms = ms_of(startTick);
    x->dur = ms_of(endT) - x->ms;
    if (x->ms + x->dur > s->lengthMs) s->lengthMs = x->ms + x->dur;
  }
  s->nNotes = n;
  for (u32 i = 0; i < s->nParts; ++i) {  /* each part's first program change */
    Part *pt = &s->part[i];
    for (u32 j = 0; j < e; ++j)
      if (s->ev[j].trk == pt->trk && s->ev[j].st == (0xC0 | pt->ch)) {
        pt->program = s->ev[j].d1;
        break;
      }
  }
  kf_release(mark);
  /* the beats, by the time signatures */
  for (u32 i = 1; i < L.nSig; ++i)
    for (u32 j = i; j > 0 && L.sig[j - 1].tick > L.sig[j].tick; --j) {
      const TimeSig x = L.sig[j];
      L.sig[j] = L.sig[j - 1];
      L.sig[j - 1] = x;
    }
  u32 minStep = L.ppq;  /* (the shortest beat of any time signature: an upper bound on the beats) */
  for (u32 i = 0; i < L.nSig; ++i) {
    const u32 st = (u32)L.ppq * 4 >> (L.sig[i].den < 6 ? L.sig[i].den : 2);
    if (st && st < minStep) minStep = st;
  }
  const u32 maxBeats = endTick / minStep + L.nSig + 2;
  s->beat = (u32 *)kf_alloc(maxBeats * sizeof(u32));
  if (!s->beat) return *why = "out of memory", -2;
  u32 tick = 0, step = L.ppq, num = 4, inBar = 0, si = 0;
  while (tick <= endTick && s->nBeats < maxBeats) {
    while (si < L.nSig && L.sig[si].tick <= tick) {
      num = L.sig[si].num ? L.sig[si].num : 4;
      const u32 den = L.sig[si].den < 6 ? L.sig[si].den : 2;
      step = (u32)L.ppq * 4 >> den;
      if (!step) step = 1;
      inBar = 0;
      ++si;
    }
    s->beat[s->nBeats++] = ms_of(tick) | (inBar == 0 ? 0x80000000u : 0);
    inBar = (inBar + 1) % num;
    tick += step;
  }
  return 0;
}
