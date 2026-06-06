#include "kernel/public/driver/driver_module.h"

#define AC97_DMA_MAX_PHYS 0x100000000ull

enum {
    PCI_COMMAND_IO = 1u << 0,
    PCI_COMMAND_BUS_MASTER = 1u << 2,
    AC97_PCI_CLASS_MULTIMEDIA = 0x04u,
    AC97_PCI_SUBCLASS_AUDIO = 0x01u,
    AC97_PCI_COMMAND_OFFSET = 0x04u,
    AC97_MIXER_RESET = 0x00u,
    AC97_MASTER_VOLUME = 0x02u,
    AC97_PCM_OUT_VOLUME = 0x18u,
    AC97_POWERDOWN = 0x26u,
    AC97_EXT_AUDIO_ID = 0x28u,
    AC97_EXT_AUDIO_CTRL = 0x2au,
    AC97_PCM_FRONT_RATE = 0x2cu,
    AC97_CODEC_VENDOR_ID1 = 0x7cu,
    AC97_CODEC_VENDOR_ID2 = 0x7eu,
    AC97_GLOB_CNT = 0x2cu,
    AC97_GLOB_STA = 0x30u,
    AC97_GLOB_CNT_COLD = 0x00000002u,
    AC97_GLOB_STA_PCR = 0x00000100u,
    AC97_PO_BDBAR = 0x10u,
    AC97_PO_CIV = 0x14u,
    AC97_PO_LVI = 0x15u,
    AC97_PO_SR = 0x16u,
    AC97_PO_CR = 0x1bu,
    AC97_PO_SR_DCH = 0x01u,
    AC97_PO_SR_CELV = 0x02u,
    AC97_PO_SR_CLEAR = 0x1cu,
    AC97_PO_CR_START = 0x01u,
    AC97_PO_CR_RESET = 0x02u,
    AC97_BDL_FLAGS_NONE = 0x0000u,
    AC97_BDL_FLAG_BUP = 0x4000u,
    AC97_SAMPLE_RATE = 48000u,
    AC97_OUTPUT_BYTES_PER_SECOND = AC97_SAMPLE_RATE * 2u * 2u,
    AC97_PAGE_BYTES = 4096u,
    AC97_BUFFER_PAGES = 16u,
    AC97_BUFFER_BYTES = AC97_PAGE_BYTES * AC97_BUFFER_PAGES,
    AC97_BUFFER_FRAMES = AC97_BUFFER_BYTES / 4u,
    AC97_BDL_ENTRIES = 32u,
    AC97_STREAM_READ_PAGES = 256u
};

struct ac97_mod_bdl_entry {
    uint32_t addr;
    uint16_t samples;
    uint16_t flags;
} __attribute__((packed));

struct ac97_mod_state {
    uint8_t initialized;
    uint8_t audio_registered;
    uint16_t nambar;
    uint16_t nabmbar;
    uint32_t codec_id;
    uint32_t global_status;
    uint64_t bdl_phys;
    uint64_t buffer_phys[AC97_BDL_ENTRIES];
    struct ac97_mod_bdl_entry *bdl;
    uint8_t *buffers[AC97_BDL_ENTRIES];
    uint32_t buffer_count;
    uint8_t stream_active;
    uint8_t stream_last_civ;
    void *stream_cancel_ctx;
    uint32_t (*stream_cancelled)(void *ctx);
};

static struct ac97_mod_state g_ac97_mod;

static void (*volatile ac97_mod_log)(const char *fmt, ...) = driver_log;

static uint16_t ac97_mod_io_base_from_bar_local(uint32_t bar) {
    if ((bar & 0x1u) == 0u) {
        return 0;
    }
    return (uint16_t)(bar & 0xfffcu);
}

static uint16_t ac97_mod_mixer_read16_local(uint16_t reg) {
    return driver_io_in16((uint16_t)(g_ac97_mod.nambar + reg));
}

static void ac97_mod_mixer_write16_local(uint16_t reg, uint16_t value) {
    driver_io_out16((uint16_t)(g_ac97_mod.nambar + reg), value);
}

static uint8_t ac97_mod_bus_read8_local(uint16_t reg) {
    return driver_io_in8((uint16_t)(g_ac97_mod.nabmbar + reg));
}

static uint16_t ac97_mod_bus_read16_local(uint16_t reg) {
    return driver_io_in16((uint16_t)(g_ac97_mod.nabmbar + reg));
}

static uint32_t ac97_mod_bus_read32_local(uint16_t reg) {
    return driver_io_in32((uint16_t)(g_ac97_mod.nabmbar + reg));
}

static void ac97_mod_bus_write8_local(uint16_t reg, uint8_t value) {
    driver_io_out8((uint16_t)(g_ac97_mod.nabmbar + reg), value);
}

static void ac97_mod_bus_write16_local(uint16_t reg, uint16_t value) {
    driver_io_out16((uint16_t)(g_ac97_mod.nabmbar + reg), value);
}

static void ac97_mod_bus_write32_local(uint16_t reg, uint32_t value) {
    driver_io_out32((uint16_t)(g_ac97_mod.nabmbar + reg), value);
}

static void ac97_mod_delay_local(uint32_t spins) {
    volatile uint32_t i;

    for (i = 0; i < spins; i++) {
        __asm__ __volatile__("" ::: "memory");
    }
}

static void ac97_mod_flush_cache_local(void) {
    __asm__ __volatile__("wbinvd" ::: "memory");
}

static int ac97_mod_wait_codec_ready_local(void) {
    uint32_t spins;

    for (spins = 0; spins < 2000000u; spins++) {
        if ((ac97_mod_bus_read32_local(AC97_GLOB_STA) & AC97_GLOB_STA_PCR) != 0u) {
            return 1;
        }
    }
    return 0;
}

static int ac97_mod_wait_channel_reset_local(void) {
    uint32_t spins;

    for (spins = 0; spins < 1000000u; spins++) {
        if ((ac97_mod_bus_read8_local(AC97_PO_CR) & AC97_PO_CR_RESET) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int ac97_mod_wait_channel_done_local(uint32_t duration_ms) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t wait_ticks;
    uint32_t start_ticks;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    wait_ticks = ((duration_ms + 39u) * timer_hz + 999u) / 1000u;
    if (wait_ticks < 2u) {
        wait_ticks = 2u;
    }
    start_ticks = driver_timer_current_ticks();
    while ((uint32_t)(driver_timer_current_ticks() - start_ticks) < wait_ticks) {
        ac97_mod_delay_local(20000u);
    }
    return 1;
}

static void ac97_mod_stop_channel_local(void) {
    ac97_mod_bus_write8_local(AC97_PO_CR, 0u);
    ac97_mod_bus_write16_local(AC97_PO_SR, AC97_PO_SR_CLEAR);
}

static void ac97_mod_reset_stream_local(void) {
    ac97_mod_stop_channel_local();
    g_ac97_mod.stream_active = 0u;
    g_ac97_mod.stream_last_civ = 0u;
    g_ac97_mod.stream_cancel_ctx = NULL;
    g_ac97_mod.stream_cancelled = NULL;
}

static int ac97_mod_stream_cancelled_local(void) {
    return g_ac97_mod.stream_cancelled != NULL &&
           g_ac97_mod.stream_cancelled(g_ac97_mod.stream_cancel_ctx) != 0u;
}

static int ac97_mod_dma_phys32_local(uint64_t phys, uint32_t *out) {
    if (out == NULL) {
        return 0;
    }
    if ((phys >> 32) != 0u) {
        ac97_mod_log("driver: AC97MOD dma phys above 32-bit phys=%lx\n", phys);
        *out = 0u;
        return 0;
    }
    *out = (uint32_t)phys;
    return 1;
}

static int ac97_mod_prepare_dma_local(void) {
    if (g_ac97_mod.bdl == NULL) {
        uint32_t phys32;

        g_ac97_mod.bdl = (struct ac97_mod_bdl_entry *)driver_alloc_pages_below(1u,
                                                                               AC97_DMA_MAX_PHYS,
                                                                               &g_ac97_mod.bdl_phys);
        if (g_ac97_mod.bdl == NULL) {
            ac97_mod_log("driver: AC97MOD bdl allocation failed\n");
            return 0;
        }
        if (!ac97_mod_dma_phys32_local(g_ac97_mod.bdl_phys, &phys32)) {
            driver_free_pages(g_ac97_mod.bdl, 1u);
            g_ac97_mod.bdl = NULL;
            g_ac97_mod.bdl_phys = 0u;
            return 0;
        }
    }
    return 1;
}

static int ac97_mod_prepare_buffer_pages_local(uint32_t page_count) {
    if (page_count == 0u || page_count > AC97_BDL_ENTRIES) {
        return 0;
    }
    while (g_ac97_mod.buffer_count < page_count) {
        uint64_t phys;
        uint32_t phys32;
        uint8_t *buffer = (uint8_t *)driver_alloc_pages_below(AC97_BUFFER_PAGES,
                                                              AC97_DMA_MAX_PHYS,
                                                              &phys);

        if (buffer == NULL) {
            ac97_mod_log("driver: AC97MOD buffer allocation failed count=%u need=%u\n",
                         g_ac97_mod.buffer_count,
                         page_count);
            return 0;
        }
        if (!ac97_mod_dma_phys32_local(phys, &phys32)) {
            driver_free_pages(buffer, AC97_BUFFER_PAGES);
            return 0;
        }
        g_ac97_mod.buffer_phys[g_ac97_mod.buffer_count] = phys;
        g_ac97_mod.buffers[g_ac97_mod.buffer_count] = buffer;
        g_ac97_mod.buffer_count++;
    }
    return 1;
}

static int16_t *ac97_mod_tone_buffer_local(void) {
    if (g_ac97_mod.buffers[0] == NULL) {
        return NULL;
    }
    return (int16_t *)g_ac97_mod.buffers[0];
}

static uint32_t ac97_mod_generate_tone_local(uint32_t hz) {
    int16_t *buffer = ac97_mod_tone_buffer_local();
    uint32_t period_frames;
    uint32_t half_period;
    uint32_t usable_frames;
    uint32_t i;

    if (buffer == NULL) {
        return 0;
    }
    if (hz < 40u) {
        hz = 40u;
    }
    if (hz > 4000u) {
        hz = 4000u;
    }
    period_frames = AC97_SAMPLE_RATE / hz;
    if (period_frames < 2u) {
        period_frames = 2u;
    }
    if (period_frames > AC97_BUFFER_FRAMES) {
        period_frames = AC97_BUFFER_FRAMES;
    }
    usable_frames = AC97_BUFFER_FRAMES - (AC97_BUFFER_FRAMES % period_frames);
    if (usable_frames == 0u) {
        usable_frames = period_frames;
    }
    half_period = period_frames / 2u;
    if (half_period == 0u) {
        half_period = 1u;
    }
    for (i = 0; i < usable_frames; i++) {
        int16_t sample = (i % period_frames) < half_period ? 0x2800 : (int16_t)-0x2800;

        buffer[i * 2u] = sample;
        buffer[i * 2u + 1u] = sample;
    }
    for (; i < AC97_BUFFER_FRAMES; i++) {
        buffer[i * 2u] = 0;
        buffer[i * 2u + 1u] = 0;
    }
    return usable_frames;
}

static void ac97_mod_clear_bdl_local(uint32_t start) {
    while (start < AC97_BDL_ENTRIES) {
        g_ac97_mod.bdl[start].addr = 0;
        g_ac97_mod.bdl[start].samples = 0;
        g_ac97_mod.bdl[start].flags = 0;
        start++;
    }
}

static uint32_t ac97_mod_prepare_repeated_bdl_local(uint32_t total_frames,
                                                    uint32_t chunk_frames) {
    uint32_t remaining = total_frames;
    uint32_t count = 0;

    while (count < AC97_BDL_ENTRIES) {
        uint32_t frames = remaining > chunk_frames ? chunk_frames : remaining;

        if (frames == 0u) {
            break;
        }
        g_ac97_mod.bdl[count].addr = (uint32_t)g_ac97_mod.buffer_phys[0];
        g_ac97_mod.bdl[count].samples = (uint16_t)(frames * 2u);
        g_ac97_mod.bdl[count].flags = AC97_BDL_FLAGS_NONE;
        remaining -= frames;
        count++;
    }
    ac97_mod_clear_bdl_local(count);
    return total_frames == 0u ? 0u : (total_frames + chunk_frames - 1u) / chunk_frames;
}

static int ac97_mod_configure_output_rate_local(uint32_t sample_rate) {
    uint16_t ext_audio_id;

    if (sample_rate == 0u || sample_rate > AC97_SAMPLE_RATE) {
        return 0;
    }
    if (sample_rate != AC97_SAMPLE_RATE && sample_rate < 8000u) {
        return 0;
    }
    ext_audio_id = ac97_mod_mixer_read16_local(AC97_EXT_AUDIO_ID);
    if ((ext_audio_id & 0x0001u) != 0u) {
        ac97_mod_mixer_write16_local(AC97_EXT_AUDIO_CTRL, 0x0001u);
        ac97_mod_mixer_write16_local(AC97_PCM_FRONT_RATE, (uint16_t)sample_rate);
        return 1;
    }
    if (sample_rate != AC97_SAMPLE_RATE) {
        ac97_mod_log("driver: AC97MOD sample rate %u unsupported ext=%x\n",
                     sample_rate,
                     (uint32_t)ext_audio_id);
    }
    return sample_rate == AC97_SAMPLE_RATE;
}

static int ac97_mod_start_and_wait_local(uint32_t descriptors, uint32_t duration_ms) {
    ac97_mod_stop_channel_local();
    ac97_mod_bus_write8_local(AC97_PO_CR, AC97_PO_CR_RESET);
    if (!ac97_mod_wait_channel_reset_local()) {
        return 0;
    }
    ac97_mod_flush_cache_local();
    ac97_mod_bus_write32_local(AC97_PO_BDBAR, (uint32_t)g_ac97_mod.bdl_phys);
    ac97_mod_bus_write8_local(AC97_PO_LVI, (uint8_t)(descriptors - 1u));
    ac97_mod_bus_write16_local(AC97_PO_SR, AC97_PO_SR_CLEAR);
    ac97_mod_bus_write8_local(AC97_PO_CR, AC97_PO_CR_START);
    if (!ac97_mod_wait_channel_done_local(duration_ms)) {
        ac97_mod_stop_channel_local();
        return 0;
    }
    ac97_mod_stop_channel_local();
    return 1;
}

static int ac97_mod_play_tone_local(void *ctx, uint32_t hz, uint32_t duration_ms) {
    uint32_t chunk_frames;
    uint32_t total_frames;
    uint32_t max_frames;
    uint32_t effective_duration_ms;
    uint32_t descriptors;

    (void)ctx;
    ac97_mod_reset_stream_local();
    if (!g_ac97_mod.initialized ||
        !ac97_mod_prepare_dma_local() ||
        !ac97_mod_prepare_buffer_pages_local(1u)) {
        return 0;
    }
    if (duration_ms == 0u) {
        duration_ms = 150u;
    }
    if (!ac97_mod_configure_output_rate_local(AC97_SAMPLE_RATE)) {
        return 0;
    }
    chunk_frames = ac97_mod_generate_tone_local(hz);
    if (chunk_frames == 0u) {
        return 0;
    }
    total_frames = (uint32_t)(((uint64_t)AC97_SAMPLE_RATE * (uint64_t)duration_ms) / 1000ull);
    if (total_frames == 0u) {
        total_frames = chunk_frames;
    }
    max_frames = chunk_frames * AC97_BDL_ENTRIES;
    if (total_frames > max_frames) {
        total_frames = max_frames;
    }
    effective_duration_ms = (uint32_t)(((uint64_t)total_frames * 1000ull +
                                        (AC97_SAMPLE_RATE - 1u)) /
                                       (uint64_t)AC97_SAMPLE_RATE);
    descriptors = ac97_mod_prepare_repeated_bdl_local(total_frames, chunk_frames);
    if (descriptors == 0u || descriptors > AC97_BDL_ENTRIES) {
        return 0;
    }
    return ac97_mod_start_and_wait_local(descriptors, effective_duration_ms);
}

static int16_t ac97_mod_decode_u8_sample_local(uint8_t value) {
    return (int16_t)(((int32_t)value - 128) << 8);
}

static int16_t ac97_mod_decode_le16_sample_local(const uint8_t *src) {
    return (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static void ac97_mod_write_stereo_page_frame_local(uint32_t page_index,
                                                   uint32_t page_frame,
                                                   int16_t left,
                                                   int16_t right) {
    int16_t *dst = (int16_t *)g_ac97_mod.buffers[page_index];

    dst[page_frame * 2u] = left;
    dst[page_frame * 2u + 1u] = right;
}

struct ac97_mod_fill_result {
    uint16_t samples;
    uint8_t end;
};

struct ac97_mod_stream_source {
    struct driver_audio_pcm_stream *stream;
    uint8_t *buffer;
    uint32_t capacity;
    uint32_t src_frame_bytes;
    uint32_t remaining_bytes;
    uint32_t total_frames;
    uint32_t buffer_base_frame;
    uint32_t buffer_frames;
    uint8_t eof;
};

static void ac97_mod_zero_descriptor_local(uint32_t index) {
    uint32_t *dst = (uint32_t *)g_ac97_mod.buffers[index];
    uint32_t dwords = AC97_BUFFER_BYTES / sizeof(uint32_t);
    uint32_t i;

    for (i = 0; i < dwords; i++) {
        dst[i] = 0u;
    }
}

static int ac97_mod_stream_source_read_next_local(struct ac97_mod_stream_source *source) {
    uint32_t want;
    uint32_t got;
    uint32_t usable;

    if (source == NULL || source->stream == NULL || source->stream->read == NULL ||
        source->buffer == NULL || source->capacity == 0u || source->src_frame_bytes == 0u) {
        return 0;
    }
    if (ac97_mod_stream_cancelled_local()) {
        source->eof = 1u;
        return 0;
    }
    source->buffer_base_frame += source->buffer_frames;
    source->buffer_frames = 0u;
    if (source->remaining_bytes == 0u) {
        source->eof = 1u;
        return 0;
    }
    want = source->remaining_bytes > source->capacity ?
        source->capacity :
        source->remaining_bytes;
    want -= want % source->src_frame_bytes;
    if (want == 0u) {
        source->eof = 1u;
        return 0;
    }
    got = source->stream->read(source->stream->ctx, source->buffer, want);
    if (got == 0u) {
        source->eof = 1u;
        return 0;
    }
    if (got > want) {
        got = want;
    }
    if (got >= source->remaining_bytes) {
        source->remaining_bytes = 0u;
    } else {
        source->remaining_bytes -= got;
    }
    usable = got - (got % source->src_frame_bytes);
    if (usable == 0u) {
        source->eof = 1u;
        return 0;
    }
    source->buffer_frames = usable / source->src_frame_bytes;
    return 1;
}

static int ac97_mod_stream_source_ensure_frame_local(struct ac97_mod_stream_source *source,
                                                     uint32_t frame) {
    if (source == NULL) {
        return 0;
    }
    while (frame >= source->buffer_base_frame + source->buffer_frames) {
        if (source->eof != 0u ||
            !ac97_mod_stream_source_read_next_local(source)) {
            return 0;
        }
    }
    return 1;
}

static int16_t ac97_mod_stream_source_sample_local(struct ac97_mod_stream_source *source,
                                                   uint32_t frame,
                                                   uint32_t channel) {
    const uint8_t *src;
    uint32_t local_frame;
    uint32_t sample_bytes;
    uint32_t offset;

    if (!ac97_mod_stream_source_ensure_frame_local(source, frame)) {
        return 0;
    }
    if (source->stream->channels == 0u) {
        return 0;
    }
    if (channel >= source->stream->channels) {
        channel = 0u;
    }
    sample_bytes = source->stream->bits_per_sample / 8u;
    local_frame = frame - source->buffer_base_frame;
    offset = local_frame * source->src_frame_bytes + channel * sample_bytes;
    src = source->buffer + offset;
    if (source->stream->bits_per_sample == 8u) {
        return ac97_mod_decode_u8_sample_local(src[0]);
    }
    return ac97_mod_decode_le16_sample_local(src);
}

static struct ac97_mod_fill_result ac97_mod_fill_pcm_descriptor_local(
    uint32_t index,
    const uint8_t *src,
    uint32_t input_frames,
    uint64_t *src_pos,
    uint64_t src_step,
    uint32_t channels,
    uint32_t bits_per_sample) {
    struct ac97_mod_fill_result result;
    uint32_t src_stride = channels * (bits_per_sample / 8u);
    uint32_t frames_written = 0u;
    uint32_t frame;

    result.samples = 0u;
    result.end = 0u;
    if (src == NULL || src_pos == NULL || src_stride == 0u || input_frames == 0u) {
        result.end = 1u;
        return result;
    }
    for (frame = 0; frame < AC97_BUFFER_FRAMES; frame++) {
        uint32_t src_frame = (uint32_t)(*src_pos >> 32);
        uint32_t src_offset;
        int16_t left;
        int16_t right;

        if (src_frame >= input_frames) {
            ac97_mod_write_stereo_page_frame_local(index, frame, 0, 0);
            result.end = 1u;
            continue;
        }
        src_offset = src_frame * src_stride;
        if (bits_per_sample == 8u) {
            left = ac97_mod_decode_u8_sample_local(src[src_offset]);
            right = channels == 1u ? left :
                ac97_mod_decode_u8_sample_local(src[src_offset + 1u]);
        } else {
            left = ac97_mod_decode_le16_sample_local(src + src_offset);
            right = channels == 1u ? left :
                ac97_mod_decode_le16_sample_local(src + src_offset + 2u);
        }
        ac97_mod_write_stereo_page_frame_local(index, frame, left, right);
        *src_pos += src_step;
        frames_written++;
    }
    if ((uint32_t)(*src_pos >> 32) >= input_frames) {
        result.end = 1u;
    }
    result.samples = (uint16_t)(frames_written * 2u);
    if (result.samples == 0u) {
        result.samples = 2u;
    }
    return result;
}

static struct ac97_mod_fill_result ac97_mod_fill_stream_descriptor_local(
    uint32_t index,
    struct ac97_mod_stream_source *source,
    uint64_t *src_pos,
    uint64_t src_step) {
    struct ac97_mod_fill_result result;
    uint32_t frames_written = 0u;
    uint32_t frame;

    result.samples = 0u;
    result.end = 0u;
    if (source == NULL || src_pos == NULL) {
        result.end = 1u;
        return result;
    }
    for (frame = 0; frame < AC97_BUFFER_FRAMES; frame++) {
        uint32_t src_frame = (uint32_t)(*src_pos >> 32);
        int16_t left;
        int16_t right;

        if (src_frame >= source->total_frames ||
            !ac97_mod_stream_source_ensure_frame_local(source, src_frame)) {
            ac97_mod_write_stereo_page_frame_local(index, frame, 0, 0);
            result.end = 1u;
            continue;
        }
        left = ac97_mod_stream_source_sample_local(source, src_frame, 0u);
        right = source->stream->channels == 1u ?
            left :
            ac97_mod_stream_source_sample_local(source, src_frame, 1u);
        ac97_mod_write_stereo_page_frame_local(index, frame, left, right);
        *src_pos += src_step;
        frames_written++;
    }
    if ((uint32_t)(*src_pos >> 32) >= source->total_frames) {
        result.end = 1u;
    }
    result.samples = (uint16_t)(frames_written * 2u);
    if (result.samples == 0u) {
        result.samples = 2u;
    }
    return result;
}

static void ac97_mod_set_bdl_descriptor_local(uint32_t index,
                                              uint16_t samples,
                                              uint16_t flags) {
    g_ac97_mod.bdl[index].addr = (uint32_t)g_ac97_mod.buffer_phys[index];
    g_ac97_mod.bdl[index].samples = samples;
    g_ac97_mod.bdl[index].flags = flags;
}

static int ac97_mod_wait_dma_started_local(void) {
    uint32_t spins;

    for (spins = 0; spins < 1000000u; spins++) {
        if ((ac97_mod_bus_read16_local(AC97_PO_SR) & AC97_PO_SR_DCH) == 0u) {
            return 1;
        }
        ac97_mod_delay_local(10u);
    }
    ac97_mod_log("driver: AC97MOD stream did not start sr=%x\n",
                 (uint32_t)ac97_mod_bus_read16_local(AC97_PO_SR));
    return 0;
}

static int ac97_mod_wait_dma_halt_local(void) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t timeout_ticks;
    uint32_t start_ticks;
    uint64_t ring_ticks;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    ring_ticks = ((uint64_t)AC97_BUFFER_BYTES * (uint64_t)AC97_BDL_ENTRIES *
                  (uint64_t)timer_hz + (AC97_OUTPUT_BYTES_PER_SECOND - 1u)) /
        (uint64_t)AC97_OUTPUT_BYTES_PER_SECOND;
    timeout_ticks = (uint32_t)ring_ticks + timer_hz * 5u;
    if (timeout_ticks < timer_hz * 10u) {
        timeout_ticks = timer_hz * 10u;
    }
    start_ticks = driver_timer_current_ticks();
    while ((uint32_t)(driver_timer_current_ticks() - start_ticks) < timeout_ticks) {
        if (ac97_mod_stream_cancelled_local()) {
            return 0;
        }
        if ((ac97_mod_bus_read16_local(AC97_PO_SR) & AC97_PO_SR_DCH) != 0u) {
            return 1;
        }
        ac97_mod_delay_local(20000u);
    }
    ac97_mod_log("driver: AC97MOD stream drain timeout sr=%x civ=%u lvi=%u\n",
                 (uint32_t)ac97_mod_bus_read16_local(AC97_PO_SR),
                 (uint32_t)(ac97_mod_bus_read8_local(AC97_PO_CIV) & 0x1fu),
                 (uint32_t)(ac97_mod_bus_read8_local(AC97_PO_LVI) & 0x1fu));
    return 0;
}

static int ac97_mod_start_pcm_stream_local(uint8_t lvi) {
    ac97_mod_stop_channel_local();
    ac97_mod_bus_write8_local(AC97_PO_CR, AC97_PO_CR_RESET);
    if (!ac97_mod_wait_channel_reset_local()) {
        return 0;
    }
    ac97_mod_flush_cache_local();
    ac97_mod_bus_write32_local(AC97_PO_BDBAR, (uint32_t)g_ac97_mod.bdl_phys);
    ac97_mod_bus_write8_local(AC97_PO_LVI, (uint8_t)(lvi & 0x1fu));
    ac97_mod_bus_write16_local(AC97_PO_SR, AC97_PO_SR_CLEAR);
    ac97_mod_bus_write8_local(AC97_PO_CR, AC97_PO_CR_START);
    if (!ac97_mod_wait_dma_started_local()) {
        ac97_mod_stop_channel_local();
        return 0;
    }
    g_ac97_mod.stream_last_civ = (uint8_t)(ac97_mod_bus_read8_local(AC97_PO_CIV) & 0x1fu);
    g_ac97_mod.stream_active = 1u;
    return 1;
}

static int ac97_mod_stream_alive_local(void) {
    uint16_t status;

    if (!g_ac97_mod.stream_active) {
        return 0;
    }
    status = ac97_mod_bus_read16_local(AC97_PO_SR);
    if ((status & AC97_PO_SR_DCH) == 0u) {
        return 1;
    }
    ac97_mod_log("driver: AC97MOD stream halted sr=%x civ=%u lvi=%u\n",
                 (uint32_t)status,
                 (uint32_t)(ac97_mod_bus_read8_local(AC97_PO_CIV) & 0x1fu),
                 (uint32_t)(ac97_mod_bus_read8_local(AC97_PO_LVI) & 0x1fu));
    g_ac97_mod.stream_active = 0u;
    return 0;
}

static int ac97_mod_wait_completed_descriptor_local(uint8_t *descriptor_out,
                                                    uint8_t *civ_out) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t timeout_ticks;
    uint32_t start_ticks;

    if (descriptor_out == NULL || civ_out == NULL) {
        return 0;
    }
    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    timeout_ticks = timer_hz * 2u;
    if (timeout_ticks < 2u) {
        timeout_ticks = 2u;
    }
    start_ticks = driver_timer_current_ticks();
    for (;;) {
        uint16_t status = ac97_mod_bus_read16_local(AC97_PO_SR);
        uint8_t civ = (uint8_t)(ac97_mod_bus_read8_local(AC97_PO_CIV) & 0x1fu);

        if (ac97_mod_stream_cancelled_local()) {
            g_ac97_mod.stream_active = 0u;
            return 0;
        }
        if ((status & AC97_PO_SR_DCH) != 0u) {
            ac97_mod_log("driver: AC97MOD stream underrun sr=%x civ=%u lvi=%u\n",
                         (uint32_t)status,
                         (uint32_t)civ,
                         (uint32_t)(ac97_mod_bus_read8_local(AC97_PO_LVI) & 0x1fu));
            g_ac97_mod.stream_active = 0u;
            return 0;
        }
        if (civ != g_ac97_mod.stream_last_civ) {
            *descriptor_out = g_ac97_mod.stream_last_civ;
            *civ_out = civ;
            return 1;
        }
        if ((status & AC97_PO_SR_CELV) != 0u) {
            uint8_t lvi = (uint8_t)((uint32_t)(civ + AC97_BDL_ENTRIES - 1u) & 0x1fu);

            ac97_mod_bus_write8_local(AC97_PO_LVI, lvi);
            ac97_mod_bus_write16_local(AC97_PO_SR, AC97_PO_SR_CLEAR);
        }
        if ((uint32_t)(driver_timer_current_ticks() - start_ticks) > timeout_ticks) {
            ac97_mod_log("driver: AC97MOD stream progress timeout sr=%x civ=%u lvi=%u\n",
                         (uint32_t)status,
                         (uint32_t)civ,
                         (uint32_t)(ac97_mod_bus_read8_local(AC97_PO_LVI) & 0x1fu));
            return 0;
        }
        ac97_mod_delay_local(20000u);
    }
}

static uint8_t ac97_mod_prefill_pcm_ring_local(const uint8_t *src,
                                               uint32_t input_frames,
                                               uint64_t *src_pos,
                                               uint64_t src_step,
                                               uint32_t channels,
                                               uint32_t bits_per_sample,
                                               uint8_t final_chunk) {
    uint32_t index;

    for (index = 0; index < AC97_BDL_ENTRIES; index++) {
        struct ac97_mod_fill_result fill =
            ac97_mod_fill_pcm_descriptor_local(index,
                                               src,
                                               input_frames,
                                               src_pos,
                                               src_step,
                                               channels,
                                               bits_per_sample);
        uint16_t flags = (fill.end != 0u && final_chunk != 0u) ?
            AC97_BDL_FLAG_BUP :
            AC97_BDL_FLAGS_NONE;

        ac97_mod_set_bdl_descriptor_local(index, fill.samples, flags);
        if (fill.end != 0u) {
            return (uint8_t)index;
        }
    }
    return (uint8_t)(AC97_BDL_ENTRIES - 1u);
}

static uint8_t ac97_mod_prefill_stream_ring_local(struct ac97_mod_stream_source *source,
                                                  uint64_t *src_pos,
                                                  uint64_t src_step,
                                                  uint8_t *out_end) {
    uint32_t index;

    if (out_end != NULL) {
        *out_end = 0u;
    }
    for (index = 0; index < AC97_BDL_ENTRIES; index++) {
        struct ac97_mod_fill_result fill =
            ac97_mod_fill_stream_descriptor_local(index,
                                                  source,
                                                  src_pos,
                                                  src_step);
        uint16_t flags = fill.end != 0u ? AC97_BDL_FLAG_BUP : AC97_BDL_FLAGS_NONE;

        ac97_mod_set_bdl_descriptor_local(index, fill.samples, flags);
        if (fill.end != 0u) {
            if (out_end != NULL) {
                *out_end = 1u;
            }
            return (uint8_t)index;
        }
    }
    return (uint8_t)(AC97_BDL_ENTRIES - 1u);
}

static int ac97_mod_stream_pcm_data_local(const uint8_t *src,
                                          uint32_t input_frames,
                                          uint64_t *src_pos,
                                          uint64_t src_step,
                                          uint32_t channels,
                                          uint32_t bits_per_sample,
                                          uint8_t final_chunk) {
    while ((uint32_t)(*src_pos >> 32) < input_frames) {
        uint8_t descriptor;
        uint8_t civ;

        if (!ac97_mod_stream_alive_local()) {
            return 0;
        }
        if (!ac97_mod_wait_completed_descriptor_local(&descriptor, &civ)) {
            return 0;
        }
        while (descriptor != civ && (uint32_t)(*src_pos >> 32) < input_frames) {
            struct ac97_mod_fill_result fill;
            uint16_t flags;

            fill = ac97_mod_fill_pcm_descriptor_local((uint32_t)descriptor,
                                                      src,
                                                      input_frames,
                                                      src_pos,
                                                      src_step,
                                                      channels,
                                                      bits_per_sample);
            flags = (fill.end != 0u && final_chunk != 0u) ?
                AC97_BDL_FLAG_BUP :
                AC97_BDL_FLAGS_NONE;
            ac97_mod_set_bdl_descriptor_local((uint32_t)descriptor, fill.samples, flags);
            ac97_mod_flush_cache_local();
            ac97_mod_bus_write8_local(AC97_PO_LVI, descriptor);
            ac97_mod_bus_write16_local(AC97_PO_SR, AC97_PO_SR_CLEAR);
            descriptor = (uint8_t)((uint32_t)(descriptor + 1u) & 0x1fu);
            g_ac97_mod.stream_last_civ = descriptor;
            if (fill.end != 0u) {
                return 1;
            }
        }
    }
    return 1;
}

static int ac97_mod_play_pcm_local(void *ctx,
                                   const void *data,
                                   uint32_t bytes,
                                   uint32_t sample_rate,
                                   uint32_t channels,
                                   uint32_t bits_per_sample,
                                   uint32_t flags) {
    uint32_t src_frame_bytes;
    uint32_t input_frames;
    uint64_t src_pos;
    uint64_t src_step;
    uint8_t final_chunk;
    uint8_t start_lvi;

    (void)ctx;
    if (!g_ac97_mod.initialized || data == NULL || bytes == 0u) {
        return 0;
    }
    if (channels == 0u || channels > 2u) {
        return 0;
    }
    if (bits_per_sample != 8u && bits_per_sample != 16u) {
        return 0;
    }
    if ((flags & ~DRIVER_AUDIO_PLAY_F_ASYNC) != 0u) {
        return 0;
    }
    src_frame_bytes = channels * (bits_per_sample / 8u);
    if (src_frame_bytes == 0u) {
        return 0;
    }
    if (sample_rate < 8000u || sample_rate > AC97_SAMPLE_RATE) {
        return 0;
    }
    input_frames = bytes / src_frame_bytes;
    if (input_frames == 0u) {
        return 0;
    }
    if (!ac97_mod_prepare_dma_local() ||
        !ac97_mod_prepare_buffer_pages_local(AC97_BDL_ENTRIES)) {
        return 0;
    }
    if (!ac97_mod_configure_output_rate_local(AC97_SAMPLE_RATE)) {
        return 0;
    }

    src_step = ((uint64_t)sample_rate << 32) / (uint64_t)AC97_SAMPLE_RATE;
    if (src_step == 0u) {
        src_step = 1u;
    }
    src_pos = 0u;
    final_chunk = (flags & DRIVER_AUDIO_PLAY_F_ASYNC) == 0u ? 1u : 0u;
    if (!ac97_mod_stream_alive_local()) {
        uint32_t index;

        for (index = 0; index < AC97_BDL_ENTRIES; index++) {
            ac97_mod_zero_descriptor_local(index);
        }
        start_lvi = ac97_mod_prefill_pcm_ring_local((const uint8_t *)data,
                                                    input_frames,
                                                    &src_pos,
                                                    src_step,
                                                    channels,
                                                    bits_per_sample,
                                                    final_chunk);
        if (!ac97_mod_start_pcm_stream_local(start_lvi)) {
            return 0;
        }
        if ((uint32_t)(src_pos >> 32) >= input_frames) {
            if (final_chunk != 0u) {
                (void)ac97_mod_wait_dma_halt_local();
                ac97_mod_reset_stream_local();
            }
            return 1;
        }
    }
    if (!ac97_mod_stream_pcm_data_local((const uint8_t *)data,
                                        input_frames,
                                        &src_pos,
                                        src_step,
                                        channels,
                                        bits_per_sample,
                                        final_chunk)) {
        ac97_mod_reset_stream_local();
        return 0;
    }
    if (final_chunk == 0u) {
        return 1;
    }
    (void)ac97_mod_wait_dma_halt_local();
    ac97_mod_reset_stream_local();
    return 1;
}

static int ac97_mod_play_stream_local(void *ctx,
                                      struct driver_audio_pcm_stream *stream) {
    struct ac97_mod_stream_source source;
    uint32_t src_frame_bytes;
    uint32_t read_capacity;
    uint64_t read_phys;
    uint8_t *read_buffer;
    uint64_t src_pos;
    uint64_t src_step;
    uint8_t source_end = 0u;
    uint8_t start_lvi;
    int ok = 0;

    (void)ctx;
    if (stream == NULL || stream->read == NULL) {
        return 0;
    }
    if (!g_ac97_mod.initialized) {
        return 0;
    }
    if (stream->channels == 0u || stream->channels > 2u) {
        return 0;
    }
    if (stream->bits_per_sample != 8u && stream->bits_per_sample != 16u) {
        return 0;
    }
    if (stream->sample_rate < 8000u || stream->sample_rate > AC97_SAMPLE_RATE) {
        return 0;
    }
    if ((stream->flags & ~DRIVER_AUDIO_PLAY_F_ASYNC) != 0u) {
        return 0;
    }
    src_frame_bytes = stream->channels * (stream->bits_per_sample / 8u);
    if (src_frame_bytes == 0u || stream->data_bytes < src_frame_bytes) {
        return 0;
    }
    if (!ac97_mod_prepare_dma_local() ||
        !ac97_mod_prepare_buffer_pages_local(AC97_BDL_ENTRIES)) {
        return 0;
    }
    read_buffer = (uint8_t *)driver_alloc_pages(AC97_STREAM_READ_PAGES, &read_phys);
    (void)read_phys;
    if (read_buffer == NULL) {
        ac97_mod_log("driver: AC97MOD stream read allocation failed pages=%u\n",
                     AC97_STREAM_READ_PAGES);
        return 0;
    }
    read_capacity = AC97_STREAM_READ_PAGES * AC97_PAGE_BYTES;
    read_capacity -= read_capacity % src_frame_bytes;
    if (read_capacity == 0u) {
        goto done;
    }

    ac97_mod_reset_stream_local();
    g_ac97_mod.stream_cancel_ctx = stream->ctx;
    g_ac97_mod.stream_cancelled = stream->cancelled;
    if (!ac97_mod_configure_output_rate_local(AC97_SAMPLE_RATE)) {
        goto done;
    }
    src_step = ((uint64_t)stream->sample_rate << 32) / (uint64_t)AC97_SAMPLE_RATE;
    if (src_step == 0u) {
        src_step = 1u;
    }
    source.stream = stream;
    source.buffer = read_buffer;
    source.capacity = read_capacity;
    source.src_frame_bytes = src_frame_bytes;
    source.remaining_bytes = stream->data_bytes - (stream->data_bytes % src_frame_bytes);
    source.total_frames = source.remaining_bytes / src_frame_bytes;
    source.buffer_base_frame = 0u;
    source.buffer_frames = 0u;
    source.eof = 0u;
    src_pos = 0u;

    for (uint32_t index = 0; index < AC97_BDL_ENTRIES; index++) {
        ac97_mod_zero_descriptor_local(index);
    }
    start_lvi = ac97_mod_prefill_stream_ring_local(&source,
                                                   &src_pos,
                                                   src_step,
                                                   &source_end);
    if (ac97_mod_stream_cancelled_local()) {
        goto done;
    }
    if (!ac97_mod_start_pcm_stream_local(start_lvi)) {
        goto done;
    }
    while (source_end == 0u) {
        uint8_t descriptor;
        uint8_t civ;

        if (ac97_mod_stream_cancelled_local()) {
            goto done;
        }
        if (!ac97_mod_stream_alive_local()) {
            goto done;
        }
        if (!ac97_mod_wait_completed_descriptor_local(&descriptor, &civ)) {
            goto done;
        }
        while (descriptor != civ && source_end == 0u) {
            struct ac97_mod_fill_result fill =
                ac97_mod_fill_stream_descriptor_local((uint32_t)descriptor,
                                                      &source,
                                                      &src_pos,
                                                      src_step);
            uint16_t flags = fill.end != 0u ? AC97_BDL_FLAG_BUP : AC97_BDL_FLAGS_NONE;

            ac97_mod_set_bdl_descriptor_local((uint32_t)descriptor, fill.samples, flags);
            ac97_mod_flush_cache_local();
            ac97_mod_bus_write8_local(AC97_PO_LVI, descriptor);
            ac97_mod_bus_write16_local(AC97_PO_SR, AC97_PO_SR_CLEAR);
            descriptor = (uint8_t)((uint32_t)(descriptor + 1u) & 0x1fu);
            g_ac97_mod.stream_last_civ = descriptor;
            if (fill.end != 0u) {
                source_end = 1u;
            }
        }
    }
    if (!ac97_mod_wait_dma_halt_local()) {
        goto done;
    }
    ac97_mod_reset_stream_local();
    ok = 1;

done:
    if (!ok) {
        ac97_mod_reset_stream_local();
    }
    driver_free_pages(read_buffer, AC97_STREAM_READ_PAGES);
    return ok;
}

static void ac97_mod_fill_name_local(char dst[32], const char *src) {
    uint32_t i = 0;

    while (src != NULL && src[i] != '\0' && i + 1u < 32u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int ac97_mod_register_audio_local(void) {
    static const struct driver_audio_device_ops audio_ops = {
        ac97_mod_play_tone_local,
        ac97_mod_play_pcm_local,
        ac97_mod_play_stream_local
    };
    struct driver_audio_device_info info;

    if (g_ac97_mod.audio_registered) {
        return 1;
    }
    info.present = 1u;
    info.initialized = 1u;
    info.caps = DRIVER_AUDIO_CAP_PLAYBACK | DRIVER_AUDIO_CAP_TONE | DRIVER_AUDIO_CAP_STREAM;
    info.driver_kind = DRIVER_AUDIO_KIND_AC97;
    info.sample_rate = AC97_SAMPLE_RATE;
    info.channels = 2u;
    info.bits_per_sample = 16u;
    ac97_mod_fill_name_local(info.name, "Intel 82801AA AC97 DRV");
    if (!driver_audio_register_device(&info, &audio_ops, &g_ac97_mod, NULL)) {
        return 0;
    }
    g_ac97_mod.audio_registered = 1u;
    return 1;
}

static int ac97_mod_init(void) {
    struct driver_pci_device ac97;
    uint16_t command;

    if (!driver_pci_find_by_class(AC97_PCI_CLASS_MULTIMEDIA,
                                  AC97_PCI_SUBCLASS_AUDIO,
                                  0u,
                                  &ac97)) {
        ac97_mod_log("driver: AC97MOD controller not found\n");
        return 0;
    }
    g_ac97_mod.nambar = ac97_mod_io_base_from_bar_local(ac97.bar[0]);
    g_ac97_mod.nabmbar = ac97_mod_io_base_from_bar_local(ac97.bar[1]);
    if (g_ac97_mod.nambar == 0u || g_ac97_mod.nabmbar == 0u) {
        ac97_mod_log("driver: AC97MOD invalid io bars bar0=%x bar1=%x\n",
                     ac97.bar[0],
                     ac97.bar[1]);
        return 0;
    }
    command = driver_pci_read16(&ac97, AC97_PCI_COMMAND_OFFSET);
    command |= (uint16_t)(PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);
    driver_pci_write16(&ac97, AC97_PCI_COMMAND_OFFSET, command);

    ac97_mod_bus_write32_local(AC97_GLOB_CNT, AC97_GLOB_CNT_COLD);
    ac97_mod_delay_local(200000u);
    (void)ac97_mod_wait_codec_ready_local();
    ac97_mod_mixer_write16_local(AC97_MIXER_RESET, 0u);
    ac97_mod_delay_local(200000u);
    ac97_mod_mixer_write16_local(AC97_MASTER_VOLUME, 0x0000u);
    ac97_mod_mixer_write16_local(AC97_PCM_OUT_VOLUME, 0x0000u);
    ac97_mod_mixer_write16_local(AC97_POWERDOWN, 0x0000u);
    (void)ac97_mod_configure_output_rate_local(AC97_SAMPLE_RATE);
    ac97_mod_delay_local(200000u);

    g_ac97_mod.codec_id =
        ((uint32_t)ac97_mod_mixer_read16_local(AC97_CODEC_VENDOR_ID1) << 16) |
        (uint32_t)ac97_mod_mixer_read16_local(AC97_CODEC_VENDOR_ID2);
    g_ac97_mod.global_status = ac97_mod_bus_read32_local(AC97_GLOB_STA);
    g_ac97_mod.initialized = 1u;
    if (!ac97_mod_prepare_dma_local() ||
        !ac97_mod_prepare_buffer_pages_local(1u) ||
        !ac97_mod_register_audio_local()) {
        return 0;
    }
    ac97_mod_log("driver: AC97MOD init bdf=%u:%u.%u cmd=%x io=%x:%x codec=%x sta=%x dma=%lx:%lx\n",
                 (uint32_t)ac97.bus,
                 (uint32_t)ac97.slot,
                 (uint32_t)ac97.function,
                 (uint32_t)driver_pci_read16(&ac97, AC97_PCI_COMMAND_OFFSET),
                 (uint32_t)g_ac97_mod.nambar,
                 (uint32_t)g_ac97_mod.nabmbar,
                 g_ac97_mod.codec_id,
                 g_ac97_mod.global_status,
                 g_ac97_mod.bdl_phys,
                 g_ac97_mod.buffer_phys[0]);
    return 1;
}

const struct kernel_driver kernel_driver = {
    "AC97",
    KERNEL_DRIVER_KIND_AUDIO,
    ac97_mod_init,
    NULL
};
