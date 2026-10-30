/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * fontfile.c -- MuPDF's base 14 fonts (pdf_fontfile.c's lookups) as files in the viewer's folder instead of in its
 * image: each read in when a page first uses it, into the slow memory where there is one (fz_kn7_slow_malloc), and kept
 * until the viewer ends (MuPDF takes the bytes as static).
 * The files are MuPDF 1.1's fonts/*.cff (URW's Nimbus fonts, GPL), renamed to 8.3 names (Makefile: card-dir). A
 * document whose fonts are all embedded, as LilyPond's, MuseScore's and Chrome's are, reads none of them. */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "fitz-internal.h"

const char* fontfile_dir = "";  /* put in front of the file names: the viewer's folder */
fz_context* fontfile_ctx;       /* their memory (view.c) */

static const struct {
  const char* name;  /* the PDF's base font name */
  const char* file;  /* its file */
} fonts[] = {
    {"Courier", "COUR.CFF"},          {"Courier-Bold", "COURB.CFF"},
    {"Courier-Oblique", "COURI.CFF"}, {"Courier-BoldOblique", "COURBI.CFF"},
    {"Helvetica", "HELV.CFF"},        {"Helvetica-Bold", "HELVB.CFF"},
    {"Helvetica-Oblique", "HELVI.CFF"}, {"Helvetica-BoldOblique", "HELVBI.CFF"},
    {"Times-Roman", "TIMES.CFF"},     {"Times-Bold", "TIMESB.CFF"},
    {"Times-Italic", "TIMESI.CFF"},   {"Times-BoldItalic", "TIMESBI.CFF"},
    {"Symbol", "SYMBOL.CFF"},         {"ZapfDingbats", "DINGBATS.CFF"},
};
#define FONTS ((int)(sizeof fonts / sizeof *fonts))
static unsigned char* data[FONTS];
static unsigned int bytes[FONTS];

static unsigned char* load(int i, unsigned int* len) {
  *len = 0;
  if (!data[i] && fontfile_ctx) {
    char path[64];
    strcpy(path, fontfile_dir);
    strcat(path, fonts[i].file);
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    const off_t n = lseek(fd, 0, SEEK_END);
    const int slow = fz_kn7_slow_malloc != NULL;
    unsigned char* p = n <= 0 ? NULL : slow ? fz_kn7_slow_malloc((unsigned)n) : fz_malloc_no_throw(fontfile_ctx, (unsigned)n);
    if (p && lseek(fd, 0, SEEK_SET) == 0 && read(fd, p, (size_t)n) == n) {
      data[i] = p;
      bytes[i] = (unsigned)n;
    } else if (p) {
      if (slow) fz_kn7_slow_free(p);
      else fz_free(fontfile_ctx, p);
    }
    close(fd);
  }
  *len = bytes[i];
  return data[i];
}

unsigned char* pdf_lookup_builtin_font(char* name, unsigned int* len) {
  for (int i = 0; i < FONTS; ++i)
    if (!strcmp(fonts[i].name, name)) return load(i, len);
  *len = 0;
  return NULL;
}

/* MuPDF's choice for a font that is neither embedded nor one of the 14 (its NODROIDFONT case) */
unsigned char* pdf_lookup_substitute_font(int mono, int serif, int bold, int italic, unsigned int* len) {
  static const char* const names[3][4] = {
      {"Helvetica", "Helvetica-Oblique", "Helvetica-Bold", "Helvetica-BoldOblique"},
      {"Times-Roman", "Times-Italic", "Times-Bold", "Times-BoldItalic"},
      {"Courier", "Courier-Oblique", "Courier-Bold", "Courier-BoldOblique"},
  };
  return pdf_lookup_builtin_font((char*)names[mono ? 2 : serif ? 1 : 0][(bold ? 2 : 0) + (italic ? 1 : 0)], len);
}

/* no CJK font */
unsigned char* pdf_lookup_substitute_cjk_font(int ros, int serif, unsigned int* len) {
  (void)ros, (void)serif;
  *len = 0;
  return NULL;
}
