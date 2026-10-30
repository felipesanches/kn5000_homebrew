/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * diag.c -- a log of what KN7APP.BIN and the viewer did, saved on the card as PDFLOG.TXT in the program's folder
 * (KN7000 tools/sdcard: update_sdcard.sh brings it back into sdcard-stage/from-card/), for the instrument, where no
 * debugger looks: each line the kernel's milliseconds and the text. Anything may append (diag, diag_line: the
 * converter entry on the main task, the program task, PDF.BIN through PfHost::diag); only the program task saves
 * (diag_save: file_save waits for the converter entry, which does the writing), the whole log each time, the file
 * made afresh. The buffer keeps the first DIAG_KEEP bytes and then the latest, so the start and the end survive. */
#include <stdarg.h>

#include "app.h"

#define DIAG_SIZE 8192
#define DIAG_KEEP 3072
static char text[DIAG_SIZE];
static u32 len;
u32 diag_saves, diag_save_fail;

static void put(char c) {
  if (len >= DIAG_SIZE) {  /* full: the part after DIAG_KEEP moves up by a quarter */
    const u32 drop = (DIAG_SIZE - DIAG_KEEP) / 4;
    for (u32 i = DIAG_KEEP; i + drop < DIAG_SIZE; ++i) text[i] = text[i + drop];
    len -= drop;
  }
  text[len++] = c;
}

static void put_s(const char* s) {
  while (*s) put(*s++);
}

static void put_u(u32 v, u32 base, int width, char pad) {
  char b[12];
  int n = 0;
  do {
    const u32 d = v % base;
    b[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
    v /= base;
  } while (v && n < 12);
  while (n < width) put(pad), --width;
  while (n) put(b[--n]);
}

/* printf-like: %s %d %u %x %X, a width and 0 for numbers (%08X), %c */
void diag(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  put_u(fw.ms ? now_ms() : 0, 10, 8, ' ');
  put_s(" ms  ");
  for (const char* f = fmt; *f; ++f) {
    if (*f != '%') {
      put(*f);
      continue;
    }
    ++f;
    char pad = ' ';
    int width = 0;
    if (*f == '0') pad = '0', ++f;
    while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    switch (*f) {
      case 's': put_s(va_arg(ap, const char*)); break;
      case 'c': put((char)va_arg(ap, int)); break;
      case 'd': {
        const s32 v = va_arg(ap, s32);
        if (v < 0) put('-');
        put_u(v < 0 ? (u32)-v : (u32)v, 10, width, pad);
        break;
      }
      case 'u': put_u(va_arg(ap, u32), 10, width, pad); break;
      case 'x':
      case 'X': put_u(va_arg(ap, u32), 16, width, pad); break;
      case '%': put('%'); break;
      default: put('?'); break;
    }
  }
  put('\r');
  put('\n');
  va_end(ap);
}

/* a line of the program's (PfHost::diag) */
void diag_line(const char* s) { diag("%s", s); }

/* On the program task: the log onto the card (PDFLOG.TXT, made afresh). */
void diag_save(void) {
  const s32 r = file_save("PDFLOG.TXT", text, len);
  ++diag_saves;
  if (r != (s32)len) ++diag_save_fail;
}
