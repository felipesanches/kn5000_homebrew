/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * kfcheck.c -- the game's song reading (smf.c), charts (song.c) and judging (judge.c) on the PC, the same C as on the
 * KN7000 (tests/host/README.md): for each MIDI file named, what the game reads of it (title, parts, notes, beats,
 * length) and, for each part choice the menu offers, the chart at 100% and 50% with the accompaniment, played by a
 * perfect player, a player 70 ms late, one who strikes a wrong key before every fourth note, and one who misses every
 * third: the counts and the accuracy each gets. Exits 1 when any expectation fails. */
#include <stdio.h>
#include <stdlib.h>

#include "kf.h"

static u8 *heap;
static u32 heapAt, heapSize = 4u << 20;
void *kf_alloc(u32 bytes) {
  bytes = (bytes + 3) & ~3u;
  if (heapAt + bytes > heapSize) return 0;
  void *p = heap + heapAt;
  heapAt += bytes;
  kf_memset(p, 0, bytes);
  return p;
}
u32 kf_mark(void) { return heapAt; }
void kf_release(u32 mark) { heapAt = mark; }
void kf_log(const char *fmt, ...) {
  char b[256];
  va_list ap;
  va_start(ap, fmt);
  kf_vfmt(b, sizeof b, fmt, ap);
  va_end(ap);
  puts(b);
}

static int fails;
static void expect(int ok, const char *what) {
  if (!ok) ++fails;
  printf("    %s %s\n", ok ? "ok  " : "FAIL", what);
}

typedef enum { P_PERFECT, P_LATE70, P_WRONG4, P_MISS3 } Player;

static void play(const Chart *c0, Player who, Judge *j) {
  static ChartNote copy[8192];
  const u32 n = c0->n < 8192 ? c0->n : 8192;
  for (u32 i = 0; i < n; ++i) copy[i] = c0->note[i];
  judge_init(j, copy, n);
  for (u32 i = 0; i < n; ++i) {
    const s32 t = copy[i].t + (who == P_LATE70 ? 70 : 0);
    judge_pass(j, t);
    if (who == P_MISS3 && i % 3 == 2) continue;
    if (who == P_WRONG4 && i % 4 == 0) judge_press(j, (u8)(copy[i].note + 1 > 127 ? 0 : copy[i].note + 1), t - 1);
    judge_press(j, copy[i].note, t);
  }
  judge_pass(j, copy[n ? n - 1 : 0].t + 10000);
}

int main(int argc, char **argv) {
  heap = malloc(heapSize);
  for (int a = 1; a < argc; ++a) {
    FILE *f = fopen(argv[a], "rb");
    if (!f) { printf("%s: cannot open\n", argv[a]); ++fails; continue; }
    static u8 data[1 << 20];
    const u32 bytes = (u32)fread(data, 1, sizeof data, f);
    fclose(f);
    heapAt = 0;
    Song s;
    const char *why;
    const int r = smf_load(&s, data, bytes, &why);
    printf("%s: %u bytes, smf_load %d %s\n", argv[a], bytes, r, why);
    if (r < 0) { ++fails; continue; }
    printf("  title \"%s\", format %u, %u tracks, ppq %u; %u events, %u notes, %u beats, %u.%03u s; %u bytes of memory\n",
           s.title, s.format, s.tracks, s.ppq, s.nEv, s.nNotes, s.nBeats, s.lengthMs / 1000, s.lengthMs % 1000, heapAt);
    for (u32 i = 0; i < s.nParts; ++i)
      printf("  part %u: \"%s\" track %u ch %u, %u notes %u-%u, program %u%s\n", i, s.part[i].name, s.part[i].trk,
             s.part[i].ch + 1, s.part[i].notes, s.part[i].lo, s.part[i].hi, s.part[i].program, s.part[i].drums ? " (drums)" : "");
    u32 choice[KF_MAX_PARTS + 1];
    const int nc = song_part_choices(&s, choice, KF_MAX_PARTS + 1);
    const u32 def = song_default_parts(&s);
    for (int k = 0; k < nc; ++k) {
      char name[64];
      song_choice_name(&s, choice[k], name, sizeof name);
      for (int pct = 100; pct >= 50; pct -= 50) {
        const u32 mark = kf_mark();
        Chart c;
        const int cr = chart_build(&c, &s, choice[k], (u32)pct, 1, MODE_PLAY);
        printf("  choice %d \"%s\"%s at %d%%: chart %d, %u notes, keys %u-%u, %u messages out on %u channels, %d.%03d s\n", k,
               name, choice[k] == def ? " (the default)" : "", pct, cr, c.n, c.lo, c.hi, c.nOut, c.outCh,
               c.length / 1000, c.length % 1000);
        int sorted = 1, chanOk = 1;
        for (u32 i = 1; i < c.nOut; ++i) sorted &= c.out[i].t >= c.out[i - 1].t;
        for (u32 i = 0; i < c.nOut; ++i) chanOk &= (c.out[i].m[0] & 0x0F) >= 3;
        for (u32 i = 1; i < c.n; ++i) sorted &= c.note[i].t >= c.note[i - 1].t;
        expect(cr == 0 && c.n > 0, "a chart with notes");
        expect(sorted, "notes and messages in time order");
        expect(chanOk, "no message on channels 1-3 (the keybed's parts)");
        Judge j;
        play(&c, P_PERFECT, &j);
        printf("    perfect player: %u perfect %u good %u ok %u miss %u wrong, streak %u, score %u, accuracy %u.%u%%, %c\n",
               j.perfect, j.good, j.ok, j.miss, j.wrong, j.maxStreak, j.score, judge_accuracy(&j) / 10,
               judge_accuracy(&j) % 10, judge_grade(&j));
        expect(j.perfect == c.n && judge_accuracy(&j) == 1000 && judge_grade(&j) == 'S', "perfect: all PERFECT, 100%, S");
        play(&c, P_LATE70, &j);
        printf("    70 ms late: %u perfect %u good %u ok %u miss, accuracy %u.%u%%, mean offset %d ms\n", j.perfect, j.good,
               j.ok, j.miss, judge_accuracy(&j) / 10, judge_accuracy(&j) % 10, j.nDt ? j.sumDt / (s32)j.nDt : 0);
        expect(j.good == c.n && j.nDt && j.sumDt / (s32)j.nDt == 70, "70 ms late: all GOOD, mean offset 70");
        play(&c, P_WRONG4, &j);
        printf("    a wrong key before every 4th note: %u perfect, %u wrong, streak %u\n", j.perfect, j.wrong, j.maxStreak);
        expect(j.perfect == c.n && j.wrong == (c.n + 3) / 4, "wrong keys: counted, notes still PERFECT");
        play(&c, P_MISS3, &j);
        printf("    every 3rd note missed: %u perfect, %u miss, accuracy %u.%u%%\n", j.perfect, j.miss,
               judge_accuracy(&j) / 10, judge_accuracy(&j) % 10);
        expect(j.miss == c.n / 3 && j.perfect == c.n - c.n / 3, "misses: counted");
        kf_release(mark);
      }
    }
  }
  printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks ok");
  return fails ? 1 : 0;
}
