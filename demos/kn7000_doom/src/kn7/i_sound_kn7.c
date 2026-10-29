/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * i_sound_kn7.c -- the KN7000's sound modules, which i_sound.c lists (CHOCO_KN7):
 *   * the sound effects play on the effects DSP's eight-voice mixer (Another World's, which KN7APP.BIN puts into the DSP:
 *     PfHost::dspSet), one voice a channel, from its SDRAM. At start every DS* lump of the WAD goes there: its 8-bit
 *     samples (DMX format: a header of 8 bytes, 16 samples of padding at either end, which Chocolate Doom's
 *     i_sdlsound.c leaves out too) made signed, each sound from a multiple of 32 bytes, coded as the mixer's decoder
 *     takes them (bank.c, ProTracker's) and sent through PfHost::dspSend, BANK_PART bytes of records at a time. The
 *     mixer is mono: a sound's separation is not heard; its volume (0-127) is the voice's (0-63), its pitch the voice's
 *     step (11,025 samples a second at 44,100 frames a second is a quarter of a sample a frame);
 *   * the music is still to come: its module does not start. */
#include <stdio.h>
#include <string.h>

#include "kn7doom.h"
#include "deh_str.h"
#include "doomtype.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

uint32_t bankEncode(const uint8_t *bank, uint32_t words, uint32_t *out);  /* bank.c */
uint32_t bankEncodeMax(uint32_t words);

#define DSP_RATE 44100
#define BANK_PART 0x10000   /* bytes of coded records sent at a time */
#define BANK_ID 0x444D5300u /* "DMS" and the part: the DSP keeps a part's id in its marker (dsp.c) */
#define NORM_PITCH 127

static const snddevice_t sfxDevices[] = {SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                         SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32};
static const snddevice_t musicDevices[] = {SNDDEVICE_GENMIDI, SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
                                           SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32};

/* each sound in the bank: its first word, its samples, its rate; 0 bytes when the lump is not a sound */
typedef struct
{
    u32 word, bytes, rate;
} sound_t;

static sound_t *sound;   /* [NUMSFX], by sfxinfo_t index */
static int sounds;
static sfxinfo_t *sfxBase;
static u32 voices;

/* what each channel's voice plays: the sound, until when (ms) */
#define CHANNELS 8
static struct
{
    int playing, pitch;
    u32 until;
} chan[CHANNELS];

static boolean sfxIsPlaying(int channel);

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    if (sfx->link != NULL)
        sfx = sfx->link;
    M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
}

static int lumpOf(sfxinfo_t *sfx)
{
    char name[9];
    GetSfxLumpName(sfx, name, sizeof name);
    return W_CheckNumForName(name);
}

/* checksum (dsp_bank.checksum) of the bank so far, over its words in order */
static u32 sum, words;

static void sumWords(const u8 *p, u32 n)
{
    for (u32 i = 0; i < n; ++i, p += 4)
        sum = ((sum << 1) | (sum >> 31)) ^ (p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24);
}

/* coded records waiting to be sent: rec[0 .. recBytes) */
static u32 *rec, recBytes;
static int part, sent = 1;

static void sendPart(void)
{
    if (!recBytes)
        return;
    if (host->dspSend(rec, recBytes, words, sum, BANK_ID + (u32)part) != 1)
        sent = 0;
    ++part;
    recBytes = 0;
}

/* One sound into the bank: its samples, made signed, padded with silence to a multiple of 32 bytes, coded. */
static void addSound(int i, const byte *data, u32 length, u32 rate)
{
    const u32 padded = (length + 31) & ~31u, n = padded / 4;
    byte *s = Z_Malloc(padded, PU_STATIC, NULL);
    for (u32 k = 0; k < length; ++k)
        s[k] = data[k] ^ 0x80;
    memset(s + length, 0, padded - length);
    if (recBytes + bankEncodeMax(n) > BANK_PART)
        sendPart();
    u32 *at = (u32 *)((byte *)rec + recBytes);
    const u32 got = bankEncode(s, n, at);
    for (u32 *r = at; (byte *)r < (byte *)at + got; r += 1 + r[0])
        r[1] += words;  /* the block's first word: from the bank's start, not the sound's */
    recBytes += got;
    sound[i] = (sound_t){words, length, rate};
    sumWords(s, n);
    words += n;
    Z_Free(s);
}

static boolean sfxInit(GameMission_t mission)
{
    (void)mission;
    if (!host->dspVoices || !host->dspSend)
        return false;
    voices = host->dspVoices < CHANNELS ? host->dspVoices : CHANNELS;
    return true;
}

static void sfxCache(sfxinfo_t *sfx, int num)
{
    if (sound || !voices)
        return;
    printf("I_InitSound: the sound effects into the DSP");
    sfxBase = sfx;
    sounds = num;
    sound = Z_Malloc(num * sizeof *sound, PU_STATIC, NULL);
    memset(sound, 0, num * sizeof *sound);
    rec = Z_Malloc(BANK_PART, PU_STATIC, NULL);
    for (int i = 0; i < num; ++i)
    {
        const int lump = lumpOf(&sfx[i]);
        if (lump < 0 || sfx[i].link != NULL)
            continue;
        const int lumplen = W_LumpLength(lump);
        const byte *data = W_CacheLumpNum(lump, PU_STATIC);
        const u32 rate = data[2] | data[3] << 8, length = data[4] | data[5] << 8 | data[6] << 16 | (u32)data[7] << 24;
        if (lumplen >= 8 && data[0] == 3 && data[1] == 0 && length <= (u32)lumplen - 8 && length > 48 &&
            words + (length + 31) / 4 <= host->dspMaxWords)
            addSound(i, data + 8 + 16, length - 32, rate);
        W_ReleaseLumpNum(lump);
        if (i % 8 == 0)
            printf(".");
    }
    sendPart();
    Z_Free(rec);
    printf(sent ? " %u KB\n" : " FAILED\n", (unsigned)(words * 4 / 1024));
    if (!sent)
        voices = 0;
}

static int sfxIndex(sfxinfo_t *sfx)
{
    if (sfx->link != NULL)
        sfx = sfx->link;
    const int i = (int)(sfx - sfxBase);
    return i >= 0 && i < sounds ? i : -1;
}

static int sfxGetLumpNum(sfxinfo_t *sfx) { return lumpOf(sfx); }

static void voiceSet(int channel, const sound_t *s, int vol, int pitch, u32 how)
{
    PfVoice v;
    memset(&v, 0, sizeof v);
    v.base = host->dspBase + s->word;
    v.position = 0;
    v.step = (u32)((unsigned long long)s->rate * 65536u * (u32)pitch / NORM_PITCH / DSP_RATE);
    v.end = s->bytes;
    v.loop = 0;
    v.volume = (u32)(vol < 0 ? 0 : vol > 127 ? 63 : vol / 2);
    host->dspSet((u32)channel, &v, how);
}

static int sfxStart(sfxinfo_t *sfx, int channel, int vol, int sep, int pitch)
{
    (void)sep;
    const int i = sfxIndex(sfx);
    if (!voices || channel < 0 || channel >= (int)voices || i < 0 || !sound[i].bytes)
        return -1;
    voiceSet(channel, &sound[i], vol, pitch, 1);
    chan[channel].playing = i;
    chan[channel].pitch = pitch;
    chan[channel].until = host->ms() + sound[i].bytes * 1000u * NORM_PITCH / (sound[i].rate * (u32)pitch) + 1;
    return channel;
}

static void sfxParams(int channel, int vol, int sep)
{
    (void)sep;
    if (!voices || channel < 0 || channel >= (int)voices || !sfxIsPlaying(channel))
        return;
    voiceSet(channel, &sound[chan[channel].playing], vol, chan[channel].pitch, 2);
}

static void sfxStop(int channel)
{
    if (!voices || channel < 0 || channel >= (int)voices)
        return;
    host->dspSet((u32)channel, NULL, 0);
    chan[channel].until = 0;
}

static boolean sfxIsPlaying(int channel)
{
    return channel >= 0 && channel < CHANNELS && (s32)(chan[channel].until - host->ms()) > 0;
}

static void sfxUpdate(void)
{
    if (voices)
        host->dspFlush();
}

static void sfxShutdown(void)
{
    for (u32 c = 0; c < voices; ++c)
        host->dspSet(c, NULL, 0);
    if (voices)
        host->dspFlush();
}

const sound_module_t sound_kn7_module = {
    sfxDevices, sizeof sfxDevices / sizeof *sfxDevices,
    sfxInit, sfxShutdown, sfxGetLumpNum, sfxUpdate, sfxParams, sfxStart, sfxStop, sfxIsPlaying, sfxCache,
};

static boolean musicInit(void) { return false; }
const music_module_t music_kn7_module = {musicDevices, sizeof musicDevices / sizeof *musicDevices, musicInit};
