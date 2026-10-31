/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * pagecache.h -- pages drawn ahead of the reader and kept: each page, as the LCD shows it (4 bits a pixel, PC_PITCH
 * bytes a row), PackBits-coded a row at a time into a blob in the display heap (display memory the page shown does
 * not take), and saved on the card beside the PDFs (Kxxxxnnn.PGC: the document's key and the page) so that the
 * next time the document is opened its pages come from there. */
#ifndef KN7PDF_PAGECACHE_H
#define KN7PDF_PAGECACHE_H
#include "kn7pdf.h"

#define PC_WIDTH 640
#define PC_PITCH (PC_WIDTH / 2)  /* bytes a row at 4 bits a pixel */

/* The display heap over [lo, hi) and [lo2, hi2) of display memory (byte offsets from LCD_MEM); the font files' share
 * of it (bytes; past that they go to the main heap). */
void pc_init(u32 lo, u32 hi, u32 lo2, u32 hi2, u32 fontBudget);
/* for MuPDF's font files (fz_kn7_slow_malloc, fz_kn7_slow_free): the display heap, or the main heap */
void* pc_slow_malloc(unsigned n);
void pc_slow_free(void* p);

/* A document opened: its name and size make its key; `pages` of it. The card's folder listed for its pages. */
void pc_open(const char* name, u32 bytes, int pages);
void pc_close(void);

/* What is known of page n: in the display heap, on the card, or neither. */
int pc_have(int n);    /* 1: its blob is in the display heap */
int pc_onCard(int n);  /* 1: the card has it */
int pc_failed(int n);  /* 1: drawing it into the cache did not work (the viewer draws it straight to the LCD) */
void pc_setFailed(int n);
/* page n's height in rows (once it is in the heap or was) */
int pc_rows(int n);

/* Making page n's blob: begin, a band of 8-bit gray rows (MuPDF's gray and alpha: gray every other byte) at a time,
 * then end (-> 0, or -1: no room; the blob is gone) or drop (aborted). */
int pc_begin(int n);
int pc_band(int y, int h, const unsigned char* gray, int stride);
int pc_end(int rows, int incomplete);
void pc_drop(void);

/* Page n's blob (from the card first when it is only there) into the display memory at `dst` (LCD_MEM-relative
 * offset), PC_PITCH bytes a row, at most maxRows rows -> its rows, or -1. `keep` marks it used (the cache keeps the
 * most recently used). */
int pc_show(int n, u32 dst, int maxRows);
/* Page n from the card into the display heap -> 0, or -1. */
int pc_load(int n);

/* A page made since the document was opened and not yet on the card -> its number, or -1. pc_save writes it. */
int pc_unsaved(void);
int pc_save(int n);

/* the display heap: its size, what is in use, the pages in it (for the log) */
void pc_stats(u32* total, u32* used, int* pagesIn, u32* fontBytes);

#endif
