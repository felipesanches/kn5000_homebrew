/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * readcache.h -- the PDF file's blocks kept (readcache.c), between MuPDF's reads and the card's. */
#ifndef KN7PDF_READCACHE_H
#define KN7PDF_READCACHE_H
#include <stdint.h>

#define RC_BLOCK 1024
#define RC_MAX_BLOCKS 128 /* at most 128 KB */
#define RC_MAX_RUN 8      /* blocks fetched in one request at most */

/* fetch: `n` bytes of the file from `off` into dst -> the bytes read, or < 0 */
typedef int (*RcFetch)(void* arg, uint32_t off, void* dst, uint32_t n);

/* the memory: its last RC_MAX_RUN blocks for a fetch's, the rest for the blocks kept (none: every read fetched) */
void rc_init(void* mem, uint32_t bytes);
void rc_forget(int file);            /* file (a small number, < 256) closed: its blocks go */
/* `n` bytes of file (size bytes long) from `off` into dst -> the bytes read, or < 0 */
int rc_read(int file, uint32_t size, uint32_t off, void* dst, uint32_t n, RcFetch fetch, void* arg);
extern uint32_t rc_fetches, rc_fetch_bytes, rc_hits;

#endif
