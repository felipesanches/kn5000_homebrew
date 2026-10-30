/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * scores.c -- the best scores, in SCORES.TXT in the game's folder: a line each, "FILE PARTS TEMPO SCORE ACCURACY"
 * (the MIDI file's 8.3 name; the parts played, a bit each, in hexadecimal; the tempo in per cent; the score; the
 * accuracy in tenths of a per cent), for games played in PLAY mode to their end. Read when the game starts, written
 * afresh (PfHost::save) when a game beats the best. */
#include "play.h"

extern const PfHost *host;

#define MAXS 64
typedef struct Best {
  char file[13];
  u32 parts, pct, score, acc;
} Best;
static Best best[MAXS];
static int nBest;
static char text[MAXS * 48];

static u32 num(const char **p, int base) {
  while (**p == ' ') ++*p;
  u32 v = 0;
  for (;; ++*p) {
    const char c = **p;
    const int d = c >= '0' && c <= '9' ? c - '0' : base == 16 && c >= 'A' && c <= 'F' ? c - 'A' + 10 : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (d < 0) return v;
    v = v * (u32)base + (u32)d;
  }
}

void scores_load(void) {
  nBest = 0;
  PfPiece piece = {text, sizeof text - 1};
  const s32 got = host->load("SCORES.TXT", &piece, 1);
  if (got <= 0) return;
  text[got] = 0;
  for (const char *p = text; *p && nBest < MAXS;) {
    Best *b = &best[nBest];
    int i = 0;
    while (*p == ' ') ++p;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < 12) b->file[i++] = *p++;
    b->file[i] = 0;
    b->parts = num(&p, 16), b->pct = num(&p, 10), b->score = num(&p, 10), b->acc = num(&p, 10);
    while (*p && *p != '\n') ++p;
    if (*p) ++p;
    if (b->file[0] && b->score) ++nBest;
  }
  kf_log("scores: %d read", nBest);
}

static Best *find(const char *file, u32 parts, u32 pct) {
  for (int i = 0; i < nBest; ++i)
    if (best[i].parts == parts && best[i].pct == pct && kf_streq_ci(best[i].file, file)) return &best[i];
  return 0;
}

u32 scores_best(const char *file, u32 parts, u32 pct, u32 *acc) {
  const Best *b = find(file, parts, pct);
  *acc = b ? b->acc : 0;
  return b ? b->score : 0;
}

int scores_offer(const char *file, u32 parts, u32 pct, u32 score, u32 acc) {
  Best *b = find(file, parts, pct);
  if (b && b->score >= score) return 0;
  if (!b) {
    if (nBest >= MAXS) return 0;
    b = &best[nBest++];
    kf_strcpy(b->file, file, sizeof b->file);
    b->parts = parts, b->pct = pct;
  }
  b->score = score, b->acc = acc;
  int n = 0;
  for (int i = 0; i < nBest; ++i)
    n += kf_fmt(text + n, (int)sizeof text - n, "%s %X %u %u %u\r\n", best[i].file, best[i].parts, best[i].pct,
                best[i].score, best[i].acc);
  const s32 r = host->save("SCORES.TXT", text, (u32)n);
  kf_log("scores: saved, %d bytes -> %d", n, r);
  return 1;
}
