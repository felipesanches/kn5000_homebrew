/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * kn7doom.h -- what the KN7000 side of Doom (src/kn7/) shares: the platform KN7APP.BIN hands over, the card's files,
 * the startup log. */
#pragma once
#include "pfhost.h"

extern const PfHost *host;

/* main.c: a file of the program's folder (DOOM\) for reading anywhere in it. kn7FileOpen takes the name after the last
 * '/' or '\' of path -> 0, or -1 (errno ENOENT, EIO or ENOMEM). kn7FileRead -> the bytes read, or -1. */
int kn7FileOpen(const char *path, PfRawFile *f);
int kn7FileRead(const PfRawFile *f, unsigned offset, void *dst, unsigned bytes);
void kn7FileClose(PfRawFile *f);

/* main.c: the startup log (what Doom prints) shown on the LCD until the game takes it (i_video.c), and after the end
 * when it ended on an error. */
void kn7LogShow(int on);

/* i_video.c: the LCD as the firmware had it (the end, an error) */
void kn7VideoRelease(void);

/* i_input.c: the panel and the keybed read, Doom's key events posted (I_StartTic) */
void kn7InputPoll(void);
