/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * offsets.c -- where doom_test.lua finds the player's state in DOOM.BIN's memory, as mn10300-elf-gcc lays the
 * structures out: `make tests/mame/offsets.txt` compiles this and prints each value. */
#include <stddef.h>
#include "doomdef.h"
#include "d_player.h"
#include "p_mobj.h"

const int off_player_mo = offsetof(player_t, mo);
const int off_player_health = offsetof(player_t, health);
const int off_player_ammo = offsetof(player_t, ammo);
const int off_player_readyweapon = offsetof(player_t, readyweapon);
const int off_player_killcount = offsetof(player_t, killcount);
const int size_player = sizeof(player_t);
const int off_mobj_x = offsetof(mobj_t, x);
const int off_mobj_y = offsetof(mobj_t, y);
const int off_mobj_angle = offsetof(mobj_t, angle);
