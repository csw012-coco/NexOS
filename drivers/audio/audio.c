#include "drivers/audio/audio.h"

enum {
    AUDIO_MAX_DEVICES = 4u
};

struct audio_slot {
    uint8_t used;
    struct audio_device_info info;
    const struct audio_device_ops *ops;
    void *ctx;
};

static struct audio_slot g_audio_slots[AUDIO_MAX_DEVICES];

static void audio_copy_name(char dst[32], const char *src) {
    uint32_t i = 0;

    if (dst == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < 32u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int audio_register_device(const struct audio_device_info *info,
                          const struct audio_device_ops *ops,
                          void *ctx,
                          uint32_t *index_out) {
    uint32_t index;

    if (info == 0) {
        return 0;
    }

    for (index = 0; index < AUDIO_MAX_DEVICES; index++) {
        if (g_audio_slots[index].used) {
            continue;
        }
        g_audio_slots[index].used = 1u;
        g_audio_slots[index].info = *info;
        audio_copy_name(g_audio_slots[index].info.name, info->name);
        g_audio_slots[index].ops = ops;
        g_audio_slots[index].ctx = ctx;
        if (index_out != 0) {
            *index_out = index;
        }
        return 1;
    }

    return 0;
}

uint32_t audio_device_count(void) {
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < AUDIO_MAX_DEVICES; index++) {
        if (g_audio_slots[index].used) {
            count++;
        }
    }
    return count;
}

int audio_query_device(uint32_t index, struct audio_device_info *out) {
    if (out == 0 || index >= AUDIO_MAX_DEVICES || !g_audio_slots[index].used) {
        return 0;
    }
    *out = g_audio_slots[index].info;
    return 1;
}

int audio_default_output_device(uint32_t *index_out) {
    uint32_t index;

    for (index = 0; index < AUDIO_MAX_DEVICES; index++) {
        if (!g_audio_slots[index].used) {
            continue;
        }
        if ((g_audio_slots[index].info.caps & AUDIO_CAP_PLAYBACK) == 0) {
            continue;
        }
        if (index_out != 0) {
            *index_out = index;
        }
        return 1;
    }
    return 0;
}

int audio_play_tone(uint32_t index, uint32_t hz, uint32_t duration_ms) {
    if (index >= AUDIO_MAX_DEVICES || !g_audio_slots[index].used) {
        return 0;
    }
    if (g_audio_slots[index].ops == 0 || g_audio_slots[index].ops->play_tone == 0) {
        return 0;
    }
    return g_audio_slots[index].ops->play_tone(g_audio_slots[index].ctx, hz, duration_ms);
}

int audio_play_pcm(uint32_t index,
                   const void *data,
                   uint32_t bytes,
                   uint32_t sample_rate,
                   uint32_t channels,
                   uint32_t bits_per_sample,
                   uint32_t flags) {
    if (index >= AUDIO_MAX_DEVICES || !g_audio_slots[index].used) {
        return 0;
    }
    if (g_audio_slots[index].ops == 0 || g_audio_slots[index].ops->play_pcm == 0) {
        return 0;
    }
    return g_audio_slots[index].ops->play_pcm(g_audio_slots[index].ctx,
                                              data,
                                              bytes,
                                              sample_rate,
                                              channels,
                                              bits_per_sample,
                                              flags);
}

int audio_play_stream(uint32_t index, struct audio_pcm_stream *stream) {
    if (index >= AUDIO_MAX_DEVICES || !g_audio_slots[index].used) {
        return 0;
    }
    if (stream == 0 || g_audio_slots[index].ops == 0 ||
        g_audio_slots[index].ops->play_stream == 0) {
        return 0;
    }
    return g_audio_slots[index].ops->play_stream(g_audio_slots[index].ctx, stream);
}
