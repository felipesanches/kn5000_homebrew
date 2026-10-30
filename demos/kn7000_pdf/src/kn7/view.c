/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * view.c -- the viewer's drawing (view.h) with MuPDF 1.1: the page is run once into a display list, which is then
 * drawn a band of rows at a time into a gray pixmap of that band's size, white first. MuPDF's draw device skips the
 * list's items that lie outside the band, so the page costs its content once and the bands little more, and the
 * memory a page needs is the list's and one band's, not a whole page's pixmap (640 x 906 gray with alpha: 1.1 MB). */
#include "fitz-internal.h"
#include "view.h"

unsigned (*view_clock)(void);
extern fz_context* fontfile_ctx;  /* (fontfile.c) */
void pdf_close_image_windows(fz_context* ctx);  /* (MuPDF's pdf_image.c and pdf_xref.c, KN7PDF) */
typedef struct pdf_document_s pdf_document;
void pdf_clear_xref(pdf_document* xref);

static fz_context* ctx;
static fz_document* doc;
static char error[96];

static unsigned clock_ms(void) { return view_clock ? view_clock() : 0; }

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

int view_draw(int page_no, int width, int max_h, int band_rows, view_band_fn out, void* user, ViewStats* st) {
  fz_page* page = NULL;
  fz_display_list* list = NULL;
  fz_device* dev = NULL;
  fz_pixmap* band = NULL;
  int h = -1;
  unsigned t = clock_ms();
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
    if (st) st->load_ms = clock_ms() - t, t = clock_ms();
    list = fz_new_display_list(ctx);
    dev = fz_new_list_device(ctx, list);
    fz_run_page(doc, page, dev, ctm, NULL);
    fz_free_device(dev);
    dev = NULL;
    fz_free_page(doc, page);  /* (the list holds what the page's drawing needs) */
    page = NULL;
    if (st) st->list_ms = clock_ms() - t, t = clock_ms();
    h = box.y1 < max_h ? box.y1 : max_h;
    for (int y = 0; y < h; y += band_rows) {
      const int rows = h - y < band_rows ? h - y : band_rows;
      const fz_bbox b = {0, y, box.x1, y + rows};
      if (!band || fz_pixmap_height(ctx, band) != rows) {
        if (band) fz_drop_pixmap(ctx, band);
        band = NULL;
        band = fz_new_pixmap_with_bbox(ctx, fz_device_gray, b);
      }
      band->x = 0, band->y = y;
      fz_clear_pixmap_with_value(ctx, band, 255);
      dev = fz_new_draw_device(ctx, band);
      fz_run_display_list(list, dev, fz_identity, b, NULL);
      fz_free_device(dev);
      dev = NULL;
      out(user, y, rows, box.x1, fz_pixmap_samples(ctx, band), 2 * box.x1);  /* (gray and alpha: the gray in byte 0) */
    }
    if (st) st->draw_ms = clock_ms() - t;
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
