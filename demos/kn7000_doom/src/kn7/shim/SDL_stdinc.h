/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * SDL_stdinc.h -- stands in for SDL's header in the KN7000 build (m_argv.c sorts with SDL_qsort). */
#pragma once
#include <stdlib.h>
#define SDL_qsort qsort
