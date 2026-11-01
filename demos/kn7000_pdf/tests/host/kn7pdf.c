/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * kn7pdf.c -- the viewer's drawing (src/kn7/view.c) on the PC, 32-bit as the KN7000: each page of a PDF drawn as the
 * instrument draws it, with MuPDF's memory counted. Prints, per page, its rows, the heap in use at the peak while it was
 * drawn and the time taken; with an output directory, writes each page as a PGM (<dir>/<n>.pgm, 1 first).
 *   kn7pdf FILE.PDF [WIDTH=640] [BAND_ROWS=32] [STORE_KB=64] [OUT_DIR] [HEAP_KB=0: no limit]
 * With HEAP_KB, an allocation that would take the heap in use over it fails, as it would on the instrument.
 * With KN7PDF_SITES=1 in the environment, the bytes in use at the run's highest peak (with HEAP_KB: at the first
 * allocation refused; with KN7PDF_SITES_END=1 too: after the last page) are printed by where they were allocated
 * (return addresses: addr2line -f -e kn7pdf ADDR names them). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <execinfo.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fitz-internal.h"
#include "readcache.h"
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
  if (n > (1u << 20) && getenv("KN7PDF_BIG")) {  /* (where a request over 1 MB comes from: addr2line names these) */
    void* bt[8];
    const int k = backtrace(bt, 8);
    fprintf(stderr, "big allocation %u:", n);
    for (int i = 0; i < k; ++i) fprintf(stderr, " %p", bt[i]);
    fprintf(stderr, "\n");
  }
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
/* fz_kn7_slow_try (the Type 3 glyph procedures kept): only while the slow memory in use stays within the viewer's
 * share of the display heap for fonts (viewer.c FONT_BUDGET, 128 KB; KN7PDF_SLOW_KB) */
static size_t slow_limit = 128 << 10;
static void* slow_try(unsigned n) { return slow_in_use + n <= slow_limit ? slow_malloc(n) : NULL; }
static void slow_free(void* q) {
  if (!q) return;
  size_t* p = (size_t*)((char*)q - 8);
  slow_in_use -= *p;
  free(p);
}

static unsigned refused(void) { return (unsigned)failed; }

/* the file's reads (linked with --wrap=read): on the KN7000 each one is a request to the card, which costs far more
 * than the drawing a glyph needs; and MuPDF's Type 3 glyph procedures loaded and glyph cache (KN7PDF counters) */
static unsigned reads, read_kb_bytes, read_ms;
static void card_cost(unsigned off, unsigned n) {  /* the unit's: 5 ms a request, 3.9 ms a 512-byte sector touched */
  ++reads, read_kb_bytes += n;
  read_ms += 5 + (unsigned)(3.9 * (((off + n + 511) / 512) - off / 512));
}
ssize_t __real_read(int fd, void* b, size_t n);
static int fetch(void* arg, uint32_t off, void* dst, uint32_t n) {  /* the read cache's (KN7PDF_RC_KB) */
  const int fd = (int)(intptr_t)arg;
  const ssize_t r = pread(fd, dst, n, off);
  if (r > 0) card_cost(off, (unsigned)r);
  return (int)r;
}
static int rc_on;
ssize_t __wrap_read(int fd, void* b, size_t n) {
  if (fd <= 2) return __real_read(fd, b, n);
  const off_t at = lseek(fd, 0, SEEK_CUR);
  if (rc_on) {
    struct stat st;
    fstat(fd, &st);
    const int r = rc_read(fd, (uint32_t)st.st_size, (uint32_t)at, b, (uint32_t)n, fetch, (void*)(intptr_t)fd);
    if (r > 0) lseek(fd, at + r, SEEK_SET);
    return r;
  }
  const ssize_t r = __real_read(fd, b, n);
  if (r > 0) card_cost((unsigned)at, (unsigned)r);
  return r;
}
extern int fz_kn7_file_read_max;
extern int fz_kn7_t3_loads, fz_kn7_t3_bytes, fz_kn7_glyph_misses, fz_kn7_glyph_evictions;

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
  view_failures = refused;
  if (getenv("KN7PDF_AA")) view_aa_bits = atoi(getenv("KN7PDF_AA"));
  fz_kn7_slow_malloc = slow_malloc;
  fz_kn7_slow_free = slow_free;
  fz_kn7_slow_try = slow_try;
  if (getenv("KN7PDF_SLOW_KB")) slow_limit = (size_t)atoi(getenv("KN7PDF_SLOW_KB")) << 10;
  if (getenv("KN7PDF_READ_MAX")) fz_kn7_file_read_max = atoi(getenv("KN7PDF_READ_MAX"));
  static unsigned char rc_mem[RC_MAX_BLOCKS * RC_BLOCK];
  if (getenv("KN7PDF_RC_KB")) rc_on = 1, rc_init(rc_mem, (uint32_t)atoi(getenv("KN7PDF_RC_KB")) << 10);
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
  int errors = 0, incomplete = 0;
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
    reads = read_kb_bytes = read_ms = 0;
    fz_kn7_t3_loads = fz_kn7_t3_bytes = fz_kn7_glyph_misses = fz_kn7_glyph_evictions = 0;
    const unsigned t0 = ms();
    const int r = view_draw(p, width, h, band, band_out, NULL, &st);
    const unsigned t = ms() - t0;
    if (r < 0) {
      printf("page %d: %s\n", p + 1, view_error());
      ++errors;
    }
    printf("page %2d: %dx%d, heap peak %4zu KB, after %4zu KB; %u ms (load %u, lists %u: %u, bands %u: %u%s)%s\n", p + 1,
           width, h, peak / 1024, in_use / 1024, t, st.load_ms, st.lists, st.list_ms, st.draw_ms, st.bands,
           st.direct ? ", regions" : "", st.incomplete ? " INCOMPLETE" : "");
    printf("         reads %u (%u KB, %u ms on the unit); glyphs drawn %d, cache emptied %d; Type 3 procedures loaded %d (%d KB)\n",
           reads, read_kb_bytes / 1024, read_ms, fz_kn7_glyph_misses, fz_kn7_glyph_evictions, fz_kn7_t3_loads,
           fz_kn7_t3_bytes / 1024);
    incomplete += st.incomplete;
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
  printf("worst heap peak %zu KB; %d page errors, %d incomplete; %zu allocations refused; slow memory peak %zu KB\n",
         worst / 1024, errors, incomplete, failed, slow_peak / 1024);
  if (sites_on && getenv("KN7PDF_SITES_END")) {  /* what stays in use after the last page, instead */
    memcpy(site_at_peak, site_live, sizeof site_live);
    top_peak = in_use;
  }
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
  return errors || incomplete ? 1 : 0;
}
