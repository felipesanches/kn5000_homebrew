/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * i_input.c -- Chocolate Doom's i_input.c and i_joystick.c on the KN7000: no mouse, no joystick; the panel's buttons and
 * the keybed become key events (kn7InputPoll, from I_StartTic). */
#include "kn7doom.h"
#include "d_event.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "i_input.h"
#include "i_joystick.h"

int vanilla_keyboard_mapping = 1;
float mouse_acceleration = 2.0f;
int mouse_threshold = 10;
int use_analog = 0, joystick_turn_sensitivity = 10, joystick_move_sensitivity = 10, joystick_look_sensitivity = 10;

void I_BindInputVariables(void) {}
void I_ReadMouse(void) {}
void I_StartTextInput(int x1, int y1, int x2, int y2) { (void)x1; (void)y1; (void)x2; (void)y2; }
void I_StopTextInput(void) {}
void I_HandleKeyboardEvent(SDL_Event *e) { (void)e; }
void I_HandleMouseEvent(SDL_Event *e) { (void)e; }
void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

void kn7InputPoll(void) { host->panelScan(); }
