/* left_out.c -- what MuPDF 1.1 refers to that the viewer leaves out: the XPS and CBZ documents and JPEG 2000 images
 * (OpenJPEG). A JPX image is drawn as nothing, with a warning (MuPDF's own handling of an image it cannot load). */
#include "fitz-internal.h"

struct xps_document *xps_open_document(fz_context *ctx, char *filename)
{
	fz_throw(ctx, "XPS documents are not supported: %s", filename);
	return NULL;
}

struct cbz_document *cbz_open_document(fz_context *ctx, char *filename)
{
	fz_throw(ctx, "CBZ documents are not supported: %s", filename);
	return NULL;
}

fz_pixmap *fz_load_jpx(fz_context *ctx, unsigned char *data, int size, fz_colorspace *defcs, int indexed)
{
	fz_throw(ctx, "JPEG 2000 images are not supported");
	return NULL;
}
