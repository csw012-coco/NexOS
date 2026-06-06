#pragma once

#include <stdint.h>

enum {
    AUDIO_CAP_PLAYBACK = 1u << 0,
    AUDIO_CAP_TONE = 1u << 1,
    AUDIO_CAP_STREAM = 1u << 2
};

enum {
    AUDIO_DRIVER_NONE = 0,
    AUDIO_DRIVER_AC97 = 1,
    AUDIO_DRIVER_HDA = 2
};

enum {
    AUDIO_PLAY_F_ASYNC = 1u << 0
};

struct audio_pcm_stream {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t data_bytes;
    uint32_t flags;
    void *ctx;
    uint32_t (*read)(void *ctx, void *buffer, uint32_t bytes);
    uint32_t (*cancelled)(void *ctx);
};

struct audio_device_info {
    uint32_t present;
    uint32_t initialized;
    uint32_t caps;
    uint32_t driver_kind;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    char name[32];
};

struct audio_device_ops {
    int (*play_tone)(void *ctx, uint32_t hz, uint32_t duration_ms);
    int (*play_pcm)(void *ctx,
                    const void *data,
                    uint32_t bytes,
                    uint32_t sample_rate,
                    uint32_t channels,
                    uint32_t bits_per_sample,
                    uint32_t flags);
    int (*play_stream)(void *ctx, struct audio_pcm_stream *stream);
};

int audio_register_device(const struct audio_device_info *info,
                          const struct audio_device_ops *ops,
                          void *ctx,
                          uint32_t *index_out);
uint32_t audio_device_count(void);
int audio_query_device(uint32_t index, struct audio_device_info *out);
int audio_default_output_device(uint32_t *index_out);
int audio_play_tone(uint32_t index, uint32_t hz, uint32_t duration_ms);
int audio_play_pcm(uint32_t index,
                   const void *data,
                   uint32_t bytes,
                   uint32_t sample_rate,
                   uint32_t channels,
                   uint32_t bits_per_sample,
                   uint32_t flags);
int audio_play_stream(uint32_t index, struct audio_pcm_stream *stream);
