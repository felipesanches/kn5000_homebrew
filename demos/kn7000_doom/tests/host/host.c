/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * host.c -- Chocolate Doom's platform (i_video, i_input, i_joystick, i_timer, the zone's RAM) on the PC, without a
 * window or sound: the port's game code run as a 32-bit program, to measure what it needs of the KN7000 (tests/host/
 * README.md). The screen goes to PPM files when DOOM_PPM is set (every DOOM_PPM_EVERY-th frame, default 35); the zone's
 * size is DOOM_ZONE_KB (default 16384), in DOOM_ZONE_PIECES; DOOM_FRAMES ends it after that many frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

#include "doomtype.h"
#include "d_event.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "z_zone.h"

void D_DoomMain(void);

/* ---- i_video ---------------------------------------------------------------------------------------------------- */
int usemouse = 0, png_screenshots = 0, fullscreen = 0, aspect_ratio_correct = 0, integer_scaling = 0;
int smooth_pixel_scaling = 0, vga_porch_flash = 0, force_software_renderer = 0, usegamma = 0;
int screen_width = SCREENWIDTH, screen_height = SCREENHEIGHT;
char *video_driver = "", *window_position = "";
boolean screenvisible = true, screensaver_mode = false;
unsigned int joywait = 0;
pixel_t *I_VideoBuffer;

static pixel_t screen[SCREENWIDTH * SCREENHEIGHT];
static int zoneSize, zonePeak;

/* After each frame: the zone's blocks that cannot be purged, at their most so far; DOOM_ZONE_DUMP names a file that gets
 * the zone's block list (Z_FileDumpHeap) at that moment (tests/host/zone_tags.py sums it by tag). */
static void zoneWatch(void)
{
    const int used = zoneSize - Z_FreeMemory();
    if (used <= zonePeak)
        return;
    zonePeak = used;
    const char *dump = getenv("DOOM_ZONE_DUMP");
    FILE *f = dump ? fopen(dump, "w") : NULL;
    if (f)
    {
        Z_FileDumpHeap(f);
        fclose(f);
    }
}
static byte palette[768];
static unsigned frames;

void I_InitGraphics(void) { I_VideoBuffer = screen; }
void I_GraphicsCheckCommandLine(void) {}
void I_ShutdownGraphics(void) {}
void I_SetPalette(byte *p) { memcpy(palette, p, sizeof palette); }

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, bestDiff = 1 << 30;
    for (int i = 0; i < 256; i++)
    {
        const int dr = palette[i * 3] - r, dg = palette[i * 3 + 1] - g, db = palette[i * 3 + 2] - b;
        const int diff = dr * dr + dg * dg + db * db;
        if (diff < bestDiff)
        {
            best = i;
            bestDiff = diff;
        }
    }
    return best;
}

void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void)
{
    static const char *dir;
    static int every;
    if (!every)
    {
        dir = getenv("DOOM_PPM");
        every = getenv("DOOM_PPM_EVERY") ? atoi(getenv("DOOM_PPM_EVERY")) : 35;
        if (every < 1)
            every = 1;
    }
    if (dir && frames % every == 0)
    {
        char name[512];
        snprintf(name, sizeof name, "%s/f%06u.ppm", dir, frames);
        FILE *f = fopen(name, "wb");
        if (f)
        {
            fprintf(f, "P6\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
            for (int i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
                fwrite(palette + screen[i] * 3, 1, 3, f);
            fclose(f);
        }
    }
    frames++;
    zoneWatch();
    static int quitAfter = -1;
    if (quitAfter < 0)
        quitAfter = getenv("DOOM_FRAMES") ? atoi(getenv("DOOM_FRAMES")) : 0;
    if (quitAfter && frames >= (unsigned)quitAfter)
    {
        fprintf(stderr, "drew %u frames; zone blocks not purgeable at most %d bytes; malloc in use %zu bytes\n", frames,
                zonePeak, mallinfo2().uordblks);
        exit(0);
    }
}

void I_ReadScreen(pixel_t *scr) { memcpy(scr, screen, sizeof screen); }
void I_BeginRead(void) {}
void I_SetWindowTitle(const char *title) { (void)title; }
void I_CheckIsScreensaver(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void)func; }
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_BindVideoVariables(void) {}
void I_InitWindowTitle(void) {}
void I_RegisterWindowIcon(const unsigned int *icon, int width, int height) { (void)icon; (void)width; (void)height; }
void I_InitWindowIcon(void) {}
void I_StartFrame(void) {}
void I_StartTic(void) {}
void I_GetWindowPosition(int *x, int *y, int w, int h) { (void)w; (void)h; *x = *y = 0; }

/* ---- i_input, i_joystick ------------------------------------------------------------------------------------------ */
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

/* ---- i_timer: the PC's clock -------------------------------------------------------------------------------------- */
static struct timespec t0;

int I_GetTimeMS(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int)((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
}

int I_GetTime(void) { return I_GetTimeMS() * TICRATE / 1000; }

void I_Sleep(int ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

void I_InitTimer(void) { clock_gettime(CLOCK_MONOTONIC, &t0); }
void I_WaitVBL(int count) { I_Sleep(count * 1000 / 70); }

/* ---- the zone: DOOM_ZONE_KB of it, in DOOM_ZONE_PIECES pieces (default 1) with a gap of 4 KB between two, as the
 * KN7000's zone is several regions (z_zone.c, CHOCO_KN7) ---------------------------------------------------------- */
const kn7_region_t *I_ZoneRegions(int *count)
{
    static kn7_region_t region[16];
    const char *kb = getenv("DOOM_ZONE_KB"), *pieces = getenv("DOOM_ZONE_PIECES");
    const int bytes = (kb ? atoi(kb) : 16384) * 1024, n = pieces ? atoi(pieces) : 1;
    if (n < 1 || n > 16)
        I_Error("DOOM_ZONE_PIECES: 1 to 16");
    const unsigned piece = (bytes / n) & ~15u, gap = 4096;
    byte *zone = malloc(n * (piece + gap) + 16);
    if (zone == NULL)
        I_Error("no memory for a zone of %d bytes", bytes);
    const unsigned lo = ((unsigned)zone + 15) & ~15u;
    for (int i = 0; i < n; ++i)
        region[i] = (kn7_region_t){lo + i * (piece + gap), lo + i * (piece + gap) + piece};
    zoneSize = piece * n;
    printf("zone: %d KB in %d pieces\n", zoneSize / 1024, n);
    *count = n;
    return region;
}

int main(int argc, char **argv)
{
    myargc = argc;
    myargv = argv;
    M_FindResponseFile();
    M_SetExeDir();
    D_DoomMain();
    return 0;
}
