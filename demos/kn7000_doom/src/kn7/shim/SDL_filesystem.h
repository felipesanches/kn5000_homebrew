/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * SDL_filesystem.h -- stands in for SDL's header in the KN7000 build: there is no preferences folder (m_config.c then
 * keeps its files beside the program). */
#pragma once
#include <stdlib.h>
static inline char *SDL_GetPrefPath(const char *org, const char *app) { (void)org; (void)app; return NULL; }
#define SDL_free free
