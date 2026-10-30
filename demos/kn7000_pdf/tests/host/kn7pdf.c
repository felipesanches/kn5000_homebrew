/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * kn7pdf.c -- the viewer's drawing (src/kn7/view.c) on the PC, 32-bit as the KN7000: each page of a PDF drawn as the
 * instrument draws it, with MuPDF's memory counted. Prints, per page, its rows, the heap in use at the peak while it was
 * drawn and the time taken; with an output directory, writes each page as a PGM (<dir>/<n>.pgm, 1 first).
 *   kn7pdf FILE.PDF [WIDTH=640] [BAND_ROWS=32] [STORE_KB=64] [OUT_DIR] [HEAP_KB=0: no limit]
 * With HEAP_KB, an allocation that would take the heap in use over it fails, as it would on the instrument.
 * With KN7PDF_SITES=1 in the environment, the bytes in use at the run's highest peak are printed by where they were
 * allocated (return addresses: addr2line -f -e kn7pdf ADDR names them). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <execinfo.h>
#include "fitz-internal.h"
#include "view.h"

static size_t in_use, peak, limit, failed;

/* allocation sites (KN7PDF_SITES): each block's header holds its size and its site; live bytes per site */
#define SITES 4096
#define SITE_DEPTH 5
static int sites_on;
static void* site_pc[SITES][SITE_DEPTH];
static size_t site_live[SITES], site_at_peak[SITES], top_peak;
static int refused_seen;  /* with HEAP_KB: the sites are those in use at the first refused allocation, not the peak */

static unsigned site_of(void) {
  void* bt[SITE_DEPTH + 3];
  const int n = backtrace(bt, SITE_DEPTH + 3);
  unsigned h = 0;
  for (int i = 3; i < n; ++i) h = h * 31 + (unsigned)(size_t)bt[i];
  unsigned s = h % SITES;
  for (unsigned k = 0; k < SITES; ++k, s = (s + 1) % SITES) {
    if (!site_pc[s][0]) {
      for (int i = 3; i < n; ++i) site_pc[s][i - 3] = bt[i];
      return s;
    }
    if (site_pc[s][0] == bt[3] && (n < 5 || site_pc[s][1] == bt[4]) && (n < 6 || site_pc[s][2] == bt[5]) &&
        (n < 7 || site_pc[s][3] == bt[6]) && (n < 8 || site_pc[s][4] == bt[7]))
      return s;
  }
  return 0;
}

static void site_add(size_t* p, long n) {
  if (!sites_on) return;
  site_live[p[1]] += n;
  if (in_use > top_peak && !refused_seen) {
    top_peak = in_use;
    memcpy(site_at_peak, site_live, sizeof site_live);
  }
}

static void* count_malloc(void* user, unsigned n) {
  (void)user;
  if (limit && in_use + n > limit) {
    if (sites_on && !refused_seen++) {
      memcpy(site_at_peak, site_live, sizeof site_live);
      top_peak = in_use;
      fprintf(stderr, "first refusal: %u bytes with %zu in use\n", n, in_use);
    }
    ++failed;
    return NULL;
  }
  size_t* p = malloc(n + 8);
  if (!p) return NULL;
  *p = n;
  p[1] = sites_on ? site_of() : 0;
  in_use += n;
  if (in_use > peak) peak = in_use;
  site_add(p, (long)n);
  return (char*)p + 8;
}

static void count_free(void* user, void* q) {
  (void)user;
  if (!q) return;
  size_t* p = (size_t*)((char*)q - 8);
  in_use -= *p;
  site_add(p, -(long)*p);
  free(p);
}

static void* count_realloc(void* user, void* q, unsigned n) {
  if (!q) return count_malloc(user, n);
  size_t* p = (size_t*)((char*)q - 8);
  if (limit && in_use - *p + n > limit) {
    ++failed;
    return NULL;
  }
  const size_t old = *p;
  size_t* r = realloc(p, n + 8);
  if (!r) return NULL;
  in_use = in_use - old + n;
  *r = n;
  if (in_use > peak) peak = in_use;
  site_add(r, (long)n - (long)old);
  return (char*)r + 8;
}

static fz_alloc_context counting = {NULL, count_malloc, count_realloc, count_free};

/* the slow memory (display memory on the KN7000: fz_kn7_slow_malloc), counted apart from the heap and not limited */
static size_t slow_in_use, slow_peak;
static void* slow_malloc(unsigned n) {
  size_t* p = malloc(n + 8);
  if (!p) return NULL;
  *p = n;
  slow_in_use += n;
  if (slow_in_use > slow_peak) slow_peak = slow_in_use;
  return (char*)p + 8;
}
static void slow_free(void* q) {
  if (!q) return;
  size_t* p = (size_t*)((char*)q - 8);
  slow_in_use -= *p;
  free(p);
}

static unsigned ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static unsigned char* page_buf;
static int page_w;

static void band_out(void* user, int y, int h, int w, const unsigned char* gray, int stride) {
  (void)user;
  for (int r = 0; r < h; ++r)
    for (int x = 0; x < w; ++x) page_buf[(size_t)(y + r) * w + x] = gray[r * stride + 2 * x];
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: kn7pdf FILE.PDF [WIDTH] [BAND_ROWS] [STORE_KB] [OUT_DIR] [HEAP_KB]\n");
    return 2;
  }
  const int width = argc > 2 ? atoi(argv[2]) : 640;
  const int band = argc > 3 ? atoi(argv[3]) : 32;
  const unsigned store = (argc > 4 ? atoi(argv[4]) : 64) * 1024u;
  const char* out = argc > 5 && strcmp(argv[5], "-") ? argv[5] : NULL;
  limit = argc > 6 ? (size_t)atoi(argv[6]) * 1024 : 0;
  view_clock = ms;
  fz_kn7_slow_malloc = slow_malloc;
  fz_kn7_slow_free = slow_free;
  sites_on = getenv("KN7PDF_SITES") != NULL;
  if (view_init(&counting, store) < 0) {
    printf("init: %s\n", view_error());
    return 1;
  }
  const size_t base = in_use;
  const int pages = view_open(argv[1]);
  if (pages < 0) {
    printf("open: %s (peak %zu KB)\n", view_error(), peak / 1024);
    return 1;
  }
  printf("%s: %d pages; context %zu KB, after open %zu KB (peak %zu KB)\n", argv[1], pages, base / 1024, in_use / 1024,
         peak / 1024);
  size_t worst = 0;
  int errors = 0;
  for (int p = 0; p < pages; ++p) {
    const int h = view_height(p, width);
    if (h <= 0) {
      printf("page %d: %s\n", p + 1, view_error());
      ++errors;
      continue;
    }
    page_w = width;
    page_buf = malloc((size_t)width * h);
    memset(page_buf, 255, (size_t)width * h);
    peak = in_use;
    ViewStats st = {0};
    const unsigned t0 = ms();
    const int r = view_draw(p, width, h, band, band_out, NULL, &st);
    const unsigned t = ms() - t0;
    if (r < 0) {
      printf("page %d: %s\n", p + 1, view_error());
      ++errors;
    }
    printf("page %2d: %dx%d, heap peak %4zu KB, after %4zu KB; %u ms (load %u, list %u, bands %u)\n", p + 1, width, h,
           peak / 1024, in_use / 1024, t, st.load_ms, st.list_ms, st.draw_ms);
    if (peak > worst) worst = peak;
    if (out) {
      char name[512];
      snprintf(name, sizeof name, "%s/%d.pgm", out, p + 1);
      FILE* f = fopen(name, "wb");
      if (f) {
        fprintf(f, "P5\n%d %d\n255\n", width, h);
        fwrite(page_buf, 1, (size_t)width * h, f);
        fclose(f);
      }
    }
    free(page_buf);
  }
  printf("worst heap peak %zu KB; %d page errors; %zu allocations refused; slow memory peak %zu KB\n", worst / 1024,
         errors, failed, slow_peak / 1024);
  if (sites_on) {
    printf("in use at the highest peak (%zu KB), by site:\n", top_peak / 1024);
    for (int k = 0; k < 25; ++k) {
      int best = -1;
      for (int i = 0; i < SITES; ++i)
        if (site_at_peak[i] && (best < 0 || site_at_peak[i] > site_at_peak[best])) best = i;
      if (best < 0) break;
      printf("%8zu KB", site_at_peak[best] / 1024);
      for (int d = 0; d < SITE_DEPTH && site_pc[best][d]; ++d) printf(" %p", site_pc[best][d]);
      printf("\n");
      site_at_peak[best] = 0;
    }
  }
  view_close();
  return errors ? 1 : 0;
}
