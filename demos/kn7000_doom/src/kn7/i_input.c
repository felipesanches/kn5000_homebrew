/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * i_input.c -- Chocolate Doom's i_input.c and i_joystick.c on the KN7000: no mouse, no joystick; the keybed and the
 * panel's buttons become key events (kn7InputPoll, from I_StartTic), with Chocolate Doom's default bindings:
 *   * the keybed is the game pad. Left hand, around F4, the arrow keys as a PC keyboard has them (G#4 forward over
 *     F4, G4 and A4: left, back, right), the black keys beside them strafe (F#4 left, A#4 right) and E4 runs. Right
 *     hand: C5 fires (and selects in the menus), D5 uses (doors, switches), E5 the automap; C6 to B6 (white keys) the
 *     weapons 1 to 7. The sustain pedal fires too. All above C4: the keys below the split point are the arranger's
 *     chord keys, which the firmware sends nowhere a program can read (MAME: G#3 held, nothing in the MIDI OUT ring);
 *   * the soft keys beside the LCD carry what a PC keyboard's other keys do (their labels: i_video.c): left MENU (Esc),
 *     ENTER, YES, NO, MAP; right PAUSE, VIEW - and VIEW + (the view's size), GAMMA (F11), QUIT (F10). EXIT pressed is
 *     Esc too (held, KN7APP ends the program); PAGE UP and DOWN the view's size or, on the automap, its zoom; START/STOP
 *     Enter.
 * A key goes down when the first of its controls does and up when the last one is let go. */
#include <string.h>

#include "kn7doom.h"
#include "d_event.h"
#include "doomkeys.h"
#include "doomstat.h"
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

#define FIRE (-1)  /* KEY_RCTRL, or KEY_ENTER while the menu is up (decided as the control goes down) */

static const struct
{
    u8 note;
    int key;
} noteKeys[] = {
    {64, KEY_RSHIFT},     /* E4: run */
    {65, KEY_LEFTARROW},  /* F4: turn left */
    {66, ','},            /* F#4: strafe left */
    {67, KEY_DOWNARROW},  /* G4: back */
    {68, KEY_UPARROW},    /* G#4: forward */
    {69, KEY_RIGHTARROW}, /* A4: turn right */
    {70, '.'},            /* A#4: strafe right */
    {72, FIRE},           /* C5 */
    {74, ' '},            /* D5: use */
    {76, KEY_TAB},        /* E5: the automap */
    {84, '1'}, {86, '2'}, {88, '3'}, {89, '4'}, {91, '5'}, {93, '6'}, {95, '7'},  /* C6-B6: the weapons */
};
#define NNOTES (sizeof noteKeys / sizeof *noteKeys)

static const struct
{
    u32 button;
    int key;
} panelKeys[] = {
    {BTN_EXIT, KEY_ESCAPE},  {BTN_LCDL1, KEY_ESCAPE}, {BTN_LCDL2, KEY_ENTER},     {BTN_LCDL3, 'y'},
    {BTN_LCDL4, 'n'},        {BTN_LCDL5, KEY_TAB},    {BTN_LCDR1, KEY_PAUSE},     {BTN_LCDR2, KEY_MINUS},
    {BTN_LCDR3, KEY_EQUALS}, {BTN_LCDR4, KEY_F11},    {BTN_LCDR5, KEY_F10},       {BTN_PAGE_UP, KEY_EQUALS},
    {BTN_PAGE_DOWN, KEY_MINUS}, {BTN_START_STOP, KEY_ENTER},
};
#define NPANEL (sizeof panelKeys / sizeof *panelKeys)

/* The soft keys' labels, top to bottom (i_video.c draws them). */
const char *const kn7SoftLabels[2][5] = {{"MENU", "ENTER", "YES", "NO", "MAP"},
                                         {"PAUSE", "VIEW -", "VIEW +", "GAMMA", "QUIT"}};

static int noteSent[NNOTES], panelSent[NPANEL], pedalSent;  /* the key each control sent as it went down, or 0 */
static u8 held[256];                                         /* how many controls hold each key down */

static void post(evtype_t type, int key)
{
    event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.data1 = key;
    if (type == ev_keydown && key >= ' ' && key < 0x7F)
        ev.data2 = ev.data3 = key;
    D_PostEvent(&ev);
}

/* a control is down or up; *sent is the key it sent as it went down */
static void control(int down, int key, int *sent)
{
    if (down && !*sent)
    {
        if (key == FIRE)
            key = menuactive ? KEY_ENTER : KEY_RCTRL;
        *sent = key;
        if (held[key]++ == 0)
            post(ev_keydown, key);
    }
    else if (!down && *sent)
    {
        key = *sent;
        *sent = 0;
        if (held[key] && --held[key] == 0)
            post(ev_keyup, key);
    }
}

void kn7InputPoll(void)
{
    host->panelScan();
    host->pollNotes();
    for (u32 i = 0; i < NNOTES; ++i)
        control(host->notes[noteKeys[i].note] != 0, noteKeys[i].key, &noteSent[i]);
    for (u32 i = 0; i < NPANEL; ++i)
    {
        const u32 b = panelKeys[i].button;
        control((host->panel[b >> 8] & (b & 0xFF)) != 0, panelKeys[i].key, &panelSent[i]);
    }
    control(*host->sustain != 0, FIRE, &pedalSent);
}
