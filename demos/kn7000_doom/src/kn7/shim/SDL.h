/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * SDL.h -- stands in for SDL's header in the KN7000 build: the few names Chocolate Doom's kept files use (i_system.c,
 * i_input.h). There is no SDL; src/kn7/ does what it did. */
#pragma once
#include "SDL_stdinc.h"

typedef union SDL_Event {
  unsigned type;
} SDL_Event;

#define SDL_MESSAGEBOX_ERROR 0x10
static inline void SDL_Quit(void) {}
static inline int SDL_ShowSimpleMessageBox(unsigned flags, const char *title, const char *message, void *window) {
  (void)flags; (void)title; (void)message; (void)window;
  return 0;
}
