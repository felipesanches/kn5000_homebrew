/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * kn7pdf.h -- what the viewer's KN7000 side (src/kn7/) shares: the platform KN7APP.BIN hands over, the card's files,
 * the memory, the log. */
#pragma once
#include "pfhost.h"

extern const PfHost *host;

/* main.c: a file of the program's folder (PDF\) for reading anywhere in it. kn7FileOpen takes the name after the last
 * '/' or '\' of path -> 0, or -1 (errno ENOENT, EIO or ENOMEM). kn7FileRead -> the bytes read, or -1. */
int kn7FileOpen(const char *path, PfRawFile *f);
int kn7FileRead(const PfRawFile *f, unsigned offset, void *dst, unsigned bytes);
void kn7FileClose(PfRawFile *f);

/* main.c: the heap (TLSF over every region the program does not take itself): its size, what is in use, the most that
 * was, and the largest block it could give now */
typedef struct HeapStats {
  unsigned total, used, peak, largest, regions;
} HeapStats;
void kn7HeapStats(HeapStats *s);
unsigned kn7HeapFailures(void);  /* allocations refused so far (view.h view_failures) */

/* main.c: the log (what MuPDF and the viewer print), on the LCD when shown */
void kn7LogShow(int on);
