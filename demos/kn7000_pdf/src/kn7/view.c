/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * view.c -- the viewer's drawing (view.h) with MuPDF 1.1: the page is run once into a display list, which is then
 * drawn a band of rows at a time into a gray pixmap of that band's size, white first. MuPDF's draw device skips the
 * list's items that lie outside the band, so the page costs its content once and the bands little more, and the
 * memory a page needs is the list's and one band's, not a whole page's pixmap (640 x 906 gray with alpha: 1.1 MB). */
#include "fitz-internal.h"
#include <string.h>

#include "view.h"

unsigned (*view_clock)(void);
unsigned (*view_failures)(void);
int view_aa_bits = 4;
extern fz_context* fontfile_ctx;  /* (fontfile.c) */
void pdf_close_image_windows(fz_context* ctx);  /* (MuPDF's pdf_image.c and pdf_xref.c, KN7PDF) */
typedef struct pdf_document_s pdf_document;
void pdf_clear_xref(pdf_document* xref);

static fz_context* ctx;
static fz_document* doc;
static char error[96];

static unsigned clock_ms(void) { return view_clock ? view_clock() : 0; }
static unsigned failures(void) { return view_failures ? view_failures() : 0; }

static void set_error(const char* s) {
  int i = 0;
  for (; s[i] && i < (int)sizeof error - 1; ++i) error[i] = s[i];
  error[i] = 0;
}

const char* view_error(void) { return error; }

int view_init(fz_alloc_context* alloc, unsigned store) {
  ctx = fz_new_context(alloc, NULL, store);
  if (!ctx) {
    set_error("no memory for MuPDF's context");
    return -1;
  }
  fontfile_ctx = ctx;
  /* anti-aliasing of 4 bits (5 x 3 samples a pixel, not 17 x 15): the LCD shows 16 grays, and the edge list's work, a
   * sixth of a page's time on the KN7000 at 8 bits, goes down with the samples */
  fz_set_aa_level(ctx, view_aa_bits);
  return 0;
}

int view_open(const char* path) {
  int n = -1;
  view_close();
  fz_try(ctx) {
    doc = fz_open_document(ctx, (char*)path);
    n = fz_count_pages(doc);
  }
  fz_catch(ctx) {
    set_error(ctx->error->message);
    if (doc) fz_close_document(doc);
    doc = NULL;
    n = -1;
  }
  return n;
}

void view_close(void) {
  if (doc) fz_close_document(doc);
  doc = NULL;
}

/* the page's scale for a width, and its size in rows at that scale */
static fz_matrix page_ctm(fz_page* page, int width, fz_bbox* box) {
  const fz_rect r = fz_bound_page(doc, page);
  const float z = r.x1 > r.x0 ? width / (r.x1 - r.x0) : 1;
  fz_matrix ctm = fz_scale(z, z);
  fz_bbox b = fz_round_rect(fz_transform_rect(ctm, r));
  ctm.e -= b.x0, ctm.f -= b.y0;  /* (the page's corner at 0, 0) */
  b.x1 -= b.x0, b.y1 -= b.y0, b.x0 = b.y0 = 0;
  *box = b;
  return ctm;
}

int view_height(int page_no, int width) {
  fz_page* page = NULL;
  fz_bbox box = {0, 0, 0, 0};
  int h = -1;
  fz_var(page);
  fz_try(ctx) {
    page = fz_load_page(doc, page_no);
    page_ctm(page, width, &box);
    h = box.y1;
  }
  fz_catch(ctx) {
    set_error(ctx->error->message);
  }
  if (page) fz_free_page(doc, page);
  pdf_clear_xref((pdf_document*)doc);
  return h;
}

/* A band of `rows` rows from y: a pixmap (kept in *band while it is that tall), white, drawn by the list, then handed to
 * `out` (when there is one). */
static void draw_band(fz_display_list* list, fz_page* page, fz_matrix ctm, fz_bbox box, int y, int rows,
                      fz_pixmap** band, view_band_fn out, void* user) {
  fz_device* dev = NULL;
  const fz_bbox b = {0, y, box.x1, y + rows};
  if (!*band || fz_pixmap_height(ctx, *band) != rows) {
    if (*band) fz_drop_pixmap(ctx, *band);
    *band = NULL;
    *band = fz_new_pixmap_with_bbox(ctx, fz_device_gray, b);
  }
  (*band)->x = 0, (*band)->y = y;
  fz_clear_pixmap_with_value(ctx, *band, 255);
  fz_var(dev);
  fz_try(ctx) {
    dev = fz_new_draw_device(ctx, *band);
    fz_run_display_list(list, dev, fz_identity, b, NULL);
  }
  fz_always(ctx) {
    if (dev) fz_free_device(dev);
  }
  fz_catch(ctx) {
    fz_rethrow(ctx);
  }
  if (out) out(user, y, rows, box.x1, fz_pixmap_samples(ctx, *band), 2 * box.x1);
}

/* The page is run into a display list, which then draws it a band at a time. When memory ran short while the list was
 * made (an allocation the platform refused: view_failures), the list is incomplete: it goes, and the page is drawn a
 * region at a time instead, each region's list holding only what is drawn there (fz_kn7_list_keep), the region halved
 * until its list fits (down to MIN_REGION lines); each region costs another run of the page's content. A band that runs
 * short of memory is drawn again half as tall, down to MIN_ROWS. When even that ran short, the page may lack something:
 * stats say so (ViewStats::incomplete). */
#define MIN_REGION 64
#define MIN_ROWS 16
#define KEEP_MARGIN 4  /* lines more each side of a region (antialiasing's spill) */

int view_draw(int page_no, int width, int max_h, int band_rows, view_band_fn out, void* user, ViewStats* st) {
  fz_page* page = NULL;
  fz_display_list* list = NULL;
  fz_device* dev = NULL;
  fz_pixmap* band = NULL;
  int h = -1;
  unsigned t = clock_ms();
  ViewStats dummy;
  if (!st) st = &dummy;
  memset(st, 0, sizeof *st);
  if (!doc) {
    set_error("no document");
    return -1;
  }
  fz_var(page);
  fz_var(list);
  fz_var(dev);
  fz_var(band);
  fz_try(ctx) {
    fz_bbox box;
    page = fz_load_page(doc, page_no);
    pdf_clear_xref((pdf_document*)doc);  /* (what loading it parsed and did not keep: LilyPond's hundreds of links) */
    const fz_matrix ctm = page_ctm(page, width, &box);
    st->load_ms = clock_ms() - t;
    h = box.y1 < max_h ? box.y1 : max_h;
    int region = h;
    for (int y0 = 0; y0 < h;) {
      const int y1 = h - y0 < region ? h : y0 + region;
      unsigned t0 = clock_ms(), f0 = failures();
      list = fz_new_display_list(ctx);
      if (region < h) {
        const fz_rect keep = {0, (float)(y0 - KEEP_MARGIN), (float)box.x1, (float)(y1 + KEEP_MARGIN)};
        fz_kn7_list_keep(list, keep);
      }
      dev = fz_new_list_device(ctx, list);
      fz_run_page(doc, page, dev, ctm, NULL);
      fz_free_device(dev);
      dev = NULL;
      pdf_clear_xref((pdf_document*)doc);
      st->list_ms += clock_ms() - t0;
      ++st->lists;
      if (failures() != f0 && region > MIN_REGION) {  /* incomplete: smaller regions */
        fz_free_display_list(ctx, list);
        list = NULL;
        region = (region + 1) / 2;
        st->direct = 1;
        continue;
      }
      st->incomplete |= failures() != f0;
      t0 = clock_ms();
      int rows = band_rows;
      for (int y = y0; y < y1;) {
        const unsigned f1 = failures();
        const int n = y1 - y < rows ? y1 - y : rows;
        int ok = 1;
        fz_try(ctx) { draw_band(list, page, ctm, box, y, n, &band, NULL, NULL); }
        fz_catch(ctx) { ok = 0; }
        ++st->bands;
        if ((!ok || failures() != f1) && rows > MIN_ROWS) {
          rows /= 2;
          if (band) fz_drop_pixmap(ctx, band);
          band = NULL;
          pdf_close_image_windows(ctx);  /* (its images' decoders, from their tops again) */
          continue;
        }
        if (!ok) fz_throw(ctx, "%s", ctx->error->message);
        st->incomplete |= failures() != f1;
        out(user, y, n, box.x1, fz_pixmap_samples(ctx, band), 2 * box.x1);  /* (gray and alpha: the gray in byte 0) */
        y += n;
      }
      st->draw_ms += clock_ms() - t0;
      fz_free_display_list(ctx, list);
      list = NULL;
      pdf_close_image_windows(ctx);
      y0 = y1;
    }
  }
  fz_catch(ctx) {
    set_error(ctx->error->message);
    h = -1;
  }
  if (dev) fz_free_device(dev);
  if (band) fz_drop_pixmap(ctx, band);
  pdf_close_image_windows(ctx);  /* (the decoders the bands kept open) */
  if (list) fz_free_display_list(ctx, list);
  if (page) fz_free_page(doc, page);
  pdf_clear_xref((pdf_document*)doc);  /* (the objects the page parsed) */
  fz_flush_warnings(ctx);
  return h;
}
