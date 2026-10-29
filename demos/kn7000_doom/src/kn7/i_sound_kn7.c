/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * i_sound_kn7.c -- the KN7000's sound modules, which i_sound.c lists (CHOCO_KN7): the effects through the effects DSP's
 * mixer and the music on the tone generator, both still to come; until then neither starts, and Doom is silent. */
#include <stddef.h>

#include "doomtype.h"
#include "i_sound.h"

static const snddevice_t sfxDevices[] = {SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                         SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32};
static const snddevice_t musicDevices[] = {SNDDEVICE_GENMIDI, SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
                                           SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32};

static boolean sfxInit(GameMission_t mission) { (void)mission; return false; }
static boolean musicInit(void) { return false; }

const sound_module_t sound_kn7_module = {sfxDevices, sizeof sfxDevices / sizeof *sfxDevices, sfxInit};
const music_module_t music_kn7_module = {musicDevices, sizeof musicDevices / sizeof *musicDevices, musicInit};
