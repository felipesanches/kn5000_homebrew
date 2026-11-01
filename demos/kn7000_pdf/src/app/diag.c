/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * diag.c -- a log of what KN7APP.BIN and the viewer did, saved on the card as PDFLOG.TXT in the program's folder
 * (KN7000 tools/sdcard: update_sdcard.sh brings it back into sdcard-stage/from-card/), for the instrument, where no
 * debugger looks: each line the kernel's milliseconds and the text. Anything may append (diag, diag_line: the
 * converter entry on the main task, the program task, PDF.BIN through PfHost::diag); only the program task saves
 * (diag_save: file_save waits for the converter entry, which does the writing). The log goes onto the card as it
 * grows: each time it holds DIAG_PART bytes, at the program's next yield (diag_due, from PfHost::yield), its whole
 * lines are saved as the next part, PDFLOG01.TXT, PDFLOG02.TXT ... (at most DIAG_PARTS), and leave the buffer;
 * PDFLOG.TXT is what came after the last part, saved by diag_save. Should the buffer fill before a part is saved,
 * its oldest quarter goes. */
#include <stdarg.h>

#include "app.h"

#define DIAG_SIZE 6144
#define DIAG_PART 3072  /* a part saved when the log holds this much */
#define DIAG_PARTS 50   /* PDFLOG01.TXT to PDFLOG50.TXT (KN7000 tools/sdcard/remove.txt deletes them once brought back) */
static char text[DIAG_SIZE];
static u32 len, part, partFail;
static u8 saving;
u32 diag_saves, diag_save_fail;

static void shift(u32 n) {  /* the first n bytes out of the buffer */
  u32 psw;
  __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
  for (u32 i = n; i < len; ++i) text[i - n] = text[i];
  len -= n;
  __asm__ volatile("mov %0,psw\n\tnop\n\tnop" ::"d"(psw) : "memory");
}

static void put(char c) {
  if (len >= DIAG_SIZE) shift(DIAG_SIZE / 4);  /* full before a part was saved: the oldest quarter goes */
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

/* On the program task: the log's whole lines as the next part, PDFLOGnn.TXT */
static void save_part(void) {
  static char name[] = "PDFLOG00.TXT";
  u32 n = len;
  while (n && text[n - 1] != '\n') --n;
  ++part;
  name[6] = (char)('0' + part / 10), name[7] = (char)('0' + part % 10);
  saving = 1;
  if (file_save(name, text, n) == (s32)n) {
    shift(n);
    diag("log: part %u saved (PDFLOG%02u.TXT)", part, part);
  } else {
    --part, ++partFail;  /* (tried again at a later yield, three times at most) */
  }
  saving = 0;
}

/* At the program's yield (PfHost::yield, never while KN7APP waits on a file itself): a part saved when one is due */
void diag_due(void) {
  if (len >= DIAG_PART && part < DIAG_PARTS && !saving && partFail < 3) save_part();
}

/* On the program task: the log onto the card (a part when one is due, then the rest as PDFLOG.TXT, made afresh). */
void diag_save(void) {
  if (saving) return;
  diag_due();
  saving = 1;
  const u32 n = len;  /* (lines may come while it is written: those go next time) */
  const s32 r = file_save("PDFLOG.TXT", text, n);
  saving = 0;
  ++diag_saves;
  if (r != (s32)n) ++diag_save_fail;
}
