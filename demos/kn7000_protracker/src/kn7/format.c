/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * format.c -- numbers into text for the side columns and the file names (kn7pt.h). */
#include "kn7pt.h"

char *fmtDec(char *out, int v, int width, bool zeros, bool sign)
{
	char t[12];
	int n = 0;
	unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
	do
		t[n++] = (char)('0' + u % 10);
	while (u /= 10);
	if (v < 0 || sign)
		*out++ = v < 0 ? '-' : '+';
	for (int pad = width - n - (v < 0 || sign ? 1 : 0); pad > 0; pad--)
		*out++ = zeros ? '0' : ' ';
	while (n)
		*out++ = t[--n];
	*out = '\0';
	return out;
}

char *fmtHex(char *out, uint32_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		*out++ = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
	*out = '\0';
	return out;
}

char *fmtStr(char *out, const char *s, int width)
{
	int i = 0;
	for (; s[i] && (width == 0 || i < width); i++)
		*out++ = s[i];
	for (; i < width; i++)
		*out++ = ' ';
	*out = '\0';
	return out;
}
