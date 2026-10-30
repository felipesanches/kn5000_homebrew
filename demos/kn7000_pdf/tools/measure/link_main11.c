/* link_main11.c -- MuPDF 1.1: open a PDF, render page 1 to a gray pixmap (measure_link11.sh) */
#include "fitz.h"
int main(void)
{
	fz_context *ctx = fz_new_context(NULL, NULL, 256 << 10);
	fz_document *doc = fz_open_document(ctx, "a.pdf");
	fz_page *page = fz_load_page(doc, 0);
	fz_rect r = fz_bound_page(doc, page);
	fz_pixmap *pix = fz_new_pixmap_with_bbox(ctx, fz_device_gray, fz_round_rect(r));
	fz_device *dev = fz_new_draw_device(ctx, pix);
	fz_run_page(doc, page, dev, fz_identity, NULL);
	return fz_pixmap_width(ctx, pix) + fz_count_pages(doc);
}
/* no builtin fonts (they would come from the card) */
unsigned char *pdf_lookup_builtin_font(char *name, unsigned int *len) { *len = 0; return 0; }
unsigned char *pdf_lookup_substitute_font(int mono, int serif, int bold, int italic, unsigned int *len) { *len = 0; return 0; }
unsigned char *pdf_lookup_substitute_cjk_font(int ros, int serif, unsigned int *len) { *len = 0; return 0; }
