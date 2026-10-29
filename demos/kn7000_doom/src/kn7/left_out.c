/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * left_out.c -- what Chocolate Doom's kept files call of files the KN7000 build leaves out, done as those files do when
 * the thing they serve is not there:
 *   * i_endoom.c (the text screen at the end, through the textscreen library): not shown;
 *   * i_glob.c (a folder's files, for -autoload and dehacked folders): no folder holds any;
 *   * i_sdlmusic.c, i_oplmusic.c, i_sdlsound.c, i_musicpack.c, gusconf.c: their settings, which m_config.c binds and
 *     s_sound.c sets, kept but unused (src/kn7/ plays the sound); the music pack module, which never starts. */
#include <stddef.h>

#include "doomtype.h"
#include "i_endoom.h"
#include "i_glob.h"
#include "i_sound.h"

void I_Endoom(byte *data) { (void)data; }

glob_t *I_StartMultiGlob(const char *directory, int flags, const char *glob, ...)
{
    (void)directory; (void)flags; (void)glob;
    return NULL;
}
void I_EndGlob(glob_t *glob) { (void)glob; }
const char *I_NextGlob(glob_t *glob) { (void)glob; return NULL; }

char *snd_dmxoption = "";
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
char *music_pack_path = "";
char *timidity_cfg_path = "";
char *gus_patch_path = "";
int gus_ram_kb = 1024;
void I_SetOPLDriverVer(opl_driver_ver_t ver) { (void)ver; }
void I_OPL_DevMessages(char *buffer, size_t size) { if (size) buffer[0] = '\0'; }
void I_InitTimidityConfig(void) {}
int opl_io_port = 0x388;

static boolean noMusicPack(void) { return false; }
const music_module_t music_pack_module = {NULL, 0, noMusicPack};
