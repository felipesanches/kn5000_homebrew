/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * judge.c -- the judging. A key struck strikes the earliest note of its pitch not yet struck whose time is within
 * WIN_OK of the press: PERFECT within WIN_PERFECT ms, GOOD within WIN_GOOD, OK within WIN_OK (kf.h). A key that strikes
 * no note is a wrong key; a note nobody struck by WIN_OK after its time is missed. Both end the streak. A note struck
 * scores 100, 70 or 40 times the multiplier: 1 plus a tenth of the streak, at most 4. The accuracy weighs each note
 * the same way (100, 70, 40, 0 for a miss); wrong keys do not lower it, they end the streak. */
#include "kf.h"

void judge_init(Judge *j, ChartNote *n, u32 count) {
  kf_memset(j, 0, sizeof *j);
  j->n = n, j->count = count, j->last = -2;
  for (u32 i = 0; i < count; ++i) n[i].state = NS_PENDING, n[i].dt = 0;
}

u32 judge_multiplier(u32 streak) { return streak >= 30 ? 4 : 1 + streak / 10; }

int judge_press(Judge *j, u8 note, s32 t) {
  for (u32 i = j->first; i < j->count && j->n[i].t <= t + WIN_OK; ++i) {
    ChartNote *x = &j->n[i];
    if (x->state != NS_PENDING || x->note != note || x->t < t - WIN_OK) continue;
    const s32 dt = t - x->t, a = dt < 0 ? -dt : dt;
    x->dt = (s16)dt;
    x->state = a <= WIN_PERFECT ? NS_PERFECT : a <= WIN_GOOD ? NS_GOOD : NS_OK;
    ++j->streak;
    if (j->streak > j->maxStreak) j->maxStreak = j->streak;
    j->score += (x->state == NS_PERFECT ? 100 : x->state == NS_GOOD ? 70 : 40) * judge_multiplier(j->streak);
    if (x->state == NS_PERFECT) ++j->perfect;
    else if (x->state == NS_GOOD) ++j->good;
    else ++j->ok;
    j->sumDt += dt, ++j->nDt;
    j->last = x->state;
    while (j->first < j->count && j->n[j->first].state != NS_PENDING) ++j->first;
    return (int)i;
  }
  ++j->wrong;
  j->streak = 0;
  j->last = -1;
  return -1;
}

int judge_pass(Judge *j, s32 t) {
  int missed = 0;
  for (u32 i = j->first; i < j->count && j->n[i].t < t - WIN_OK; ++i)
    if (j->n[i].state == NS_PENDING) {
      j->n[i].state = NS_MISSED;
      ++j->miss, ++missed;
      j->streak = 0;
      j->last = NS_MISSED;
    }
  while (j->first < j->count && j->n[j->first].state != NS_PENDING) ++j->first;
  return missed;
}

u32 judge_accuracy(const Judge *j) {
  if (!j->count) return 0;
  return (u32)(((u64)j->perfect * 1000 + (u64)j->good * 700 + (u64)j->ok * 400) / j->count);
}

char judge_grade(const Judge *j) {
  const u32 a = judge_accuracy(j);
  return a >= 950 ? 'S' : a >= 850 ? 'A' : a >= 700 ? 'B' : a >= 500 ? 'C' : 'D';
}
