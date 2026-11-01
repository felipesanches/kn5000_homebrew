/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * pagecache.c -- pages drawn ahead of the reader and kept (pagecache.h).
 *   * The display heap: a TLSF heap over the display memory the page shown does not take (the page is 4 bits a pixel,
 *     so it takes half of what an 8-bit page took). It holds the page blobs and, up to fontBudget, MuPDF's font files
 *     (which otherwise take the main heap, where MuPDF's lists and bands need the room). When it is full the least
 *     recently used blob goes, one the card has first.
 *   * A blob: a PcHeader, then the page's rows, each PC_PITCH bytes coded with PackBits on its own (a control byte c:
 *     0-127, the next c + 1 bytes as they are; 129-255, the next byte 257 - c times). The KN7000's pages are mostly
 *     white: 9-23% of their 290 KB (tests/host/packbits_size.py on the two test PDFs, 29-69 KB a page).
 *   * The card: a blob as it is, in a file Kxxxxnnn.PGC of the program's folder, xxxx the document's key (its name and
 *     size), nnn the page from 1; written through the firmware (PfHost::save), read back by sectors (kn7FileRead). A
 *     file whose header does not name the document, the page, the width and the anti-aliasing is not used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fitz.h"
#include "pagecache.h"
#include "tlsf.h"
#include "view.h"

#define PC_MAGIC 0x3147504Bu /* "KPG1" */
#define PC_VERSION 1
#define GROW 16384           /* a blob grows by at least this */
#define MAX_LIST 400         /* the folder's files looked at for the document's pages */

typedef struct PcHeader {
  u32 magic;
  u16 version, width;
  u16 page, rows;  /* (page from 0) */
  u32 bytes;       /* the coded rows after the header */
  u32 docBytes;    /* the PDF's size */
  u16 aa, flags;   /* MuPDF's anti-aliasing bits; flags bit 0: memory ran short while it was drawn */
  char doc[16];    /* the PDF's name */
} PcHeader;

typedef struct PcPage {
  u8* blob;   /* PcHeader and rows, in the display heap, or 0 */
  u32 card;   /* its file's size on the card, or 0 */
  u32 used;   /* when it was last made or shown (stamp) */
  u16 rows;
  u8 saved;   /* the card has the blob in the heap */
  u8 failed;  /* making it did not work */
} PcPage;

static tlsf_t dheap;
static u32 lo1, hi1, lo2, hi2;  /* the display heap's two pieces (addresses) */
static pool_t pool2;
static u32 fontBudget, fontBytes;
static PcPage* pg;
static int nPages;
static char docName[16];
static u32 docBytes, stamp;
static u16 key;
static int mk = -1;  /* the page whose blob is being made */
static u8* mkBlob;
static u32 mkLen, mkCap;
extern fz_context* fontfile_ctx; /* (fontfile.c) */

static int inHeap(const void* p) {
  const u32 a = (u32)p;
  return (a >= lo1 && a < hi1) || (a >= lo2 && a < hi2);
}

void pc_init(u32 lo, u32 hi, u32 loB, u32 hiB, u32 budget) {
  lo1 = LCD_MEM + lo, hi1 = LCD_MEM + hi;
  dheap = tlsf_create_with_pool((void*)lo1, hi - lo);
  if (hiB > loB + 4096) {
    lo2 = LCD_MEM + loB, hi2 = LCD_MEM + hiB;
    pool2 = tlsf_add_pool(dheap, (void*)lo2, hiB - loB);
  }
  fontBudget = budget;
}

/* one blob dropped to make room: the least recently used, one the card has if there is one; never `keep` nor the one
 * being made -> 0, or -1 (none) */
static int evict(int keep) {
  for (int pass = 0; pass < 2; ++pass) {
    int v = -1;
    for (int i = 0; i < nPages; ++i)
      if (pg[i].blob && i != keep && i != mk && (pass || pg[i].saved) && (v < 0 || pg[i].used < pg[v].used)) v = i;
    if (v >= 0) {
      tlsf_free(dheap, pg[v].blob);
      pg[v].blob = NULL;
      return 0;
    }
  }
  return -1;
}

static void* dalloc(u32 n, int keep) {
  for (;;) {
    void* p = tlsf_malloc(dheap, n);
    if (p || evict(keep) < 0) return p;
  }
}

void* pc_slow_malloc(unsigned n) {
  void* p = pc_slow_try(n);
  return p ? p : fz_malloc_no_throw(fontfile_ctx, n);
}

void* pc_slow_try(unsigned n) {
  if (!dheap || fontBytes + n > fontBudget) return NULL;
  void* p = dalloc(n, -1);
  if (p) fontBytes += tlsf_block_size(p);
  return p;
}

void pc_slow_free(void* p) {
  if (!p) return;
  if (inHeap(p)) {
    fontBytes -= tlsf_block_size(p);
    tlsf_free(dheap, p);
  } else {
    fz_free(fontfile_ctx, p);
  }
}

/* ---- the document --------------------------------------------------------------------------------------------- */
/* page n's file on the card (pages 1-999) -> 0, or -1 */
static int cardName(char out[13], int n) {
  if (n < 0 || n >= 999) return -1;
  snprintf(out, 13, "K%04X%03d.PGC", (unsigned)key, (n + 1) % 1000);
  return 0;
}

void pc_open(const char* name, u32 bytes, int pages) {
  pc_close();
  snprintf(docName, sizeof docName, "%s", name);
  docBytes = bytes;
  u32 h = 2166136261u;  /* (FNV-1a over the name and the size) */
  for (const char* s = name; *s; ++s) h = (h ^ (u8)*s) * 16777619u;
  for (int i = 0; i < 4; ++i) h = (h ^ (u8)(bytes >> (8 * i))) * 16777619u;
  key = (u16)(h ^ (h >> 16));
  nPages = pages > 0 ? pages : 0;
  pg = calloc(nPages ? nPages : 1, sizeof *pg);
  if (!pg) nPages = 0;
  PfFile* all = malloc(MAX_LIST * sizeof *all);
  if (!all) return;
  char pre[8];
  snprintf(pre, sizeof pre, "K%04X", key);
  const s32 n = host->list(all, MAX_LIST);
  int found = 0;
  for (s32 i = 0; i < n; ++i) {
    const char* f = all[i].name;
    if (strncmp(f, pre, 5) || strlen(f) != 12 || strcmp(f + 8, ".PGC")) continue;
    const int p = (f[5] - '0') * 100 + (f[6] - '0') * 10 + (f[7] - '0') - 1;
    if (p >= 0 && p < nPages && all[i].bytes > sizeof(PcHeader)) pg[p].card = all[i].bytes, ++found;
  }
  free(all);
  printf("cache: key %04X, %d of %d pages on the card\n", key, found, nPages);
}

void pc_close(void) {
  pc_drop();
  for (int i = 0; i < nPages; ++i)
    if (pg[i].blob) tlsf_free(dheap, pg[i].blob);
  free(pg);
  pg = NULL;
  nPages = 0;
}

int pc_have(int n) { return n >= 0 && n < nPages && pg[n].blob; }
int pc_onCard(int n) { return n >= 0 && n < nPages && pg[n].card; }
int pc_failed(int n) { return n >= 0 && n < nPages && pg[n].failed; }
void pc_setFailed(int n) {
  if (n >= 0 && n < nPages) pg[n].failed = 1;
}
int pc_rows(int n) { return n >= 0 && n < nPages ? pg[n].rows : 0; }

/* ---- making a blob ---------------------------------------------------------------------------------------------- */
int pc_begin(int n) {
  pc_drop();
  if (n < 0 || n >= nPages) return -1;
  if (pg[n].blob) tlsf_free(dheap, pg[n].blob), pg[n].blob = NULL;
  mkBlob = dalloc(GROW, n);
  if (!mkBlob) return -1;
  mk = n, mkCap = GROW, mkLen = sizeof(PcHeader);
  return 0;
}

static int grow(u32 need) {
  if (mkLen + need <= mkCap) return 0;
  const u32 cap = mkLen + need + GROW;
  for (;;) {
    u8* p = tlsf_realloc(dheap, mkBlob, cap);
    if (p) {
      mkBlob = p, mkCap = cap;
      return 0;
    }
    if (evict(mk) < 0) return -1;
  }
}

/* PackBits: n bytes from s into o -> the bytes made (at most n + n / 128 + 1) */
static u32 pack(const u8* s, u32 n, u8* o) {
  u32 i = 0, k = 0;
  while (i < n) {
    u32 j = i + 1;
    while (j < n && j - i < 128 && s[j] == s[i]) ++j;
    if (j - i >= 2) {
      o[k++] = (u8)(257 - (j - i));
      o[k++] = s[i];
      i = j;
      continue;
    }
    for (j = i + 1; j < n && j - i < 128 && !(j + 1 < n && s[j + 1] == s[j]); ++j) {
    }
    o[k++] = (u8)(j - i - 1);
    memcpy(o + k, s + i, j - i);
    k += j - i;
    i = j;
  }
  return k;
}

int pc_band(int y, int h, const unsigned char* gray, int stride) {
  (void)y;
  if (mk < 0) return -1;
  static u8 row[PC_PITCH], coded[PC_PITCH + PC_PITCH / 128 + 2];
  for (int r = 0; r < h; ++r) {
    const unsigned char* s = gray + r * stride;
    for (int x = 0; x < PC_PITCH; ++x) row[x] = (u8)((s[4 * x] & 0xF0) | (s[4 * x + 2] >> 4));  /* (gray, alpha) */
    const u32 n = pack(row, PC_PITCH, coded);
    if (grow(n) < 0) return -1;
    memcpy(mkBlob + mkLen, coded, n);
    mkLen += n;
  }
  return 0;
}

int pc_end(int rows, int incomplete) {
  if (mk < 0) return -1;
  PcHeader hd;
  memset(&hd, 0, sizeof hd);
  hd.magic = PC_MAGIC, hd.version = PC_VERSION, hd.width = PC_WIDTH;
  hd.page = (u16)mk, hd.rows = (u16)rows;
  hd.bytes = mkLen - sizeof hd, hd.docBytes = docBytes;
  hd.aa = (u16)view_aa_bits, hd.flags = incomplete ? 1 : 0;
  memcpy(hd.doc, docName, sizeof hd.doc);
  memcpy(mkBlob, &hd, sizeof hd);
  u8* p = tlsf_realloc(dheap, mkBlob, mkLen);  /* (the room it did not use back) */
  PcPage* g = &pg[mk];
  g->blob = p ? p : mkBlob, g->rows = (u16)rows, g->saved = 0, g->used = ++stamp, g->failed = 0;
  mk = -1, mkBlob = NULL;
  return 0;
}

void pc_drop(void) {
  if (mk >= 0 && mkBlob) tlsf_free(dheap, mkBlob);
  mk = -1, mkBlob = NULL;
}

/* ---- showing and the card --------------------------------------------------------------------------------------- */
static int valid(const PcHeader* hd, int n, u32 size) {
  return hd->magic == PC_MAGIC && hd->version == PC_VERSION && hd->width == PC_WIDTH && hd->page == n &&
         hd->docBytes == docBytes && hd->aa == view_aa_bits && hd->bytes + sizeof *hd == size &&
         !strncmp(hd->doc, docName, sizeof hd->doc);
}

int pc_load(int n) {
  if (n < 0 || n >= nPages || !pg[n].card) return -1;
  char name[13];
  if (cardName(name, n) < 0) return -1;
  PfRawFile f;
  if (kn7FileOpen(name, &f) < 0) {
    pg[n].card = 0;
    return -1;
  }
  u8* b = dalloc(f.bytes, n);
  const int ok = b && kn7FileRead(&f, 0, b, f.bytes) == (int)f.bytes && valid((const PcHeader*)b, n, f.bytes);
  kn7FileClose(&f);
  if (!ok) {
    if (b) tlsf_free(dheap, b);
    printf("cache: %s not used\n", name);
    pg[n].card = 0;
    return -1;
  }
  if (pg[n].blob) tlsf_free(dheap, pg[n].blob);
  pg[n].blob = b, pg[n].rows = ((const PcHeader*)b)->rows, pg[n].saved = 1, pg[n].used = ++stamp;
  return 0;
}

int pc_show(int n, u32 dst, int maxRows) {
  if (n < 0 || n >= nPages) return -1;
  if (!pg[n].blob && pc_load(n) < 0) return -1;
  const PcHeader* hd = (const PcHeader*)pg[n].blob;
  const u8* d = pg[n].blob + sizeof *hd;
  const u32 len = hd->bytes;
  const int rows = hd->rows < maxRows ? hd->rows : maxRows;
  static u8 row[PC_PITCH];
  u32 at = 0;
  for (int r = 0; r < rows; ++r) {
    for (u32 x = 0; x < PC_PITCH;) {
      if (at >= len) return -1;
      const u8 c = d[at++];
      if (c < 128) {
        const u32 cnt = c + 1u;
        if (x + cnt > PC_PITCH || at + cnt > len) return -1;
        memcpy(row + x, d + at, cnt);
        at += cnt, x += cnt;
      } else if (c > 128) {
        const u32 cnt = 257u - c;
        if (x + cnt > PC_PITCH || at >= len) return -1;
        memset(row + x, d[at++], cnt);
        x += cnt;
      }
    }
    volatile u16* o = (volatile u16*)(LCD_MEM + dst + (u32)r * PC_PITCH);
    for (int x = 0; x < PC_PITCH / 2; ++x) o[x] = (u16)(row[2 * x] | (row[2 * x + 1] << 8));
  }
  pg[n].used = ++stamp;
  return rows;
}

int pc_unsaved(void) {
  for (int i = 0; i < nPages; ++i)
    if (pg[i].blob && !pg[i].saved) return i;
  return -1;
}

int pc_save(int n) {
  if (n < 0 || n >= nPages || !pg[n].blob) return -1;
  char name[13];
  if (cardName(name, n) < 0) {
    pg[n].saved = 1;
    return -1;
  }
  const u32 len = ((const PcHeader*)pg[n].blob)->bytes + sizeof(PcHeader);
  const s32 r = host->save(name, pg[n].blob, len);
  pg[n].saved = 1;  /* (tried: not again, whatever came of it) */
  if (r != (s32)len) {
    printf("cache: %s not saved (%ld of %lu bytes)\n", name, (long)r, (unsigned long)len);
    return -1;
  }
  pg[n].card = len;
  return 0;
}

static void counter(void* ptr, size_t size, int used, void* user) {
  (void)ptr;
  if (used) *(u32*)user += (u32)size;
}

void pc_stats(u32* total, u32* used, int* pagesIn, u32* fonts) {
  *total = (hi1 - lo1) + (hi2 - lo2);
  *used = 0;
  if (dheap) {
    tlsf_walk_pool(tlsf_get_pool(dheap), counter, used);
    if (pool2) tlsf_walk_pool(pool2, counter, used);
  }
  int k = 0;
  for (int i = 0; i < nPages; ++i) k += pg[i].blob != NULL;
  *pagesIn = k;
  *fonts = fontBytes;
}
