/* link_main.c -- the least a viewer calls: open a PDF, render page 1 to a gray pixmap (measure_link.sh). */
#include "mupdf/fitz.h"
int main(void)
{
	fz_context *ctx = fz_new_context(NULL, NULL, 256 << 10);
	fz_register_document_handlers(ctx);
	fz_document *doc = fz_open_document(ctx, "a.pdf");
	fz_page *page = fz_load_page(ctx, doc, 0);
	fz_pixmap *pix = fz_new_pixmap_from_page(ctx, page, fz_scale(1, 1), fz_device_gray(ctx), 0);
	return pix->w + fz_count_pages(ctx, doc);
}
