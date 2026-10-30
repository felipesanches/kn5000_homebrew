/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * play.h -- one game (play.c) and the best scores (scores.c). */
#ifndef KF_PLAY_H
#define KF_PLAY_H
#include "kf.h"

extern u32 kf_speed;   /* lines a second the notes fall */
extern s32 kf_latency; /* ms taken off each key's time before it is judged */

/* the chart played in mode m (MODE_*) until its end (-> 0) or EXIT (-> 1); the judging's counts in *j */
int play_run(Chart *c, int m, const char *title, Judge *j);

void scores_load(void);
/* the best score for that song and choice, or 0 (accuracy in tenths into *acc) */
u32 scores_best(const char *file, u32 parts, u32 pct, u32 *acc);
/* a new score: 1 when it is the best (the file then saved) */
int scores_offer(const char *file, u32 parts, u32 pct, u32 score, u32 acc);

#endif
