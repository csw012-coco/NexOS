#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

enum {
    WAV_PAGE_BYTES = 4096u,
    WAV_MAX_OUTPUT_FRAMES = 32u * 1024u
};

struct wav_format_info {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t data_bytes;
};

static void wav_free_pages(uint8_t *base, uint32_t page_count);
static int cmd_play_wav_like(int argc, char **argv, const char *verb);

static uint16_t wav_le16(const uint8_t *src) {
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t wav_le32(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static int wav_id_is(const uint8_t *src, const char *id) {
    return src[0] == (uint8_t)id[0] &&
           src[1] == (uint8_t)id[1] &&
           src[2] == (uint8_t)id[2] &&
           src[3] == (uint8_t)id[3];
}

static int wav_read_exact(int fd, uint8_t *dst, uint32_t size) {
    uint32_t done = 0;

    while (done < size) {
        uint32_t got = (uint32_t)read(fd, (char *)dst + done, size - done);

        if (got == 0u) {
            return 0;
        }
        done += got;
    }
    return 1;
}

static int wav_skip_bytes(int fd, uint32_t size, uint8_t *scratch, uint32_t scratch_size) {
    uint32_t remaining = size;

    while (remaining != 0u) {
        uint32_t chunk = remaining > scratch_size ? scratch_size : remaining;
        uint32_t got = (uint32_t)read(fd, (char *)scratch, chunk);

        if (got == 0u) {
            return 0;
        }
        remaining -= got;
    }
    return 1;
}

static int wav_find_default_device(uint32_t *index_out) {
    struct syscall_audio_info info;
    uint32_t index = 0;

    while (audio_query(index, &info) > 0 && info.present) {
        if ((info.caps & NEX_AUDIO_CAP_PLAYBACK) != 0u) {
            *index_out = index;
            return 1;
        }
        index++;
    }
    return 0;
}

static int wav_parse_header(int fd, struct wav_format_info *out) {
    uint8_t riff_header[12];
    uint8_t chunk_header[8];
    uint8_t fmt_data[16];
    uint8_t scratch[64];
    int fmt_found = 0;

    if (!wav_read_exact(fd, riff_header, sizeof(riff_header))) {
        return 0;
    }
    if (!wav_id_is(riff_header, "RIFF") || !wav_id_is(riff_header + 8, "WAVE")) {
        return 0;
    }

    out->sample_rate = 0;
    out->channels = 0;
    out->bits_per_sample = 0;
    out->data_bytes = 0;

    for (;;) {
        uint32_t chunk_size;

        if (!wav_read_exact(fd, chunk_header, sizeof(chunk_header))) {
            return 0;
        }
        chunk_size = wav_le32(chunk_header + 4);

        if (wav_id_is(chunk_header, "fmt ")) {
            if (chunk_size < sizeof(fmt_data)) {
                return 0;
            }
            if (!wav_read_exact(fd, fmt_data, sizeof(fmt_data))) {
                return 0;
            }
            if (wav_le16(fmt_data + 0) != 1u) {
                return 0;
            }
            out->channels = wav_le16(fmt_data + 2);
            out->sample_rate = wav_le32(fmt_data + 4);
            out->bits_per_sample = wav_le16(fmt_data + 14);
            fmt_found = 1;
            if (chunk_size > sizeof(fmt_data) &&
                !wav_skip_bytes(fd, chunk_size - sizeof(fmt_data), scratch, sizeof(scratch))) {
                return 0;
            }
        } else if (wav_id_is(chunk_header, "data")) {
            if (!fmt_found) {
                return 0;
            }
            out->data_bytes = chunk_size;
            return 1;
        } else if (!wav_skip_bytes(fd, chunk_size, scratch, sizeof(scratch))) {
            return 0;
        }

        if ((chunk_size & 1u) != 0u && !wav_skip_bytes(fd, 1u, scratch, sizeof(scratch))) {
            return 0;
        }
    }
}

static uint8_t *wav_alloc_pages(uint32_t page_count) {
    uint64_t base = 0;
    uint32_t i;

    for (i = 0; i < page_count; i++) {
        uint64_t page = page_alloc();

        if (page == 0u) {
            wav_free_pages((uint8_t *)(uintptr_t)base, i);
            return 0;
        }
        if (i == 0u) {
            base = page;
        } else if (page != base + ((uint64_t)i * WAV_PAGE_BYTES)) {
            (void)page_free(page);
            wav_free_pages((uint8_t *)(uintptr_t)base, i);
            return 0;
        }
    }
    return (uint8_t *)(uintptr_t)base;
}

static void wav_free_pages(uint8_t *base, uint32_t page_count) {
    uint32_t i;

    if (base == 0) {
        return;
    }
    for (i = 0; i < page_count; i++) {
        (void)page_free((uint64_t)(uintptr_t)base + ((uint64_t)i * WAV_PAGE_BYTES));
    }
}

static int cmd_play_wav_like(int argc, char **argv, const char *verb) {
    struct wav_format_info fmt;
    struct syscall_audio_play_info play;
    uint8_t *buffer = 0;
    uint32_t device_index = 0;
    uint32_t page_count;
    uint32_t src_frame_bytes;
    uint32_t chunk_bytes;
    uint32_t remaining;
    int fd;

    if (argc < 2 || argc > 3) {
        write_err_usage(verb, " <path> [device]\n");
        return 1;
    }
    if (argc >= 3) {
        if (!parse_u32_local(argv[2], &device_index)) {
            write_err_usage(verb, " <path> [device]\n");
            return 1;
        }
    } else if (!wav_find_default_device(&device_index)) {
        write_err_str(verb);
        write_err_str(": no playback device\n");
        return 1;
    }

    fd = open(argv[1], 0);
    if (fd < 0) {
        write_err_str(verb);
        write_err_str(": open failed\n");
        return 1;
    }
    if (!wav_parse_header(fd, &fmt)) {
        close((uint32_t)fd);
        write_err_str(verb);
        write_err_str(": unsupported or invalid wav file\n");
        return 1;
    }
    if ((fmt.channels != 1u && fmt.channels != 2u) ||
        (fmt.bits_per_sample != 8u && fmt.bits_per_sample != 16u) ||
        fmt.sample_rate == 0u) {
        close((uint32_t)fd);
        write_err_str(verb);
        write_err_str(": only pcm 8/16-bit mono/stereo is supported\n");
        return 1;
    }

    src_frame_bytes = fmt.channels * (fmt.bits_per_sample / 8u);
    chunk_bytes = WAV_MAX_OUTPUT_FRAMES * src_frame_bytes;
    page_count = (chunk_bytes + WAV_PAGE_BYTES - 1u) / WAV_PAGE_BYTES;
    buffer = wav_alloc_pages(page_count);
    if (buffer == 0) {
        close((uint32_t)fd);
        write_err_str(verb);
        write_err_str(": buffer allocation failed\n");
        return 1;
    }

    remaining = fmt.data_bytes;
    while (remaining != 0u) {
        uint32_t want = remaining > chunk_bytes ? chunk_bytes : remaining;
        uint32_t got = (uint32_t)read(fd, (char *)buffer, want);
        uint32_t play_bytes;

        if (got == 0u) {
            wav_free_pages(buffer, page_count);
            close((uint32_t)fd);
            write_err_str(verb);
            write_err_str(": unexpected end of file\n");
            return 1;
        }
        play_bytes = got - (got % src_frame_bytes);
        if (play_bytes != 0u) {
            play.sample_rate = fmt.sample_rate;
            play.channels = fmt.channels;
            play.bits_per_sample = fmt.bits_per_sample;
            play.bytes = play_bytes;
            play.data_addr = (uint64_t)(uintptr_t)buffer;
            if (audio_play(device_index, &play) <= 0) {
                wav_free_pages(buffer, page_count);
                close((uint32_t)fd);
                write_err_str(verb);
                write_err_str(": playback failed\n");
                return 1;
            }
        }
        if (got >= remaining) {
            remaining = 0u;
        } else {
            remaining -= got;
        }
    }

    wav_free_pages(buffer, page_count);
    close((uint32_t)fd);
    return 0;
}

int cmd_wav(int argc, char **argv) {
    return cmd_play_wav_like(argc, argv, "wav");
}

int cmd_mplay(int argc, char **argv) {
    return cmd_play_wav_like(argc, argv, "mplay");
}

int cmd_audio(int argc, char **argv) {
    struct syscall_audio_info info;
    uint32_t index = 0;
    int printed = 0;

    (void)argv;
    if (argc > 1) {
        write_err_usage("audio", "\n");
        return 1;
    }
    while (audio_query(index, &info) > 0 && info.present) {
        write_str("card ");
        write_dec(index);
        write_str(": ");
        write_str(info.name[0] != '\0' ? info.name : "audio");
        write_str(" rate=");
        write_dec(info.sample_rate);
        write_str("Hz ch=");
        write_dec(info.channels);
        write_str(" bits=");
        write_dec(info.bits_per_sample);
        write_str(" caps=");
        if ((info.caps & NEX_AUDIO_CAP_PLAYBACK) != 0u) {
            write_str("playback");
        }
        if ((info.caps & NEX_AUDIO_CAP_TONE) != 0u) {
            write_str((info.caps & NEX_AUDIO_CAP_PLAYBACK) != 0u ? ",tone" : "tone");
        }
        write_str(" driver=");
        if (info.driver_kind == NEX_AUDIO_DRIVER_AC97) {
            write_str("ac97");
        } else {
            write_str("unknown");
        }
        write_str(" init=");
        write_dec(info.initialized);
        write_str("\n");
        printed = 1;
        index++;
    }
    if (!printed) {
        write_str("audio: no devices\n");
    }
    return 0;
}

int cmd_tone(int argc, char **argv) {
    uint32_t hz = 440u;
    uint32_t duration_ms = 150u;
    uint32_t device_index = 0u;
    struct syscall_audio_info info;

    if (argc > 4) {
        write_err_usage("tone", " [hz] [ms] [device]\n");
        return 1;
    }
    if (argc >= 2 && !parse_u32_local(argv[1], &hz)) {
        write_err_usage("tone", " [hz] [ms] [device]\n");
        return 1;
    }
    if (argc >= 3 && !parse_u32_local(argv[2], &duration_ms)) {
        write_err_usage("tone", " [hz] [ms] [device]\n");
        return 1;
    }
    if (argc >= 4 && !parse_u32_local(argv[3], &device_index)) {
        write_err_usage("tone", " [hz] [ms] [device]\n");
        return 1;
    }
    if (audio_query(device_index, &info) <= 0 || !info.present) {
        write_err_str("tone: audio device not found\n");
        return 1;
    }
    if ((info.caps & NEX_AUDIO_CAP_TONE) == 0u) {
        write_err_str("tone: device does not support tone playback\n");
        return 1;
    }
    if (audio_tone(device_index, hz, duration_ms) <= 0) {
        write_err_str("tone: playback failed\n");
        return 1;
    }
    return 0;
}
