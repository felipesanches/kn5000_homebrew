/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * i_timer.c -- Chocolate Doom's i_timer.c on the KN7000: the kernel's millisecond counter (PfHost::msCounter), and the
 * firmware's yield to wait (PfHost::yield: the firmware's tasks run meanwhile). */
#include "kn7doom.h"
#include "doomtype.h"
#include "i_timer.h"

static u32 base;

static u32 msNow(void) { return *(const volatile u32 *)host->msCounter; }

int I_GetTimeMS(void) { return (int)(msNow() - base); }

int I_GetTime(void) { return (int)((unsigned long long)(msNow() - base) * TICRATE / 1000); }

void I_Sleep(int ms) { host->yield(ms > 0 ? (u32)ms : 1); }

void I_WaitVBL(int count) { I_Sleep(count * 1000 / 70); }

void I_InitTimer(void) { base = msNow(); }
