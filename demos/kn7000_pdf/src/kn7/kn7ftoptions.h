/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU Affero General Public License v3 or later
 *
 * kn7ftoptions.h -- FreeType's options for the viewer: MuPDF's slimftoptions.h (src/third/freetype/), and less, for the
 * KN7000's memory. MuPDF 1.1 maps glyph names to Unicode itself (pdf_encoding.c), so FreeType's copy of the Adobe glyph
 * list goes (55 KB); it hints only without anti-aliasing or for a few DynaLab TrueType fonts (res_font.c), and the
 * viewer always anti-aliases, so the TrueType bytecode interpreter goes as well; no multiple-master or AFM files, no
 * SVG glyphs. */
#include "slimftoptions.h"

#undef FT_CONFIG_OPTION_ADOBE_GLYPH_LIST
#undef FT_CONFIG_OPTION_SVG
#undef TT_CONFIG_OPTION_BYTECODE_INTERPRETER
#define T1_CONFIG_OPTION_NO_AFM
#define T1_CONFIG_OPTION_NO_MM_SUPPORT
