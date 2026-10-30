/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * menu.c -- the game's screens: the songs (the folder's MIDI files) with the choices for the one selected, a game
 * (play.c), its results. The soft keys beside the LCD are the game's (their labels in the ink layer, draw.c), and EXIT;
 * KN7APP takes only those from the firmware (pfhost.h panelTake), so the rest of the panel, and the keybed, stay the
 * instrument's: its sounds can be chosen for the keybed while the game runs. The TEMPO/PROGRAM wheel moves through the
 * songs (KN7APP keeps its detents from the firmware's tempo). EXIT held 1.5 s ends the program (KN7APP).
 *   the songs:   L1 PART (the part played, or both hands), L2 TEMPO (100% to 50%), L3 ACCOMP (on, off), L4 MODE (PLAY,
 *                WAIT, DEMO), L5 SPEED (how fast the notes fall); R1 PLAY, R2 UP, R3 DOWN, R5 LATENCY
 *   a game:      EXIT stops it
 *   the results: R1 AGAIN, L1 or EXIT the songs */
#include "draw.h"
#include "play.h"

extern const PfHost *host;
extern Judge play_judge;

enum { K_L1, K_L2, K_L3, K_L4, K_L5, K_R1, K_R2, K_R3, K_R4, K_R5, K_EXIT, KEYS };
static const u32 keyBit[KEYS] = {BTN_LCDL1, BTN_LCDL2, BTN_LCDL3, BTN_LCDL4, BTN_LCDL5, BTN_LCDR1,
                                 BTN_LCDR2, BTN_LCDR3, BTN_LCDR4, BTN_LCDR5, BTN_EXIT};
static int keyWas[KEYS];
static s32 wheelWas;

static void take_keys(void) {
  u8 mask[0x40];
  kf_memset(mask, 0, sizeof mask);
  for (int k = 0; k < KEYS; ++k) mask[keyBit[k] >> 8] |= (u8)(keyBit[k] & 0xFF);
  host->panelTake(mask);
}

/* the keys pressed since the last call (a bit each), and the wheel's detents */
static u32 pressed(s32 *wheel) {
  host->panelScan();
  u32 p = 0;
  for (int k = 0; k < KEYS; ++k) {
    const int d = (host->panel[keyBit[k] >> 8] & (keyBit[k] & 0xFF)) != 0;
    if (d && !keyWas[k]) p |= 1u << k;
    keyWas[k] = d;
  }
  const s32 w = *host->wheel;
  *wheel = w - wheelWas;
  wheelWas = w;
  return p;
}

static void keys_now(void) {  /* (the keys as they are: what is held now is no press later) */
  s32 w;
  pressed(&w);
}

/* ---- the songs ---- */
#define MAX_SONGS 100
static PfFile files[MAX_SONGS];
static int nSongs;
volatile s32 menu_sel = -1;  /* (tests/mame/keyfall_test.lua reads these) */
volatile u32 menu_screen;    /* 1 the songs, 2 a game, 3 the results */
static int first;
static Song song;
static int songOk = -1, songOf = -1;
static const char *songWhy = "";
static u32 songMark;

static u32 choice[KF_MAX_PARTS + 1];
static int nChoice;
volatile int opt_choice, opt_tempo = 100, opt_accomp = 1, opt_mode;
volatile u32 opt_parts;

static int is_mid(const char *n) {
  const int l = kf_strlen(n);
  return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 'm' && (n[l - 2] | 32) == 'i' && (n[l - 1] | 32) == 'd';
}

static void list_songs(void) {
  static PfFile all[MAX_SONGS + 40];
  const s32 n = host->list(all, MAX_SONGS + 40);
  nSongs = 0;
  for (s32 i = 0; i < n && nSongs < MAX_SONGS; ++i)
    if (is_mid(all[i].name)) files[nSongs++] = all[i];
  for (int i = 1; i < nSongs; ++i)  /* by name */
    for (int j = i; j > 0; --j) {
      const char *a = files[j - 1].name, *b = files[j].name;
      int k = 0;
      while (a[k] && a[k] == b[k]) ++k;
      if ((u8)a[k] <= (u8)b[k]) break;
      const PfFile t = files[j];
      files[j] = files[j - 1];
      files[j - 1] = t;
    }
  kf_log("songs: %d MIDI files of %d in the folder", nSongs, n);
}

/* the song selected read and its choices made; -> 0, or < 0 (songWhy says why) */
static int load_song(int i) {
  if (songOf == i) return songOk;
  kf_release(songMark);
  songOf = i;
  songOk = -1;
  const u32 bytes = files[i].bytes;
  u8 *buf = bytes && bytes < 0x30000 ? (u8 *)kf_alloc(bytes + 4) : 0;
  if (!buf) return songWhy = bytes ? "too large" : "empty", -1;
  PfPiece piece = {buf, bytes + 1};
  const s32 got = host->load(files[i].name, &piece, 1);
  if (got != (s32)bytes) return songWhy = "cannot be read", -1;
  songOk = smf_load(&song, buf, bytes, &songWhy);
  if (songOk == 0 && !song.title[0]) kf_strcpy(song.title, files[i].name, sizeof song.title);
  nChoice = songOk == 0 ? song_part_choices(&song, choice, KF_MAX_PARTS + 1) : 0;
  if (songOk == 0 && !nChoice) songOk = -1, songWhy = "no part to play";
  opt_choice = 0;
  const u32 def = songOk == 0 ? song_default_parts(&song) : 0;
  for (int k = 0; k < nChoice; ++k)
    if (choice[k] == def) opt_choice = k;
  opt_parts = nChoice ? choice[opt_choice] : 0;
  kf_log("song %s: %d (%s), \"%s\", %u parts, %u notes, %u ms", files[i].name, songOk, songWhy, song.title, song.nParts,
         song.nNotes, song.lengthMs);
  return songOk;
}

static const char *const MODE_NAME[MODES] = {"PLAY", "WAIT", "DEMO"};
static const u32 SPEEDS[] = {70, 90, 110, 140, 180};

static void line_at(int y, const char *label, const char *value, u8 col) {
  fill8(312, y, 268, 13, C_MENU);
  text8(label, 312, y, C_GREY, C_MENU);
  text8(value, 312 + 8 * 8, y, col, C_MENU);
}

static void draw_details(void) {
  fill8(304, 20, 280, 196, C_MENU);
  char b[64];
  if (menu_sel < 0) return;
  if (songOk < 0) {
    text8(files[menu_sel].name, 312, 24, C_WHITE, C_MENU);
    text8("This file cannot be played:", 312, 44, C_RED, C_MENU);
    text8(songWhy, 312, 60, C_RED, C_MENU);
    return;
  }
  text8(song.title, 312, 24, C_YELLOW, C_MENU);
  kf_fmt(b, sizeof b, "%s, %u:%02u, %u parts", files[menu_sel].name, song.lengthMs / 60000, song.lengthMs / 1000 % 60,
         song.nParts);
  text8(b, 312, 40, C_GREY, C_MENU);
  song_choice_name(&song, opt_parts, b, 34);
  line_at(68, "PART", b, C_WHITE);
  kf_fmt(b, sizeof b, "%d%%", opt_tempo);
  line_at(92, "TEMPO", b, C_WHITE);
  int others = 0;
  for (u32 i = 0; i < song.nParts; ++i) others += !((opt_parts >> i) & 1);
  line_at(116, "ACCOMP", !others ? "(none)" : opt_accomp ? "ON" : "OFF", C_WHITE);
  line_at(140, "MODE", MODE_NAME[opt_mode], C_WHITE);
  kf_fmt(b, sizeof b, "%u", kf_speed);
  line_at(164, "SPEED", b, C_WHITE);
  u32 acc;
  const u32 best = scores_best(files[menu_sel].name, opt_parts, (u32)opt_tempo, &acc);
  if (best) kf_fmt(b, sizeof b, "%u  (%u.%u%%)", best, acc / 10, acc % 10);
  else kf_fmt(b, sizeof b, "-");
  line_at(188, "BEST", b, C_GREEN);
  kf_fmt(b, sizeof b, "%d ms", kf_latency);
  line_at(204, "LATENCY", b, C_DIM);
}

#define LIST_ROWS 13
static void draw_list(void) {
  fill8(LABEL_W + 4, 20, 240, 196, C_MENU);
  for (int i = 0; i < LIST_ROWS && first + i < nSongs; ++i) {
    char b[40];
    kf_fmt(b, sizeof b, " %-12s ", files[first + i].name);
    const int on = first + i == menu_sel;
    fill8(LABEL_W + 4, 22 + 15 * i, 240, 15, on ? C_SEL : C_MENU);
    text8(b, LABEL_W + 8, 23 + 15 * i, on ? C_WHITE : C_GREY, on ? C_SEL : C_MENU);
  }
  if (!nSongs) {
    text8("No songs (.MID files)", LABEL_W + 8, 40, C_RED, C_MENU);
    text8("in the KEYFALL folder.", LABEL_W + 8, 56, C_RED, C_MENU);
  }
}

static const char *const songsLeft[5] = {"PART", "TEMPO", "ACCOMP", "MODE", "SPEED"};
static const char *const songsRight[5] = {"PLAY", "UP", "DOWN", "", "LATENCY"};

static void draw_songs(void) {
  fill8(0, 0, FB_W, FB_H, C_BLACK);
  fill8(0, 0, FB_W, 16, C_HUD);
  text8("KEYFALL", LABEL_W + 8, 2, C_YELLOW, C_HUD);
  text8("notes fall: play them on the keybed", LABEL_W + 80, 2, C_GREY, C_HUD);
  fill8(LABEL_W + 4, 20, 240, 196, C_MENU);
  draw_list();
  draw_details();
  ink_clear();
  ink_labels(songsLeft, songsRight);
  ink_line("EXIT held: leave Keyfall");
}

/* ---- the results ---- */
static const char *const resLeft[5] = {"SONGS", "", "", "", ""};
static const char *const resRight[5] = {"AGAIN", "", "", "", ""};

static void draw_results(const Judge *j, int aborted, int newBest) {
  fill8(0, 0, FB_W, FB_H, C_BLACK);
  fill8(0, 0, FB_W, 16, C_HUD);
  char b[64];
  kf_fmt(b, sizeof b, "%s - %s", song.title, MODE_NAME[opt_mode]);
  text8(b, LABEL_W + 8, 2, C_YELLOW, C_HUD);
  const u32 acc = judge_accuracy(j);
  int y = 28;
  if (opt_mode == MODE_DEMO) {
    text8("DEMO: the song played by itself.", LABEL_W + 16, y, C_WHITE, C_BLACK);
  } else if (opt_mode == MODE_WAIT) {
    kf_fmt(b, sizeof b, "%u of %u notes played, %u wrong keys", j->perfect + j->good + j->ok, j->count, j->wrong);
    text8(b, LABEL_W + 16, y, C_WHITE, C_BLACK);
  } else {
    char g[2] = {judge_grade(j), 0};
    text8s(g, LABEL_W + 24, y, g[0] == 'S' ? C_YELLOW : g[0] == 'A' ? C_GREEN : g[0] == 'D' ? C_RED : C_CYAN, C_BLACK, 6);
    const int x = LABEL_W + 104;
    kf_fmt(b, sizeof b, "SCORE     %u%s", j->score, newBest ? "   NEW BEST!" : "");
    text8(b, x, y, newBest ? C_YELLOW : C_WHITE, C_BLACK);
    kf_fmt(b, sizeof b, "ACCURACY  %u.%u%%", acc / 10, acc % 10);
    text8(b, x, y + 18, C_WHITE, C_BLACK);
    kf_fmt(b, sizeof b, "PERFECT   %u", j->perfect);
    text8(b, x, y + 42, C_YELLOW, C_BLACK);
    kf_fmt(b, sizeof b, "GOOD      %u", j->good);
    text8(b, x, y + 58, C_GREEN, C_BLACK);
    kf_fmt(b, sizeof b, "OK        %u", j->ok);
    text8(b, x, y + 74, C_CYAN, C_BLACK);
    kf_fmt(b, sizeof b, "MISSED    %u", j->miss);
    text8(b, x, y + 90, C_RED, C_BLACK);
    kf_fmt(b, sizeof b, "WRONG KEY %u", j->wrong);
    text8(b, x, y + 106, C_RED, C_BLACK);
    kf_fmt(b, sizeof b, "STREAK    %u (longest)", j->maxStreak);
    text8(b, x, y + 130, C_WHITE, C_BLACK);
    if (j->nDt) {
      const s32 m = j->sumDt / (s32)j->nDt;
      kf_fmt(b, sizeof b, "TIMING    %d ms %s on average", m < 0 ? -m : m, m < 0 ? "early" : "late");
      text8(b, x, y + 146, C_GREY, C_BLACK);
    }
  }
  if (aborted) text8("(stopped with EXIT)", LABEL_W + 16, 200, C_DIM, C_BLACK);
  ink_clear();
  ink_labels(resLeft, resRight);
}

/* ---- the game ---- */
static void play_once(void) {
  int again;
  do {
    again = 0;
    const u32 mark = kf_mark();
    Chart c;
    if (chart_build(&c, &song, opt_parts, (u32)opt_tempo, opt_accomp, opt_mode) < 0) {
      kf_release(mark);
      ink_line("Not enough memory for this song");
      return;
    }
    menu_screen = 2;
    fill8(0, 0, FB_W, FB_H, C_BLACK);
    char name[40];
    song_choice_name(&song, opt_parts, name, sizeof name);
    kf_log("game: %s, %s, tempo %d%%, accompaniment %d, mode %s", files[menu_sel].name, name, opt_tempo, opt_accomp,
           MODE_NAME[opt_mode]);
    host->diagSave();
    const int aborted = play_run(&c, opt_mode, song.title, &play_judge);
    int newBest = 0;
    if (!aborted && opt_mode == MODE_PLAY)
      newBest = scores_offer(files[menu_sel].name, opt_parts, (u32)opt_tempo, play_judge.score, judge_accuracy(&play_judge));
    host->diagSave();
    kf_release(mark);
    menu_screen = 3;
    draw_results(&play_judge, aborted, newBest);
    keys_now();
    for (;;) {
      s32 w;
      const u32 p = pressed(&w);
      if (p & (1u << K_R1)) {
        again = 1;
        break;
      }
      if (p & (1u << K_L1 | 1u << K_EXIT)) break;
      host->yield(20);
    }
  } while (again);
}

int kf_main(void) {
  take_keys();
  host->status("KEYFALL: HOLD EXIT TO QUIT");
  lcd_own();
  fill8(0, 0, FB_W, FB_H, C_BLACK);
  text8("KEYFALL", 292, 100, C_YELLOW, C_BLACK);
  text8("Reading the songs...", 240, 120, C_GREY, C_BLACK);
  list_songs();
  scores_load();
  host->diagSave();
  songMark = kf_mark();
  menu_sel = nSongs ? 0 : -1;
  first = 0;
  if (menu_sel >= 0) load_song(menu_sel);
  menu_screen = 1;
  draw_songs();
  keys_now();
  u32 movedAt = 0;
  int pending = 0;
  for (;;) {
    s32 w;
    const u32 p = pressed(&w);
    int move = (int)w + ((p >> K_R3) & 1) - ((p >> K_R2) & 1);
    if (move && nSongs) {
      int s = menu_sel + move;
      if (s < 0) s = 0;
      if (s >= nSongs) s = nSongs - 1;
      if (s != menu_sel) {
        menu_sel = s;
        if (menu_sel < first) first = menu_sel;
        if (menu_sel >= first + LIST_ROWS) first = menu_sel - LIST_ROWS + 1;
        draw_list();
        fill8(304, 20, 280, 196, C_MENU);
        pending = 1;
        movedAt = host->ms();
      }
    }
    if (pending && host->ms() - movedAt >= 250) {  /* (read once the selection rests) */
      pending = 0;
      load_song(menu_sel);
      draw_details();
    }
    if (menu_sel >= 0 && !pending && songOk == 0) {
      int changed = 1;
      if (p & (1u << K_L1)) opt_choice = (opt_choice + 1) % nChoice, opt_parts = choice[opt_choice];
      else if (p & (1u << K_L2)) opt_tempo = opt_tempo <= 50 ? 100 : opt_tempo - 10;
      else if (p & (1u << K_L3)) opt_accomp = !opt_accomp;
      else if (p & (1u << K_L4)) opt_mode = (opt_mode + 1) % MODES;
      else if (p & (1u << K_L5)) {
        u32 i = 0;
        while (i < sizeof SPEEDS / sizeof *SPEEDS && SPEEDS[i] != kf_speed) ++i;
        kf_speed = SPEEDS[(i + 1) % (sizeof SPEEDS / sizeof *SPEEDS)];
      } else if (p & (1u << K_R5)) kf_latency = kf_latency >= 100 ? -40 : kf_latency + 10;
      else changed = 0;
      if (changed) draw_details();
      if (p & (1u << K_R1)) {
        play_once();
        menu_screen = 1;
        draw_songs();
        keys_now();
      }
    }
    host->yield(20);
  }
  lcd_release();
  return 0;
}
