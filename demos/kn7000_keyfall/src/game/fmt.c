/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * fmt.c -- a small formatter and the string helpers the game needs (newlib's printf family is large: Doom's fmt.c
 * replaced it for 70 KB less). %d %u %x %X %s %c and %%, with a width, '0' and '-' (left-justified). */
#include "kf.h"

static void put(char *out, int room, int *n, char c) {
  if (*n < room - 1) out[*n] = c;
  ++*n;
}

int kf_vfmt(char *out, int room, const char *fmt, va_list ap) {
  int n = 0;
  for (const char *f = fmt; *f; ++f) {
    if (*f != '%') {
      put(out, room, &n, *f);
      continue;
    }
    ++f;
    int left = 0, width = 0;
    char pad = ' ';
    if (*f == '-') left = 1, ++f;
    if (*f == '0') pad = '0', ++f;
    while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    char b[16];
    int len = 0, neg = 0;
    const char *s = b;
    switch (*f) {
      case 'd': {
        const s32 v = va_arg(ap, s32);
        u32 u = v < 0 ? (u32)-v : (u32)v;
        neg = v < 0;
        do b[len++] = (char)('0' + u % 10), u /= 10; while (u);
        break;
      }
      case 'u': {
        u32 u = va_arg(ap, u32);
        do b[len++] = (char)('0' + u % 10), u /= 10; while (u);
        break;
      }
      case 'x':
      case 'X': {
        u32 u = va_arg(ap, u32);
        const char *dig = *f == 'x' ? "0123456789abcdef" : "0123456789ABCDEF";
        do b[len++] = dig[u & 15], u >>= 4; while (u);
        break;
      }
      case 'c': b[len++] = (char)va_arg(ap, int); break;
      case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = kf_strlen(s); break;
      case '%': b[len++] = '%'; break;
      default: b[len++] = '?'; break;
    }
    const int total = len + neg;
    if (!left && pad == ' ')
      for (int i = total; i < width; ++i) put(out, room, &n, ' ');
    if (neg) put(out, room, &n, '-');
    if (!left && pad == '0')
      for (int i = total; i < width; ++i) put(out, room, &n, '0');
    if (s == b)
      for (int i = len - 1; i >= 0; --i) put(out, room, &n, b[i]);  /* (digits made last first) */
    else
      for (int i = 0; i < len; ++i) put(out, room, &n, s[i]);
    if (left)
      for (int i = total; i < width; ++i) put(out, room, &n, ' ');
  }
  if (room > 0) out[n < room ? n : room - 1] = 0;
  return n;
}

int kf_fmt(char *out, int room, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = kf_vfmt(out, room, fmt, ap);
  va_end(ap);
  return n;
}

int kf_strlen(const char *s) {
  int n = 0;
  while (s[n]) ++n;
  return n;
}

void kf_strcpy(char *d, const char *s, int room) {
  int i = 0;
  for (; s[i] && i < room - 1; ++i) d[i] = s[i];
  if (room > 0) d[i] = 0;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

int kf_streq_ci(const char *a, const char *b) {
  for (; *a && *b; ++a, ++b)
    if (lower(*a) != lower(*b)) return 0;
  return *a == *b;
}

int kf_contains_ci(const char *hay, const char *needle) {
  for (; *hay; ++hay) {
    int i = 0;
    while (needle[i] && hay[i] && lower(hay[i]) == lower(needle[i])) ++i;
    if (!needle[i]) return 1;
  }
  return !*needle;
}

void kf_memset(void *d, int c, u32 n) {
  u8 *p = (u8 *)d;
  while (n--) *p++ = (u8)c;
}

void kf_memcpy(void *d, const void *s, u32 n) {
  u8 *p = (u8 *)d;
  const u8 *q = (const u8 *)s;
  while (n--) *p++ = *q++;
}
