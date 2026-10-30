/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * fmt.c -- the C library's formatted input and output for DOOM.BIN, in place of newlib's: its printf and scanf families
 * with their floating-point conversions are about 70 KB of the image, and the KN7000 has little RAM to spare. What
 * Chocolate Doom's sources use is here:
 *   * printf: flags - + space # 0, a width and a precision (or *), the lengths hh h l ll z t j, and d i u o x X c s p f %;
 *     %f to at most 9 decimals, in double arithmetic;
 *   * scanf: whitespace, literal characters, and %d %i %u %o %x %c %s %[set] %[^set] %%, with * (assign nothing), a width
 *     and the lengths h l z t j;
 *   * atof and strtod: [+-]digits[.digits][e[+-]digits], decimal only.
 * Output to stdout and stderr goes straight to _write (main.c: the startup log); to a file, through fwrite. */
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _write(int fd, const void *p, int n);

/* ---- printf ------------------------------------------------------------------------------------------------------ */
typedef struct
{
    char *buf;
    size_t size, len;  /* the buffer's size, and the characters the output had so far (also past the size) */
} Out;

static void put(Out *o, char c)
{
    if (o->len + 1 < o->size)
        o->buf[o->len] = c;
    ++o->len;
}

static void pad(Out *o, char c, int n)
{
    while (n-- > 0)
        put(o, c);
}

/* an unsigned number in base, with at least `precision` digits, `width` wide */
static void number(Out *o, uintmax_t v, unsigned base, int upper, int negative, int flags, int width, int precision,
                   const char *prefix)
{
    char digits[32];
    int n = 0;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    while (v)
    {
        digits[n++] = set[v % base];
        v /= base;
    }
    if (precision < 0)
        precision = 1;
    const int zeros = precision > n ? precision - n : 0;
    const char sign = negative ? '-' : (flags & 2) ? '+' : (flags & 4) ? ' ' : 0;
    const int len = n + zeros + (sign != 0) + (int)strlen(prefix);
    if (!(flags & 1) && !(flags & 16))
        pad(o, ' ', width - len);
    if (sign)
        put(o, sign);
    for (const char *p = prefix; *p; ++p)
        put(o, *p);
    if (!(flags & 1) && (flags & 16))
        pad(o, '0', width - len);
    pad(o, '0', zeros);
    while (n)
        put(o, digits[--n]);
    if (flags & 1)
        pad(o, ' ', width - len);
}

static void fixed(Out *o, double v, int flags, int width, int precision)
{
    if (precision < 0)
        precision = 6;
    if (precision > 9)
        precision = 9;
    const int negative = v < 0;
    if (negative)
        v = -v;
    uint32_t scale = 1;
    for (int i = 0; i < precision; ++i)
        scale *= 10;
    v = v * scale + 0.5;
    if (v >= 1.8e19)
        v = 1.8e19;
    const uint64_t all = (uint64_t)v;
    const uint64_t whole = all / scale;
    uint32_t frac = (uint32_t)(all % scale);
    char tail[12];
    int t = 0;
    if (precision > 0 || (flags & 8))
        tail[t++] = '.';
    for (int i = precision - 1; i >= 0; --i)
        tail[t + i] = (char)('0' + frac % 10), frac /= 10;
    t += precision;
    const int tailWidth = width - t;
    number(o, whole, 10, 0, negative, flags & ~1, (flags & 1) ? 0 : tailWidth, 1, "");
    for (int i = 0; i < t; ++i)
        put(o, tail[i]);
    if (flags & 1)
    {
        int len = t + 1 + negative;
        for (uint64_t w = whole / 10; w; w /= 10)
            ++len;
        pad(o, ' ', width - len);
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    Out o = {buf, size, 0};
    for (const char *f = fmt; *f; ++f)
    {
        if (*f != '%')
        {
            put(&o, *f);
            continue;
        }
        int flags = 0, width = 0, precision = -1;
        for (;; ++f)
        {
            const char c = f[1];
            if (c == '-')
                flags |= 1;
            else if (c == '+')
                flags |= 2;
            else if (c == ' ')
                flags |= 4;
            else if (c == '#')
                flags |= 8;
            else if (c == '0')
                flags |= 16;
            else
                break;
        }
        ++f;
        if (*f == '*')
        {
            width = va_arg(ap, int);
            if (width < 0)
                flags |= 1, width = -width;
            ++f;
        }
        else
            while (isdigit((unsigned char)*f))
                width = width * 10 + (*f++ - '0');
        if (*f == '.')
        {
            ++f;
            precision = 0;
            if (*f == '*')
            {
                precision = va_arg(ap, int);
                ++f;
            }
            else
                while (isdigit((unsigned char)*f))
                    precision = precision * 10 + (*f++ - '0');
        }
        int size = 0;  /* 1 char, 2 short, 4 long, 8 long long, 3 size_t/ptrdiff_t/intmax_t */
        if (*f == 'h')
            size = f[1] == 'h' ? (++f, 1) : 2, ++f;
        else if (*f == 'l')
            size = f[1] == 'l' ? (++f, 8) : 4, ++f;
        else if (*f == 'z' || *f == 't')
            size = 3, ++f;
        else if (*f == 'j')
            size = 8, ++f;
        const char c = *f;
        if (c == 'd' || c == 'i')
        {
            intmax_t v = size == 8 ? va_arg(ap, long long) : size == 4 ? va_arg(ap, long) : va_arg(ap, int);
            if (size == 1)
                v = (signed char)v;
            else if (size == 2)
                v = (short)v;
            number(&o, v < 0 ? -(uintmax_t)v : (uintmax_t)v, 10, 0, v < 0, flags, width, precision, "");
        }
        else if (c == 'u' || c == 'x' || c == 'X' || c == 'o')
        {
            uintmax_t v = size == 8 ? va_arg(ap, unsigned long long) : size == 4 ? va_arg(ap, unsigned long)
                                                                                 : va_arg(ap, unsigned);
            if (size == 1)
                v = (unsigned char)v;
            else if (size == 2)
                v = (unsigned short)v;
            const unsigned base = c == 'u' ? 10 : c == 'o' ? 8 : 16;
            const char *prefix = (flags & 8) && v ? (c == 'x' ? "0x" : c == 'X' ? "0X" : c == 'o' ? "0" : "") : "";
            number(&o, v, base, c == 'X', 0, flags, width, precision, prefix);
        }
        else if (c == 'p')
            number(&o, (uintptr_t)va_arg(ap, void *), 16, 0, 0, flags, width, precision, "0x");
        else if (c == 'c')
        {
            if (!(flags & 1))
                pad(&o, ' ', width - 1);
            put(&o, (char)va_arg(ap, int));
            if (flags & 1)
                pad(&o, ' ', width - 1);
        }
        else if (c == 's')
        {
            const char *s = va_arg(ap, const char *);
            if (s == NULL)
                s = "(null)";
            int n = 0;
            while (s[n] && (precision < 0 || n < precision))
                ++n;
            if (!(flags & 1))
                pad(&o, ' ', width - n);
            for (int i = 0; i < n; ++i)
                put(&o, s[i]);
            if (flags & 1)
                pad(&o, ' ', width - n);
        }
        else if (c == 'f' || c == 'F' || c == 'g' || c == 'e')
            fixed(&o, va_arg(ap, double), flags, width, precision);
        else if (c == '%')
            put(&o, '%');
        else if (c == 0)
            break;
        else
        {
            put(&o, '%');
            put(&o, c);
        }
    }
    if (size)
        buf[o.len < size ? o.len : size - 1] = 0;
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, INT_MAX, fmt, ap); }

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, INT_MAX, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char line[512];
    va_list again;
    va_copy(again, ap);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    char *text = line;
    if (n >= (int)sizeof line && (text = malloc((size_t)n + 1)) != NULL)
        vsnprintf(text, (size_t)n + 1, fmt, again);
    else if (n >= (int)sizeof line)
        n = sizeof line - 1;
    va_end(again);
    if (f == stdout || f == stderr)
        _write(f == stdout ? 1 : 2, text, n);
    else
        n = (int)fwrite(text, 1, (size_t)n, f);
    if (text != line)
        free(text);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s)
{
    _write(1, s, (int)strlen(s));
    _write(1, "\n", 1);
    return 1;
}

int putchar(int c)
{
    const char ch = (char)c;
    _write(1, &ch, 1);
    return (unsigned char)c;
}

/* ---- scanf ------------------------------------------------------------------------------------------------------- */
typedef struct
{
    const char *s;  /* a string, or ... */
    FILE *f;        /* ... a file */
    int count;      /* characters taken */
} In;

static int get(In *in)
{
    int c = in->s ? (*in->s ? (unsigned char)*in->s++ : EOF) : fgetc(in->f);
    if (c != EOF)
        ++in->count;
    return c;
}

static void unget(In *in, int c)
{
    if (c == EOF)
        return;
    --in->count;
    if (in->s)
        --in->s;
    else
        ungetc(c, in->f);
}

static int digitOf(int c, unsigned base)
{
    const int d = isdigit(c) ? c - '0' : isalpha(c) ? tolower(c) - 'a' + 10 : 99;
    return d < (int)base ? d : -1;
}

static int scan(In *in, const char *fmt, va_list ap)
{
    int assigned = 0, c;
    for (const char *f = fmt; *f; ++f)
    {
        if (isspace((unsigned char)*f))
        {
            while (isspace(c = get(in)))
                ;
            unget(in, c);
            continue;
        }
        if (*f != '%' || f[1] == '%')
        {
            if (*f == '%')
                ++f;
            if ((c = get(in)) != (unsigned char)*f)
            {
                unget(in, c);
                return assigned ? assigned : c == EOF ? EOF : 0;
            }
            continue;
        }
        ++f;
        const int skip = *f == '*';
        if (skip)
            ++f;
        int width = 0;
        while (isdigit((unsigned char)*f))
            width = width * 10 + (*f++ - '0');
        if (width == 0)
            width = INT_MAX;
        int size = 0;
        while (*f == 'h' || *f == 'l' || *f == 'z' || *f == 't' || *f == 'j')
            size = *f == 'h' ? 2 : 4, ++f;
        const char conv = *f;
        if (conv == 0)
            break;
        if (conv != 'c' && conv != '[')
        {
            while (isspace(c = get(in)))
                ;
            unget(in, c);
        }
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o')
        {
            unsigned base = conv == 'x' || conv == 'X' ? 16 : conv == 'o' ? 8 : 10;
            int negative = 0, any = 0;
            unsigned long v = 0;
            c = get(in);
            if ((c == '-' || c == '+') && width > 0)
                negative = c == '-', c = get(in), --width;
            if (conv == 'i' && c == '0' && width > 0)
            {
                any = 1, base = 8, c = get(in), --width;
                if ((c == 'x' || c == 'X') && width > 0)
                    base = 16, any = 0, c = get(in), --width;
            }
            else if (base == 16 && c == '0' && width > 1)
            {
                any = 1, c = get(in), --width;
                if (c == 'x' || c == 'X')
                    any = 0, c = get(in), --width;
            }
            for (int d; width > 0 && (d = digitOf(c, base)) >= 0; --width, c = get(in))
                v = v * base + (unsigned)d, any = 1;
            unget(in, c);
            if (!any)
                return assigned ? assigned : c == EOF ? EOF : 0;
            if (negative)
                v = -v;
            if (!skip)
            {
                if (size == 2)
                    *va_arg(ap, short *) = (short)v;
                else if (size == 4)
                    *va_arg(ap, long *) = (long)v;
                else
                    *va_arg(ap, int *) = (int)v;
                ++assigned;
            }
        }
        else if (conv == 's' || conv == 'c' || conv == '[')
        {
            char set[256];
            if (conv == '[')
            {
                const int negate = f[1] == '^';
                f += negate ? 2 : 1;
                memset(set, negate, sizeof set);
                if (*f == ']')
                    set[(unsigned char)*f++] = !negate;
                for (; *f && *f != ']'; ++f)
                    set[(unsigned char)*f] = !negate;
            }
            if (conv == 'c' && width == INT_MAX)
                width = 1;
            char *to = skip ? NULL : va_arg(ap, char *);
            int n = 0;
            for (; n < width; ++n)
            {
                c = get(in);
                if (c == EOF || (conv == 's' && isspace(c)) || (conv == '[' && !set[c]))
                {
                    unget(in, c);
                    break;
                }
                if (to)
                    to[n] = (char)c;
            }
            if (n == 0)
                return assigned ? assigned : c == EOF ? EOF : 0;
            if (to)
            {
                if (conv != 'c')
                    to[n] = 0;
                ++assigned;
            }
        }
        else if (conv == 'n')
        {
            if (!skip)
                *va_arg(ap, int *) = in->count;
        }
        else
            break;
    }
    return assigned;
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    In in = {s, NULL, 0};
    return scan(&in, fmt, ap);
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsscanf(s, fmt, ap);
    va_end(ap);
    return n;
}

int fscanf(FILE *f, const char *fmt, ...)
{
    In in = {NULL, f, 0};
    va_list ap;
    va_start(ap, fmt);
    const int n = scan(&in, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- atof -------------------------------------------------------------------------------------------------------- */
double strtod(const char *s, char **end)
{
    const char *p = s;
    while (isspace((unsigned char)*p))
        ++p;
    const int negative = *p == '-';
    if (*p == '-' || *p == '+')
        ++p;
    double v = 0;
    int any = 0;
    for (; isdigit((unsigned char)*p); ++p)
        v = v * 10 + (*p - '0'), any = 1;
    if (*p == '.')
        for (double scale = 0.1; isdigit((unsigned char)*++p); scale /= 10)
            v += (*p - '0') * scale, any = 1;
    if (any && (*p == 'e' || *p == 'E'))
    {
        const char *q = p + 1;
        const int down = *q == '-';
        if (*q == '-' || *q == '+')
            ++q;
        if (isdigit((unsigned char)*q))
        {
            int e = 0;
            for (; isdigit((unsigned char)*q); ++q)
                e = e * 10 + (*q - '0');
            while (e--)
                v = down ? v / 10 : v * 10;
            p = q;
        }
    }
    if (end)
        *end = (char *)(any ? p : s);
    return negative ? -v : v;
}

double atof(const char *s) { return strtod(s, NULL); }
