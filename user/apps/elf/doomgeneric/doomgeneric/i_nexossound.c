//
// NexOS sound backend for Doomgeneric.
//

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "i_sound.h"
#include "i_timer.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "nexos/audio.h"

#define NEXOS_SOUND_CHANNELS 16
#define NEXOS_SOUND_RATE 48000

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

typedef struct {
    uint8_t *data;
    uint32_t bytes;
    uint32_t frames;
} nexos_sound_t;

static boolean nexos_sound_initialized;
static boolean nexos_use_sfx_prefix;
static uint32_t nexos_audio_index;
static sfxinfo_t *nexos_channels[NEXOS_SOUND_CHANNELS];
static int nexos_channel_end_ms[NEXOS_SOUND_CHANNELS];

static uint16_t nexos_read_le16(const byte *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t nexos_read_le32(const byte *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int16_t nexos_u8_to_s16(uint8_t value) {
    return (int16_t)(((int32_t)value - 128) << 8);
}

static void nexos_get_sfx_lump_name(sfxinfo_t *sfx, char *buf, size_t buf_len) {
    if (sfx->link != NULL) {
        sfx = sfx->link;
    }

    if (nexos_use_sfx_prefix) {
        M_snprintf(buf, buf_len, "ds%s", sfx->name);
    } else {
        M_StringCopy(buf, sfx->name, buf_len);
    }
}

static boolean nexos_expand_sound(sfxinfo_t *sfxinfo,
                                  const byte *samples,
                                  uint32_t sample_rate,
                                  uint32_t sample_count) {
    nexos_sound_t *sound;
    int16_t *out;
    uint32_t out_frames;
    uint32_t frame;
    uint32_t out_bytes;

    if (sample_rate == 0 || sample_count == 0) {
        return false;
    }

    out_frames =
        (uint32_t)(((uint64_t)sample_count * NEXOS_SOUND_RATE) / sample_rate);
    if (out_frames == 0) {
        return false;
    }

    out_bytes = out_frames * 2u * sizeof(int16_t);
    sound = (nexos_sound_t *)malloc(sizeof(*sound));
    if (sound == NULL) {
        return false;
    }
    sound->data = (uint8_t *)malloc(out_bytes);
    if (sound->data == NULL) {
        free(sound);
        return false;
    }

    out = (int16_t *)sound->data;
    for (frame = 0; frame < out_frames; frame++) {
        uint32_t src =
            (uint32_t)(((uint64_t)frame * sample_rate) / NEXOS_SOUND_RATE);
        int16_t sample;

        if (src >= sample_count) {
            src = sample_count - 1u;
        }
        sample = nexos_u8_to_s16(samples[src]);
        out[frame * 2u] = sample;
        out[frame * 2u + 1u] = sample;
    }

    sound->bytes = out_bytes;
    sound->frames = out_frames;
    sfxinfo->driver_data = sound;
    return true;
}

static boolean nexos_cache_sfx(sfxinfo_t *sfxinfo) {
    int lumpnum;
    unsigned int lumplen;
    uint32_t sample_rate;
    uint32_t sample_count;
    byte *data;
    boolean ok;

    lumpnum = sfxinfo->lumpnum;
    data = (byte *)W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = W_LumpLength(lumpnum);

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00) {
        W_ReleaseLumpNum(lumpnum);
        return false;
    }

    sample_rate = nexos_read_le16(data + 2);
    sample_count = nexos_read_le32(data + 4);
    if (sample_count > lumplen - 8 || sample_count <= 48) {
        W_ReleaseLumpNum(lumpnum);
        return false;
    }

    data += 16;
    sample_count -= 32;
    ok = nexos_expand_sound(sfxinfo, data + 8, sample_rate, sample_count);

    W_ReleaseLumpNum(lumpnum);
    return ok;
}

static boolean nexos_sound_ready(uint32_t *index_out) {
    struct syscall_audio_info info;
    uint32_t index;

    for (index = 0; index < 8u; index++) {
        if (audio_query(index, &info) &&
            info.present != 0u &&
            info.initialized != 0u &&
            (info.caps & NEX_AUDIO_CAP_PLAYBACK) != 0u) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return true;
        }
    }

    return false;
}

static boolean I_NexOS_InitSound(boolean use_sfx_prefix) {
    uint32_t index;

    nexos_use_sfx_prefix = use_sfx_prefix;
    memset(nexos_channels, 0, sizeof(nexos_channels));
    memset(nexos_channel_end_ms, 0, sizeof(nexos_channel_end_ms));

    if (!nexos_sound_ready(&index)) {
        fprintf(stderr, "NexOS audio: no initialized playback device\n");
        return false;
    }

    nexos_audio_index = index;
    nexos_sound_initialized = true;
    return true;
}

static void I_NexOS_ShutdownSound(void) {
    memset(nexos_channels, 0, sizeof(nexos_channels));
    memset(nexos_channel_end_ms, 0, sizeof(nexos_channel_end_ms));
    nexos_sound_initialized = false;
}

static int I_NexOS_GetSfxLumpNum(sfxinfo_t *sfx) {
    char namebuf[9];

    nexos_get_sfx_lump_name(sfx, namebuf, sizeof(namebuf));
    return W_GetNumForName(namebuf);
}

static void I_NexOS_UpdateSound(void) {
    int now;
    int channel;

    if (!nexos_sound_initialized) {
        return;
    }

    now = I_GetTimeMS();
    for (channel = 0; channel < NEXOS_SOUND_CHANNELS; channel++) {
        if (nexos_channels[channel] != NULL &&
            nexos_channel_end_ms[channel] <= now) {
            nexos_channels[channel] = NULL;
        }
    }
}

static void I_NexOS_UpdateSoundParams(int channel, int vol, int sep) {
    (void)channel;
    (void)vol;
    (void)sep;
}

static int I_NexOS_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep) {
    nexos_sound_t *sound;
    struct syscall_audio_play_info play;

    (void)vol;
    (void)sep;

    if (!nexos_sound_initialized ||
        channel < 0 ||
        channel >= NEXOS_SOUND_CHANNELS) {
        return -1;
    }

    if (sfxinfo->driver_data == NULL && !nexos_cache_sfx(sfxinfo)) {
        return -1;
    }

    sound = (nexos_sound_t *)sfxinfo->driver_data;
    memset(&play, 0, sizeof(play));
    play.sample_rate = NEXOS_SOUND_RATE;
    play.channels = 2u;
    play.bits_per_sample = 16u;
    play.bytes = sound->bytes;
    play.data_addr = (uint64_t)(uintptr_t)sound->data;
    play.flags = NEX_AUDIO_PLAY_F_ASYNC;

    if (!audio_play(nexos_audio_index, &play)) {
        return -1;
    }

    nexos_channels[channel] = sfxinfo;
    nexos_channel_end_ms[channel] =
        I_GetTimeMS() + (int)(((uint64_t)sound->frames * 1000u) /
                              NEXOS_SOUND_RATE) + 1;
    return channel;
}

static void I_NexOS_StopSound(int channel) {
    if (channel < 0 || channel >= NEXOS_SOUND_CHANNELS) {
        return;
    }
    nexos_channels[channel] = NULL;
    nexos_channel_end_ms[channel] = 0;
}

static boolean I_NexOS_SoundIsPlaying(int channel) {
    if (!nexos_sound_initialized ||
        channel < 0 ||
        channel >= NEXOS_SOUND_CHANNELS) {
        return false;
    }

    I_NexOS_UpdateSound();
    return nexos_channels[channel] != NULL;
}

static void I_NexOS_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {
    int i;

    for (i = 0; i < num_sounds; i++) {
        char namebuf[9];

        nexos_get_sfx_lump_name(&sounds[i], namebuf, sizeof(namebuf));
        sounds[i].lumpnum = W_CheckNumForName(namebuf);
        if (sounds[i].lumpnum != -1 && sounds[i].driver_data == NULL) {
            (void)nexos_cache_sfx(&sounds[i]);
        }
    }
}

static snddevice_t nexos_sound_devices[] = {
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32
};

sound_module_t DG_sound_module = {
    nexos_sound_devices,
    arrlen(nexos_sound_devices),
    I_NexOS_InitSound,
    I_NexOS_ShutdownSound,
    I_NexOS_GetSfxLumpNum,
    I_NexOS_UpdateSound,
    I_NexOS_UpdateSoundParams,
    I_NexOS_StartSound,
    I_NexOS_StopSound,
    I_NexOS_SoundIsPlaying,
    I_NexOS_PrecacheSounds
};

static boolean I_NexOS_InitMusic(void) {
    return true;
}

static void I_NexOS_ShutdownMusic(void) {
}

static void I_NexOS_SetMusicVolume(int volume) {
    (void)volume;
}

static void I_NexOS_PauseMusic(void) {
}

static void I_NexOS_ResumeMusic(void) {
}

static void *I_NexOS_RegisterSong(void *data, int len) {
    (void)data;
    (void)len;
    return NULL;
}

static void I_NexOS_UnRegisterSong(void *handle) {
    (void)handle;
}

static void I_NexOS_PlaySong(void *handle, boolean looping) {
    (void)handle;
    (void)looping;
}

static void I_NexOS_StopSong(void) {
}

static boolean I_NexOS_MusicIsPlaying(void) {
    return false;
}

static void I_NexOS_PollMusic(void) {
}

music_module_t DG_music_module = {
    nexos_sound_devices,
    arrlen(nexos_sound_devices),
    I_NexOS_InitMusic,
    I_NexOS_ShutdownMusic,
    I_NexOS_SetMusicVolume,
    I_NexOS_PauseMusic,
    I_NexOS_ResumeMusic,
    I_NexOS_RegisterSong,
    I_NexOS_UnRegisterSong,
    I_NexOS_PlaySong,
    I_NexOS_StopSong,
    I_NexOS_MusicIsPlaying,
    I_NexOS_PollMusic
};
