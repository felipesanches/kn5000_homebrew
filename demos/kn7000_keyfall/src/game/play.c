/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * play.c -- one game: the chart's notes fall down the LCD onto a keyboard drawn at its bottom, a lane for each key
 * shown (draw.h); the player strikes them on the keybed as they reach it; the accompaniment goes into MIDI IN 1's ring
 * as its time comes (pfhost.h midiIn), and the instrument plays it as it plays what a sequencer sends. The keybed sounds
 * as the instrument has it set (KN7APP takes nothing of it): the game only reads its keys (keyEvents).
 *
 * The screen: a line of score at the top (HUD_H), the notes' lanes below it down to the keyboard (HIT_Y), the keyboard
 * (KEY_Y to the bottom). A note's lower edge reaches the keyboard at its time; it falls `speed` lines a second.
 *
 * Drawing: only what changes, as display memory is slow (draw.c). The lanes are cut into vertical segments at every
 * key's edge (a segment lies in at most one white key's lane and one black key's); a pixel's colour is the note of the
 * black key's lane over it, else the white key's, else the lane's background or a beat's line (compose). Each frame,
 * every note that moved adds the rows its edges crossed in its lane (or all its rows when its colour changed), and
 * every beat line its old and new row across the whole width; those rows, and only those, are drawn again by compose.
 *
 * Modes (kf.h): PLAY judges every key (judge.c); WAIT stops the song's clock at each note until its key is struck (the
 * accompaniment waits too); DEMO plays the player's part too, and judges nothing. */
#include "draw.h"
#include "play.h"

extern const PfHost *host;

#define HUD_H 16
#define HWY_Y0 HUD_H
#define HIT_Y 194
#define HWY_H (HIT_Y - HWY_Y0)
#define KEY_Y HIT_Y
#define BLACK_H 27
#define MIN_NOTE_H 5
#define HIT_FLASH_MS 160
#define WRONG_FLASH_MS 220
#define GUIDE_MS 300      /* a key is tinted this long before its next note */
#define JUDGE_SHOW_MS 700
#define END_PAD_MS 1500   /* the song ends this long after its last note */

u32 kf_speed = 110;       /* lines a second (menu.c) */
s32 kf_latency;           /* ms taken off each key's time before it is judged (menu.c) */

/* what tests/mame/keyfall_test.lua reads */
volatile s32 play_clock;
volatile u32 play_state;   /* 0 none, 1 playing, 2 done (the results up), 3 stopped by EXIT */
u32 play_frames, play_loop_max, play_draw_max, play_rows, play_out_sent, play_out_full, play_key_ons;
s32 play_last_dt;
const Chart *play_chart;
Judge play_judge;

static int is_black(int n) { return (0x54A >> (n % 12)) & 1; }

/* ---- the keyboard's and the lanes' geometry ---- */
#define MAXSEG 320
typedef struct Seg {
  s16 x0, x1;
  s16 white, black;  /* the keys (index from lo) whose lanes cover it, or -1 */
  u8 bg;
} Seg;
static Seg seg[MAXSEG];
static int nSeg, lo, nKeys;
static s16 keyX[128], keyW[128], laneX0[128], laneX1[128];
static u16 laneSegA[128], laneSegB[128];
static s16 laneHead[128];
static u8 keyShown[128];
static u32 hitUntil[128], wrongUntil[128];
static u8 guide[128], sounding[128];

static void layout(const Chart *c) {
  lo = c->lo;
  nKeys = c->hi - c->lo + 1;
  int nWhite = 0;
  for (int k = 0; k < nKeys; ++k) nWhite += !is_black(lo + k);
  int kw = FB_W / nWhite;
  if (kw > 48) kw = 48;
  const int x0 = (FB_W - nWhite * kw) / 2, xEnd = x0 + nWhite * kw;
  int bw = kw * 5 / 8;
  if (bw < 4) bw = 4;
  s16 xs[4 * 128 + 8];
  int nx = 0;
  xs[nx++] = 0, xs[nx++] = (s16)x0, xs[nx++] = (s16)xEnd, xs[nx++] = FB_W;
  int wi = 0;
  for (int k = 0; k < nKeys; ++k) {
    if (!is_black(lo + k)) {
      const int wx = x0 + wi * kw;
      keyX[k] = (s16)wx, keyW[k] = (s16)kw;
      laneX0[k] = (s16)(wx + 1), laneX1[k] = (s16)(wx + kw);
      xs[nx++] = (s16)wx, xs[nx++] = (s16)(wx + 1);
      ++wi;
    } else {
      const int bx = x0 + wi * kw - bw / 2;
      keyX[k] = (s16)bx, keyW[k] = (s16)bw;
      laneX0[k] = (s16)bx, laneX1[k] = (s16)(bx + bw);
      xs[nx++] = (s16)bx, xs[nx++] = (s16)(bx + bw);
    }
  }
  for (int i = 1; i < nx; ++i)
    for (int j = i; j > 0 && xs[j - 1] > xs[j]; --j) {
      const s16 t = xs[j];
      xs[j] = xs[j - 1];
      xs[j - 1] = t;
    }
  nSeg = 0;
  for (int i = 0; i + 1 < nx && nSeg < MAXSEG; ++i) {
    const int a = xs[i], b = xs[i + 1];
    if (b <= a || a < 0 || a >= FB_W) continue;
    Seg *s = &seg[nSeg++];
    s->x0 = (s16)a, s->x1 = (s16)b, s->white = s->black = -1;
    for (int k = 0; k < nKeys; ++k)
      if (a >= laneX0[k] && a < laneX1[k]) {
        if (is_black(lo + k)) s->black = (s16)k;
        else s->white = (s16)k;
      }
    if (a < x0 || a >= xEnd) s->bg = C_MARGIN;
    else if (s->black >= 0) s->bg = C_LANE_B;
    else if (s->white >= 0) s->bg = C_LANE_W;
    else {  /* a white key's left edge: an octave's (C) a little brighter */
      s->bg = C_SEP;
      for (int k = 0; k < nKeys; ++k)
        if (!is_black(lo + k) && keyX[k] == a && (lo + k) % 12 == 0) s->bg = C_OCT;
    }
  }
  for (int k = 0; k < nKeys; ++k) {
    int A = 0;
    while (A < nSeg && seg[A].x0 < laneX0[k]) ++A;
    int B = A;
    while (B < nSeg && seg[B].x0 < laneX1[k]) ++B;
    laneSegA[k] = (u16)A, laneSegB[k] = (u16)B;
  }
}

/* ---- the rows: beats, bars and the hit line ---- */
static u8 rowKind[HWY_H];  /* 0 nothing, 1 a beat, 2 a bar's first beat, 3 the hit line */
#define MAXLINES 24
static s16 lineY[MAXLINES], lineYNew[MAXLINES];
static u8 lineK[MAXLINES], lineKNew[MAXLINES];
static int nLines, nLinesNew;

/* ---- what is drawn ---- */
static ChartNote *note;
static u32 nNotes, vis0;
static int mode;

static u8 note_colour(const ChartNote *n) {
  const int b = is_black(n->note);
  if (mode == MODE_DEMO) return b ? C_DEMO_B : C_DEMO_W;
  switch (n->state) {
    case NS_PENDING: return n->hand ? (b ? C_NOTE1_B : C_NOTE1_W) : (b ? C_NOTE0_B : C_NOTE0_W);
    case NS_MISSED: return C_MISSED;
    default: return n->hand ? C_HIT1 : C_HIT0;
  }
}

static u8 compose(int s, int y) {
  const Seg *g = &seg[s];
  if (g->black >= 0)
    for (int i = laneHead[g->black]; i >= 0; i = note[i].next == 0xFFFF ? -1 : note[i].next)
      if (y >= note[i].y0 && y < note[i].y1) return note[i].drawn;
  if (g->white >= 0)
    for (int i = laneHead[g->white]; i >= 0; i = note[i].next == 0xFFFF ? -1 : note[i].next)
      if (y >= note[i].y0 && y < note[i].y1) return note[i].drawn;
  if (g->bg == C_MARGIN) return C_MARGIN;
  switch (rowKind[y - HWY_Y0]) {
    case 1: return C_BEAT;
    case 2: return C_BAR;
    case 3: return C_HITLINE;
  }
  return g->bg;
}

static void repaint_lane(int k, int y0, int y1) {
  if (y0 < HWY_Y0) y0 = HWY_Y0;
  if (y1 > HIT_Y) y1 = HIT_Y;
  for (int s = laneSegA[k]; s < laneSegB[k]; ++s) {
    int r = y0;
    u8 c = r < y1 ? compose(s, r) : 0;
    while (r < y1) {
      int r2 = r + 1;
      u8 c2 = 0;
      while (r2 < y1 && (c2 = compose(s, r2)) == c) ++r2;
      fill8(seg[s].x0, r, seg[s].x1 - seg[s].x0, r2 - r, c);
      play_rows += (u32)(r2 - r);
      r = r2, c = c2;
    }
  }
}

static void repaint_row(int y) {
  if (y < HWY_Y0 || y >= HIT_Y) return;
  int s = 0;
  while (s < nSeg) {
    const u8 c = compose(s, y);
    int e = s + 1;
    while (e < nSeg && compose(e, y) == c) ++e;
    fill8(seg[s].x0, y, seg[e - 1].x1 - seg[s].x0, 1, c);
    s = e;
  }
  ++play_rows;
}

/* the dirty rows of this frame */
#define MAXDIRTY 512
static struct {
  s16 key, y0, y1;
} dirty[MAXDIRTY];
static int nDirty, dirtyAll;

static void mark(int key, int y0, int y1) {
  if (y0 < HWY_Y0) y0 = HWY_Y0;
  if (y1 > HIT_Y) y1 = HIT_Y;
  if (y1 <= y0) return;
  if (nDirty >= MAXDIRTY) {
    dirtyAll = 1;
    return;
  }
  dirty[nDirty].key = (s16)key, dirty[nDirty].y0 = (s16)y0, dirty[nDirty].y1 = (s16)y1;
  ++nDirty;
}

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

/* the rows of note n at the clock: [*y0, *y1) inside the lanes, empty when *y0 >= *y1 */
static void rows_of(const ChartNote *n, s32 clock, int *y0, int *y1) {
  const s32 yb = HIT_Y - (s32)((n->t - clock) * (s32)kf_speed / 1000);
  s32 h = (s32)((s32)n->dur * (s32)kf_speed / 1000);
  if (h < MIN_NOTE_H) h = MIN_NOTE_H;
  const s32 yt = yb - h;
  *y0 = yt < HWY_Y0 ? HWY_Y0 : yt > HIT_Y ? HIT_Y : (int)yt;
  *y1 = yb > HIT_Y ? HIT_Y : yb < HWY_Y0 ? HWY_Y0 : (int)yb;
}

static void frame_notes(s32 clock) {
  nDirty = 0;
  const s32 ahead = clock + (s32)(HWY_H * 1000 / kf_speed) + 50;
  for (int k = 0; k < nKeys; ++k) laneHead[k] = -1, guide[k] = 0, sounding[k] = 0;
  for (u32 i = vis0; i < nNotes && note[i].t <= ahead; ++i) {
    ChartNote *n = &note[i];
    const int k = n->note - lo;
    if (k < 0 || k >= nKeys) continue;
    int y0, y1;
    rows_of(n, clock, &y0, &y1);
    const int empty = y0 >= y1;
    const u8 col = empty ? 0 : note_colour(n);
    if (!n->drawn && !empty) mark(k, y0, y1);
    else if (n->drawn && empty) mark(k, n->y0, n->y1);
    else if (n->drawn && col != n->drawn) mark(k, min_i(n->y0, y0), max_i(n->y1, y1));
    else if (n->drawn) {
      if (y0 != n->y0) mark(k, min_i(n->y0, y0), max_i(n->y0, y0));
      if (y1 != n->y1) mark(k, min_i(n->y1, y1), max_i(n->y1, y1));
    }
    n->drawn = col;
    n->y0 = (s16)(empty ? 0 : y0), n->y1 = (s16)(empty ? 0 : y1);
    if (col) {
      n->next = laneHead[k] < 0 ? 0xFFFF : (u16)laneHead[k];
      laneHead[k] = (s16)i;
    }
    if (n->state == NS_PENDING && n->t - clock <= GUIDE_MS && n->t - clock >= -WIN_OK) guide[k] = 1;
    if (clock >= n->t && clock < n->t + (s32)n->dur) sounding[k] = 1;
  }
  while (vis0 < nNotes && !note[vis0].drawn && note[vis0].t + (s32)note[vis0].dur < clock - 5000) ++vis0;
}

static void frame_lines(const Chart *c, s32 clock) {
  nLinesNew = 0;
  const s32 ahead = clock + (s32)(HWY_H * 1000 / kf_speed);
  for (u32 i = 0; i < c->nBeat && nLinesNew < MAXLINES; ++i) {
    const s32 t = c->beat[i] & 0x3FFFFFFF;
    if (t < clock || t > ahead) continue;
    const s32 y = HIT_Y - (s32)((t - clock) * (s32)kf_speed / 1000);
    if (y < HWY_Y0 || y >= HIT_Y - 2) continue;
    lineYNew[nLinesNew] = (s16)y, lineKNew[nLinesNew] = (c->beat[i] & 0x40000000) ? 2 : 1;
    ++nLinesNew;
  }
  /* the old rows cleared, the new set; both drawn again */
  for (int i = 0; i < nLines; ++i) rowKind[lineY[i] - HWY_Y0] = 0;
  for (int i = 0; i < nLinesNew; ++i) rowKind[lineYNew[i] - HWY_Y0] = lineKNew[i];
  for (int i = 0; i < nLines; ++i) {
    int same = 0;
    for (int j = 0; j < nLinesNew; ++j) same |= lineYNew[j] == lineY[i] && lineKNew[j] == lineK[i];
    if (!same) mark(-1, lineY[i], lineY[i] + 1);
  }
  for (int j = 0; j < nLinesNew; ++j) {
    int same = 0;
    for (int i = 0; i < nLines; ++i) same |= lineYNew[j] == lineY[i] && lineKNew[j] == lineK[i];
    if (!same) mark(-1, lineYNew[j], lineYNew[j] + 1);
  }
  for (int i = 0; i < nLinesNew; ++i) lineY[i] = lineYNew[i], lineK[i] = lineKNew[i];
  nLines = nLinesNew;
}

static void draw_dirty(void) {
  if (dirtyAll) {
    for (int y = HWY_Y0; y < HIT_Y; ++y) repaint_row(y);
    dirtyAll = 0;
    nDirty = 0;
    return;
  }
  for (int i = 0; i < nDirty; ++i) {
    if (dirty[i].key < 0)
      for (int y = dirty[i].y0; y < dirty[i].y1; ++y) repaint_row(y);
    else
      repaint_lane(dirty[i].key, dirty[i].y0, dirty[i].y1);
  }
  nDirty = 0;
}

/* ---- the keyboard ---- */
static void draw_key(int k, u8 c) {
  keyShown[k] = c;
  if (is_black(lo + k)) {
    fill8(keyX[k], KEY_Y, keyW[k], BLACK_H, c);
    return;
  }
  fill8(keyX[k] + 1, KEY_Y, keyW[k] - 1, FB_H - KEY_Y, c);
  for (int d = -1; d <= 1; d += 2)
    if (k + d >= 0 && k + d < nKeys && is_black(lo + k + d)) fill8(keyX[k + d], KEY_Y, keyW[k + d], BLACK_H, keyShown[k + d]);
}

static u8 key_colour(int k, u32 now) {
  const int b = is_black(lo + k);
  if ((s32)(hitUntil[k] - now) > 0) return C_KEY_HIT;
  if ((s32)(wrongUntil[k] - now) > 0) return C_KEY_WRONG;
  if ((host->notes[lo + k] & 7) || (mode == MODE_DEMO && sounding[k])) return b ? C_KEY_HELD_B : C_KEY_HELD_W;
  if (guide[k] && mode != MODE_DEMO) return b ? C_KEY_NEXT_B : C_KEY_NEXT_W;
  return b ? C_KEY_B : C_KEY_W;
}

static void keyboard_all(u32 now) {
  fill8(0, KEY_Y, FB_W, FB_H - KEY_Y, C_BLACK);
  for (int k = 0; k < nKeys; ++k)
    if (!is_black(lo + k)) fill8(keyX[k], KEY_Y, 1, FB_H - KEY_Y, C_KEY_SEP);
  for (int k = 0; k < nKeys; ++k) keyShown[k] = key_colour(k, now);
  for (int k = 0; k < nKeys; ++k)
    if (!is_black(lo + k)) draw_key(k, keyShown[k]);
  for (int k = 0; k < nKeys; ++k)
    if (is_black(lo + k)) draw_key(k, keyShown[k]);
}

static void keyboard_update(u32 now) {
  for (int k = 0; k < nKeys; ++k) {
    const u8 c = key_colour(k, now);
    if (c != keyShown[k]) draw_key(k, c);
  }
}

/* ---- the line at the top ---- */
static char hudScore[24], hudJudge[24], hudStreak[24], hudAcc[24];
static u32 judgeUntil, progressX;

static void hud_field(char *cache, const char *s, int x, int w, u8 fg) {
  if (!kf_streq_ci(cache, s) || !cache[0]) {
    fill8(x, 1, w, 13, C_HUD);
    text8(s, x, 2, fg, C_HUD);
    kf_strcpy(cache, s, 24);
  }
}

static void hud(const Chart *c, const Judge *j, s32 clock, u32 now, const char *title) {
  char b[40];
  if (mode == MODE_DEMO) {
    hud_field(hudScore, "DEMO", 4, 120, C_ORANGE);
    kf_fmt(b, sizeof b, "%s", title);
    hud_field(hudJudge, b, 140, 300, C_WHITE);
  } else {
    if (mode == MODE_WAIT) kf_fmt(b, sizeof b, "WAIT %u/%u", j->perfect + j->good + j->ok, j->count);
    else kf_fmt(b, sizeof b, "SCORE %u", j->score);
    hud_field(hudScore, b, 4, 132, C_WHITE);
    const char *w = "";
    u8 col = C_WHITE;
    if ((s32)(judgeUntil - now) > 0) {
      switch (j->last) {
        case NS_PERFECT: w = "PERFECT", col = C_YELLOW; break;
        case NS_GOOD: w = "GOOD", col = C_GREEN; break;
        case NS_OK: w = "OK", col = C_CYAN; break;
        case NS_MISSED: w = "MISS", col = C_RED; break;
        case -1: w = "WRONG KEY", col = C_RED; break;
      }
    }
    hud_field(hudJudge, w, 160, 96, col);
    kf_fmt(b, sizeof b, "STREAK %u  x%u", j->streak, judge_multiplier(j->streak));
    hud_field(hudStreak, b, 280, 150, C_CYAN);
    const u32 judged = j->perfect + j->good + j->ok + j->miss;
    const u32 acc = judged ? (j->perfect * 1000 + j->good * 700 + j->ok * 400) / judged : 1000;
    kf_fmt(b, sizeof b, "%u.%u%%", acc / 10, acc % 10);
    hud_field(hudAcc, b, 560, 76, C_WHITE);
  }
  /* the song's progress: a line under the text */
  const s32 len = c->length > 0 ? c->length : 1;
  s32 x = clock <= 0 ? 0 : (s32)((u64)(u32)clock * FB_W / (u32)len);
  if (x > FB_W) x = FB_W;
  if ((u32)x > progressX) {
    fill8((int)progressX, HUD_H - 2, x - (int)progressX, 2, C_PROGRESS);
    progressX = (u32)x;
  }
}

/* ---- the game ---- */
static u16 out_channels(const Chart *c) {
  u16 m = 0;
  for (u32 i = 0; i < c->nOut; ++i) m |= (u16)(1u << (c->out[i].m[0] & 0x0F));
  return m;
}

static void all_notes_off(u16 chans) {
  for (int ch = 0; ch < 16; ++ch)
    if ((chans >> ch) & 1) {
      const u8 a[3] = {(u8)(0xB0 | ch), 64, 0}, b[3] = {(u8)(0xB0 | ch), 123, 0};
      for (int tries = 0; tries < 50 && host->midiIn(a, 3); ++tries) host->yield(2);
      for (int tries = 0; tries < 50 && host->midiIn(b, 3); ++tries) host->yield(2);
    }
}

int play_run(Chart *c, int m, const char *title, Judge *j) {
  mode = m;
  note = c->note, nNotes = c->n, vis0 = 0;
  play_chart = c;
  for (u32 i = 0; i < nNotes; ++i) note[i].drawn = 0, note[i].next = 0xFFFF;
  judge_init(j, note, nNotes);
  layout(c);
  kf_memset(rowKind, 0, sizeof rowKind);
  rowKind[HWY_H - 2] = rowKind[HWY_H - 1] = 3;
  nLines = 0;
  for (int k = 0; k < 128; ++k) hitUntil[k] = wrongUntil[k] = 0;
  hudScore[0] = hudJudge[0] = hudStreak[0] = hudAcc[0] = 0;
  judgeUntil = 0, progressX = 0;
  play_frames = play_loop_max = play_draw_max = play_rows = play_out_sent = play_out_full = play_key_ons = 0;
  ink_clear();
  fill8(0, 0, FB_W, HUD_H, C_HUD);
  for (int y = HWY_Y0; y < HIT_Y; ++y) repaint_row(y);
  u32 now = host->ms();
  keyboard_all(now);
  const u16 chans = out_channels(c);
  s32 clock = chart_start();
  u32 outAt = 0, last = now, exitWas = 1;
  int aborted = 0;
  PfKeyEvent ev[32];
  kf_log("play: %u notes, keys %u-%u, %u messages on %u channels, mode %d, speed %u", nNotes, c->lo, c->hi, c->nOut,
         c->outCh, m, kf_speed);
  play_state = 1;
  for (;;) {
    now = host->ms();
    const u32 dt = now - last;
    last = now;
    if (dt > play_loop_max && play_frames > 2) play_loop_max = dt;
    const int frozen = mode == MODE_WAIT && j->first < j->count && note[j->first].t <= clock;
    if (!frozen) clock += (s32)dt;
    if (mode == MODE_WAIT && j->first < j->count && note[j->first].t < clock && !frozen) clock = note[j->first].t;
    play_clock = clock;
    /* the keys */
    host->pollNotes();
    const u32 n = host->keyEvents(ev, 32);
    for (u32 i = 0; i < n; ++i) {
      if (!ev[i].on) continue;
      ++play_key_ons;
      const int k = ev[i].note - lo;
      if (mode == MODE_DEMO) continue;
      const s32 tp = (mode == MODE_WAIT ? clock : clock - (s32)(now - ev[i].ms)) - kf_latency;
      const int hit = judge_press(j, ev[i].note, tp);
      if (hit >= 0) play_last_dt = note[hit].dt;
      if (k >= 0 && k < nKeys) {
        if (hit >= 0) hitUntil[k] = now + HIT_FLASH_MS, wrongUntil[k] = 0;
        else wrongUntil[k] = now + WRONG_FLASH_MS;
      }
      judgeUntil = now + JUDGE_SHOW_MS;
    }
    if (mode == MODE_PLAY && judge_pass(j, clock)) judgeUntil = now + JUDGE_SHOW_MS;
    /* the accompaniment, as its time comes (a full ring: the rest at the next pass) */
    while (outAt < c->nOut && c->out[outAt].t <= clock) {
      if (host->midiIn(c->out[outAt].m, c->out[outAt].n)) {
        ++play_out_full;
        break;
      }
      ++play_out_sent;
      ++outAt;
    }
    /* the screen */
    const u32 d0 = host->ms();
    frame_notes(clock);
    frame_lines(c, clock);
    draw_dirty();
    keyboard_update(now);
    hud(c, j, clock, now, title);
    const u32 dd = host->ms() - d0;
    if (dd > play_draw_max) play_draw_max = dd;
    ++play_frames;
    /* EXIT: the song stopped (held 1.5 s, KN7APP ends the program too) */
    host->panelScan();
    const int exitNow = (host->panel[BTN_EXIT >> 8] & (BTN_EXIT & 0xFF)) != 0;
    if (exitNow && !exitWas) {
      aborted = 1;
      break;
    }
    exitWas = exitNow;
    if (clock >= c->length + END_PAD_MS && outAt >= c->nOut && (mode != MODE_WAIT || j->first >= j->count)) break;
    host->yield(1);
  }
  all_notes_off(chans);
  if (mode == MODE_PLAY) judge_pass(j, 0x3FFFFFFF);
  play_state = aborted ? 3 : 2;
  kf_log("play: %s at %d ms: %u perfect %u good %u ok %u miss %u wrong, score %u, streak %u, frames %u, loop max %u ms, "
         "draw max %u ms, messages %u (ring full %u)", aborted ? "stopped" : "done", clock, j->perfect, j->good, j->ok,
         j->miss, j->wrong, j->score, j->maxStreak, play_frames, play_loop_max, play_draw_max, play_out_sent,
         play_out_full);
  return aborted;
}
