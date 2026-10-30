/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * kf.h -- Keyfall, the piano game: what its files share. The song reading (smf.c), the chart (song.c) and the judging
 * (judge.c) are plain C that the PC builds too (tests/host/, KF_HOST); the rest runs on the KN7000 under KN7APP.BIN
 * (src/app/, pfhost.h). */
#ifndef KF_H
#define KF_H
#include <stdarg.h>
#include <stddef.h>

#include "pfhost.h"

typedef int16_t s16;
typedef int8_t s8;
typedef uint64_t u64;

/* ---- memory: one region (ARENA2), handed out from its start and given back all at once (main.c) ---- */
void *kf_alloc(u32 bytes);  /* 0 when it does not fit; 4-byte aligned, zeroed */
u32 kf_mark(void);          /* where the next allocation goes ... */
void kf_release(u32 mark);  /* ... and everything allocated since given back */

/* ---- text (fmt.c): %d %u %x %X %s %c, with a width, 0 and - ---- */
int kf_vfmt(char *out, int room, const char *fmt, va_list ap);
int kf_fmt(char *out, int room, const char *fmt, ...);
void kf_log(const char *fmt, ...);  /* a line into KN7APP's log (PfHost::diag); on the PC to stdout */
int kf_strlen(const char *s);
void kf_strcpy(char *d, const char *s, int room);
int kf_streq_ci(const char *a, const char *b);
int kf_contains_ci(const char *hay, const char *needle);
void kf_memset(void *d, int c, u32 n);
void kf_memcpy(void *d, const void *s, u32 n);

/* ---- a Standard MIDI File (smf.c) ---- */
#define KF_MAX_TRACKS 24
#define KF_MAX_PARTS 24

/* a channel event, every track merged in time order: its time in ms (the song at its own tempo) */
typedef struct MEv {
  u32 ms;
  u32 tick;
  u8 st, d1, d2, trk;  /* status (with its channel), data, data, the track it came from */
} MEv;

/* a note: from its note-on to its note-off */
typedef struct Note {
  u32 ms, dur;  /* start and length in ms */
  u8 note, vel, part, pad;
} Note;

/* a part: the notes of one track on one channel (format 0: a channel), what the player chooses to play */
typedef struct Part {
  char name[24];
  u8 trk, ch, lo, hi;  /* its track, channel (0-15), lowest and highest note */
  u16 notes;
  u8 program;          /* its first program change, or 0 */
  u8 drums;            /* channel 10 */
} Part;

typedef struct Song {
  char title[32];
  u16 format, tracks, ppq;
  u32 nEv;
  MEv *ev;
  u32 nNotes;
  Note *notes;  /* sorted by start */
  u32 nParts;
  Part part[KF_MAX_PARTS];
  u32 nBeats;
  u32 *beat;    /* each beat's time in ms; bit 31 set on a bar's first beat */
  u32 lengthMs; /* the last note's end */
} Song;

/* -> 0, or < 0 with *why saying what is wrong */
int smf_load(Song *s, const u8 *data, u32 bytes, const char **why);
u32 smf_ms_of(const Song *s, u32 tick);  /* (smf.c, while loading) */

/* ---- the chart: what one game plays (song.c) ---- */
enum { NS_PENDING, NS_PERFECT, NS_GOOD, NS_OK, NS_MISSED };
typedef struct ChartNote {
  s32 t;          /* when to strike it, in ms of the game's clock (the tempo applied) */
  u32 dur;        /* how long it lasts */
  u8 note, hand;  /* hand: 0 the first part chosen, 1 the second */
  u8 state;       /* NS_* */
  u8 drawn;       /* (play.c) */
  s16 dt;         /* when struck: the press's time minus t */
  s16 y0, y1;     /* (play.c: the rows drawn) */
  u16 next;       /* (play.c: the next visible note in its lane) */
  u16 pad;
} ChartNote;

/* a MIDI message the game sends at a time (the accompaniment), its channel already the instrument's */
typedef struct OutEv {
  s32 t;
  u8 m[3], n;
} OutEv;

enum { MODE_PLAY, MODE_WAIT, MODE_DEMO, MODES };

typedef struct Chart {
  u32 n;
  ChartNote *note;
  u32 nOut;
  OutEv *out;
  u32 nBeat;
  s32 *beat;      /* ms; bit 30 set on a bar's first beat (s32: times before the start are negative) */
  s32 length;     /* the last note's end */
  u8 lo, hi;      /* the keys shown */
  u8 outCh;       /* channels the accompaniment uses (count) */
  u8 pad;
} Chart;

/* The game's chart from a song: the parts in `parts` (a bit each) for the player, at tempo pct (100 the song's own),
 * the other parts as accompaniment when `accomp`, the player's too in MODE_DEMO. -> 0, or < 0 (memory). */
int chart_build(Chart *c, const Song *s, u32 parts, u32 pct, int accomp, int mode);
s32 chart_start(void);                        /* the game's clock at the start: a lead-in, negative */
u32 song_default_parts(const Song *s);       /* the part the game offers first */
int song_part_choices(const Song *s, u32 *choice, int room);  /* the choices offered: each part, both hands */
void song_choice_name(const Song *s, u32 parts, char *out, int room);

/* ---- judging (judge.c) ---- */
#define WIN_PERFECT 50
#define WIN_GOOD 100
#define WIN_OK 150
typedef struct Judge {
  ChartNote *n;
  u32 count, first;  /* first: the first note not yet judged */
  u32 score, streak, maxStreak;
  u32 perfect, good, ok, miss, wrong;
  s32 sumDt;
  u32 nDt;
  int last;          /* the last judgement (NS_*), or -1 for a wrong key */
} Judge;
void judge_init(Judge *j, ChartNote *n, u32 count);
int judge_press(Judge *j, u8 note, s32 t);  /* -> the note struck, or -1 (a wrong key) */
int judge_pass(Judge *j, s32 t);             /* the notes too late to strike at t: missed -> how many */
u32 judge_accuracy(const Judge *j);          /* in tenths of a per cent */
char judge_grade(const Judge *j);
u32 judge_multiplier(u32 streak);

#endif
