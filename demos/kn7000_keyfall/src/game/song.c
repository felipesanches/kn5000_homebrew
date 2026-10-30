/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * song.c -- a game's chart from a song (smf.c): the notes the player strikes, from the parts chosen, at the tempo chosen;
 * the MIDI messages of the other parts (the accompaniment), which the game puts into MIDI IN 1's ring as their time
 * comes, so that the instrument plays them as it plays what a sequencer sends it; and the beats.
 *
 * The accompaniment's channels: the instrument's MIDI parts PART 4 to PART 16 receive channels 4 to 16 (the owner's
 * manual, "Part Setting": its initialized settings); channels 1 to 3 are RIGHT 1, RIGHT 2 and LEFT, the parts the keybed
 * plays, and the game reads the player's keys from those (pfhost.h keyEvents). So a song's parts on channels 1 to 3 are
 * moved to channels the song leaves free among 4 to 16 (but 10, the drums), and channel 10 stays the drums. Each channel
 * starts with a bank select of the GM2 sound set (MSB 121 for sounds, 120 for drum kits: the instrument "includes GM2",
 * the manual's "NX SOUND"), the part's program, volume 100, pan centre and expression 127; every program change in the
 * song gets the same bank select before it, and the song's own bank selects are left out. */
#include "kf.h"

#define LEAD_MS 2500   /* the game's clock starts this long before the song: the first notes fall from the top */
#define SETUP_MS 2000  /* the channels set up this long before the song */

static int is_black(int n) { return (0x54A >> (n % 12)) & 1; }

static int named(const Part *p, const char *const *words) {
  for (; *words; ++words)
    if (kf_contains_ci(p->name, *words)) return 1;
  return 0;
}

static const char *const RIGHT[] = {"right", "rh", "melody", "treble", "lead", "tune", 0};
static const char *const LEFT[] = {"left", "lh", "bass hand", 0};

u32 song_default_parts(const Song *s) {
  for (u32 i = 0; i < s->nParts; ++i)
    if (!s->part[i].drums && named(&s->part[i], RIGHT)) return 1u << i;
  for (u32 i = 0; i < s->nParts; ++i)
    if (!s->part[i].drums) return 1u << i;
  return s->nParts ? 1u : 0;
}

/* each part but the drums on its own; and both hands, when a part is named for each */
int song_part_choices(const Song *s, u32 *choice, int room) {
  int n = 0, r = -1, l = -1;
  for (u32 i = 0; i < s->nParts && n < room; ++i) {
    if (s->part[i].drums) continue;
    choice[n++] = 1u << i;
    if (r < 0 && named(&s->part[i], RIGHT)) r = (int)i;
    else if (l < 0 && named(&s->part[i], LEFT)) l = (int)i;
  }
  if (r >= 0 && l >= 0 && n < room) choice[n++] = 1u << r | 1u << l;
  return n;
}

void song_choice_name(const Song *s, u32 parts, char *out, int room) {
  int n = 0;
  for (u32 i = 0; i < s->nParts; ++i) n += (parts >> i) & 1;
  if (n == 2) {
    int r = 0, l = 0;
    for (u32 i = 0; i < s->nParts; ++i)
      if ((parts >> i) & 1) r |= named(&s->part[i], RIGHT), l |= named(&s->part[i], LEFT);
    if (r && l) return kf_strcpy(out, "Both hands", room);
  }
  out[0] = 0;
  for (u32 i = 0; i < s->nParts; ++i)
    if ((parts >> i) & 1) {
      const int at = kf_strlen(out);
      kf_fmt(out + at, room - at, "%s%s", at ? " + " : "", s->part[i].name);
    }
}

static s32 scale(u32 ms, u32 pct) { return (s32)((u64)ms * 100 / pct); }

int chart_build(Chart *c, const Song *s, u32 parts, u32 pct, int accomp, int mode) {
  kf_memset(c, 0, sizeof *c);
  if (pct < 10) pct = 10;
  /* the player's notes */
  u32 n = 0;
  for (u32 i = 0; i < s->nNotes; ++i) n += (parts >> s->notes[i].part) & 1;
  c->note = (ChartNote *)kf_alloc((n + 1) * sizeof(ChartNote));
  if (!c->note) return -1;
  int lo = 127, hi = 0;
  u32 first = 0xFFFFFFFFu;
  for (u32 i = 0; i < s->nParts; ++i)
    if ((parts >> i) & 1) first = first == 0xFFFFFFFFu ? i : first;
  for (u32 i = 0; i < s->nNotes; ++i) {
    const Note *x = &s->notes[i];
    if (!((parts >> x->part) & 1)) continue;
    ChartNote *y = &c->note[c->n++];
    y->t = scale(x->ms, pct);
    y->dur = (u32)scale(x->dur, pct);
    y->note = x->note;
    y->hand = x->part != first;
    if (x->note < lo) lo = x->note;
    if (x->note > hi) hi = x->note;
  }
  if (!c->n) lo = 60, hi = 72;
  /* the keys shown: at least two octaves, from a white key to a white key */
  while (hi - lo < 24) {
    if (lo > 24) --lo;
    if (hi - lo < 24 && hi < 108) ++hi;
  }
  while (is_black(lo)) --lo;
  while (is_black(hi)) ++hi;
  c->lo = (u8)lo, c->hi = (u8)hi;
  /* the beats */
  c->beat = (s32 *)kf_alloc((s->nBeats + 1) * sizeof(s32));
  if (!c->beat) return -1;
  for (u32 i = 0; i < s->nBeats; ++i)
    c->beat[c->nBeat++] = scale(s->beat[i] & 0x7FFFFFFFu, pct) | (s->beat[i] & 0x80000000u ? 0x40000000 : 0);
  c->length = scale(s->lengthMs, pct);
  /* the accompaniment: which events go out, on which channels */
  u8 partOf[KF_MAX_TRACKS][16];
  kf_memset(partOf, 0xFF, sizeof partOf);
  for (u32 i = 0; i < s->nParts; ++i) partOf[s->part[i].trk][s->part[i].ch] = (u8)i;
  u16 used = 0;  /* the song's channels that send */
  u8 prog[16];
  kf_memset(prog, 0xFF, sizeof prog);
  u32 count = 0;
  for (u32 i = 0; i < s->nEv; ++i) {
    const MEv *e = &s->ev[i];
    const u8 ch = e->st & 0x0F, p = e->trk < KF_MAX_TRACKS ? partOf[e->trk][ch] : 0xFF;
    const int player = p != 0xFF && ((parts >> p) & 1);
    const int send = player ? mode == MODE_DEMO : accomp;
    if (!send) continue;
    used |= (u16)(1u << ch);
    if ((e->st & 0xF0) == 0xC0 && prog[ch] == 0xFF) prog[ch] = e->d1;
    count += (e->st & 0xF0) == 0xC0 ? 3 : 1;
  }
  u8 map[16];
  kf_memset(map, 0xFF, sizeof map);
  u16 taken = 0;
  for (int ch = 3; ch < 16; ++ch)
    if ((used >> ch) & 1) map[ch] = (u8)ch, taken |= (u16)(1u << ch);
  for (int ch = 0; ch < 3; ++ch) {
    if (!((used >> ch) & 1)) continue;
    for (int o = 3; o < 16; ++o)
      if (o != 9 && !((taken >> o) & 1)) {
        map[ch] = (u8)o, taken |= (u16)(1u << o);
        break;
      }
  }
  c->out = (OutEv *)kf_alloc((count + 16 * 8 + 1) * sizeof(OutEv));
  if (!c->out) return -1;
  for (int ch = 0; ch < 16; ++ch) {  /* each channel set up */
    const u8 o = map[ch];
    if (o == 0xFF) continue;
    ++c->outCh;
    const s32 t = -SETUP_MS + ch;
    const u8 b = (u8)(0xB0 | o);
    const OutEv setup[] = {{t, {b, 121, 0}, 3},
                           {t, {b, 0, ch == 9 ? 120 : 121}, 3},
                           {t, {b, 32, 0}, 3},
                           {t, {(u8)(0xC0 | o), prog[ch] == 0xFF ? 0 : prog[ch], 0}, 2},
                           {t, {b, 7, 100}, 3},
                           {t, {b, 10, 64}, 3},
                           {t, {b, 11, 127}, 3}};
    for (u32 k = 0; k < sizeof setup / sizeof *setup; ++k) c->out[c->nOut++] = setup[k];
  }
  for (u32 i = 0; i < s->nEv; ++i) {
    const MEv *e = &s->ev[i];
    const u8 ch = e->st & 0x0F, kind = e->st & 0xF0, p = e->trk < KF_MAX_TRACKS ? partOf[e->trk][ch] : 0xFF;
    const int player = p != 0xFF && ((parts >> p) & 1);
    const int send = player ? mode == MODE_DEMO : accomp;
    const u8 o = map[ch];
    if (!send || o == 0xFF) continue;
    if (kind == 0xB0 && (e->d1 == 0 || e->d1 == 32)) continue;  /* (bank selects: ours) */
    const s32 t = scale(e->ms, pct);
    if (kind == 0xC0) {
      c->out[c->nOut++] = (OutEv){t, {(u8)(0xB0 | o), 0, ch == 9 ? 120 : 121}, 3};
      c->out[c->nOut++] = (OutEv){t, {(u8)(0xB0 | o), 32, 0}, 3};
    }
    c->out[c->nOut++] = (OutEv){t, {(u8)(kind | o), e->d1, e->d2}, (u8)(kind == 0xC0 ? 2 : 3)};
  }
  return 0;
}

s32 chart_start(void) { return -LEAD_MS; }
