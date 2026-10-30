/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * view.h -- the viewer's drawing, the same on the KN7000 and on the PC (tests/host/): a PDF opened with MuPDF 1.1, a
 * page drawn to a given width in gray, a band of rows at a time, each band handed to the caller. */
#ifndef KN7PDF_VIEW_H
#define KN7PDF_VIEW_H

typedef struct fz_alloc_context_s fz_alloc_context;

/* A band of the page drawn: rows [y, y + h) of it, `w` pixels each, 0 black to 255 white, `stride` bytes a row. */
typedef void (*view_band_fn)(void* user, int y, int h, int w, const unsigned char* gray, int stride);

typedef struct ViewStats {
  unsigned load_ms, list_ms, draw_ms;  /* (the caller's clock, view_clock) */
  unsigned bands, lists;
  int direct;      /* 1: the page's list did not fit: it was made a region at a time (`lists` of them) */
  int incomplete;  /* 1: memory ran short while the bands were drawn: something may be missing */
} ViewStats;

/* MuPDF's context, its allocations through `alloc`; `store` bytes for its cache of fonts, images and objects. 0, or -1
 * when there is not memory for it. */
int view_init(fz_alloc_context* alloc, unsigned store);
/* The document `path` opened (through the C library's open/read/lseek); its page count, or < 0 (view_error says
 * why). */
int view_open(const char* path);
void view_close(void);
/* Page `page` (0 first) drawn `width` pixels wide, its height as the page's shape makes it, at most max_h rows, bands of
 * `band_rows` rows handed to `out`. -> the page's height in rows, or < 0. */
int view_draw(int page, int width, int max_h, int band_rows, view_band_fn out, void* user, ViewStats* stats);
/* The page's height for a given width, without drawing it (-1 on an error). */
int view_height(int page, int width);
const char* view_error(void);
/* the caller's millisecond clock, for ViewStats (0 when there is none) */
extern unsigned (*view_clock)(void);
/* the allocations the platform's heap has refused so far (MuPDF had given back what it could first) */
extern unsigned (*view_failures)(void);

#endif
