#include "drivers/audio/ac97.h"
#include "arch/x86/io.h"
#include "drivers/audio/audio.h"
#include "drivers/bus/pci.h"
#include "hal/hal.h"
#include "kernel/public/mem/pmm.h"

enum {
    PCI_COMMAND_IO = 1u << 0,
    PCI_COMMAND_BUS_MASTER = 1u << 2,
    AC97_MIXER_RESET = 0x00,
    AC97_MASTER_VOLUME = 0x02,
    AC97_PCM_OUT_VOLUME = 0x18,
    AC97_POWERDOWN = 0x26,
    AC97_EXT_AUDIO_ID = 0x28,
    AC97_EXT_AUDIO_CTRL = 0x2a,
    AC97_PCM_FRONT_RATE = 0x2c,
    AC97_CODEC_VENDOR_ID1 = 0x7c,
    AC97_CODEC_VENDOR_ID2 = 0x7e,
    AC97_GLOB_CNT = 0x2c,
    AC97_GLOB_STA = 0x30,
    AC97_GLOB_CNT_COLD = 0x00000002u,
    AC97_GLOB_STA_PCR = 0x00000100u,
    AC97_PO_BDBAR = 0x10,
    AC97_PO_LVI = 0x15,
    AC97_PO_SR = 0x16,
    AC97_PO_CR = 0x1b,
    AC97_PO_SR_DCH = 0x01u,
    AC97_PO_SR_CELV = 0x02u,
    AC97_PO_SR_CLEAR = 0x1cu,
    AC97_PO_CR_START = 0x01u,
    AC97_PO_CR_RESET = 0x02u,
    AC97_BDL_LAST = 0x4000u,
    AC97_SAMPLE_RATE = 48000u,
    AC97_BUFFER_PAGE_BYTES = 4096u,
    AC97_BUFFER_PAGE_FRAMES = AC97_BUFFER_PAGE_BYTES / 4u,
    AC97_BDL_ENTRIES = 32u,
    AC97_STREAM_MAX_FRAMES = AC97_BUFFER_PAGE_FRAMES * AC97_BDL_ENTRIES
};

struct ac97_bdl_entry {
    uint32_t addr;
    uint16_t samples;
    uint16_t flags;
} __attribute__((packed));

static struct ac97_status g_ac97_status;
static uint64_t g_ac97_bdl_phys;
static uint64_t g_ac97_buffer_phys[AC97_BDL_ENTRIES];
static struct ac97_bdl_entry *g_ac97_bdl;
static uint8_t *g_ac97_buffers[AC97_BDL_ENTRIES];
static uint32_t g_ac97_buffer_count;
static uint8_t g_ac97_audio_registered;

const struct kernel_driver ac97_kernel_driver = {
    .name = "AC97",
    .kind = KERNEL_DRIVER_KIND_AUDIO,
    .init = ac97_init,
    .exit = NULL,
};

static uint16_t ac97_io_base_from_bar(uint32_t bar) {
    if ((bar & 0x1u) == 0) {
        return 0;
    }
    return (uint16_t)(bar & 0xfffcu);
}

static uint16_t ac97_mixer_read16(uint16_t base, uint16_t reg) {
    return hal_io_in16((uint16_t)(base + reg));
}

static void ac97_mixer_write16(uint16_t base, uint16_t reg, uint16_t value) {
    hal_io_out16((uint16_t)(base + reg), value);
}

static uint16_t ac97_bus_read16(uint16_t base, uint16_t reg) {
    return hal_io_in16((uint16_t)(base + reg));
}

static void ac97_bus_write16(uint16_t base, uint16_t reg, uint16_t value) {
    hal_io_out16((uint16_t)(base + reg), value);
}

static uint8_t ac97_bus_read8(uint16_t base, uint16_t reg) {
    return hal_io_in8((uint16_t)(base + reg));
}

static void ac97_bus_write8(uint16_t base, uint16_t reg, uint8_t value) {
    hal_io_out8((uint16_t)(base + reg), value);
}

static uint32_t ac97_bus_read32(uint16_t base, uint16_t reg) {
    return inl((uint16_t)(base + reg));
}

static void ac97_bus_write32(uint16_t base, uint16_t reg, uint32_t value) {
    outl((uint16_t)(base + reg), value);
}

static void ac97_delay_local(uint32_t spins) {
    volatile uint32_t i;

    for (i = 0; i < spins; i++) {
        __asm__ __volatile__("" ::: "memory");
    }
}

static void ac97_fill_name_local(char dst[32], const char *src) {
    uint32_t i = 0;

    while (src != 0 && src[i] != '\0' && i + 1u < 32u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int ac97_wait_codec_ready(uint16_t nabmbar) {
    uint32_t spins = 0;

    while (spins < 2000000u) {
        if ((ac97_bus_read32(nabmbar, AC97_GLOB_STA) & AC97_GLOB_STA_PCR) != 0u) {
            return 1;
        }
        spins++;
    }
    return 0;
}

static int ac97_prepare_bdl_page(void) {
    if (g_ac97_bdl != 0) {
        return 1;
    }
    if (g_ac97_bdl_phys == 0) {
        g_ac97_bdl_phys = pmm_alloc_page();
    }
    if (g_ac97_bdl_phys == 0) {
        return 0;
    }
    g_ac97_bdl = (struct ac97_bdl_entry *)hal_phys_direct_map(g_ac97_bdl_phys);
    return g_ac97_bdl != 0;
}

static int ac97_prepare_buffer_pages(uint32_t page_count) {
    if (page_count == 0u || page_count > AC97_BDL_ENTRIES) {
        return 0;
    }
    while (g_ac97_buffer_count < page_count) {
        uint64_t phys = pmm_alloc_page();
        uint8_t *buffer;

        if (phys == 0) {
            return 0;
        }
        buffer = (uint8_t *)hal_phys_direct_map(phys);
        if (buffer == 0) {
            return 0;
        }
        g_ac97_buffer_phys[g_ac97_buffer_count] = phys;
        g_ac97_buffers[g_ac97_buffer_count] = buffer;
        g_ac97_buffer_count++;
    }
    return 1;
}

static int ac97_prepare_dma_memory(void) {
    return ac97_prepare_bdl_page() && ac97_prepare_buffer_pages(1u);
}

static int16_t *ac97_tone_buffer(void) {
    if (g_ac97_buffers[0] == 0) {
        return 0;
    }
    return (int16_t *)g_ac97_buffers[0];
}

static uint32_t ac97_generate_tone_buffer(uint32_t hz) {
    int16_t *buffer = ac97_tone_buffer();
    uint32_t period_frames;
    uint32_t half_period;
    uint32_t usable_frames;
    uint32_t i;

    if (buffer == 0) {
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
    if (period_frames > AC97_BUFFER_PAGE_FRAMES) {
        period_frames = AC97_BUFFER_PAGE_FRAMES;
    }
    usable_frames = AC97_BUFFER_PAGE_FRAMES - (AC97_BUFFER_PAGE_FRAMES % period_frames);
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
    for (; i < AC97_BUFFER_PAGE_FRAMES; i++) {
        buffer[i * 2u] = 0;
        buffer[i * 2u + 1u] = 0;
    }
    return usable_frames;
}

static void ac97_clear_bdl_unused(uint32_t start) {
    while (start < AC97_BDL_ENTRIES) {
        g_ac97_bdl[start].addr = 0;
        g_ac97_bdl[start].samples = 0;
        g_ac97_bdl[start].flags = 0;
        start++;
    }
}

static uint32_t ac97_prepare_repeated_bdl(uint32_t total_frames, uint32_t chunk_frames) {
    uint32_t remaining = total_frames;
    uint32_t count = 0;

    while (count < AC97_BDL_ENTRIES) {
        uint32_t frames = remaining > chunk_frames ? chunk_frames : remaining;

        if (frames == 0u) {
            break;
        }
        g_ac97_bdl[count].addr = (uint32_t)g_ac97_buffer_phys[0];
        g_ac97_bdl[count].samples = (uint16_t)(frames * 2u);
        g_ac97_bdl[count].flags = remaining <= chunk_frames ? AC97_BDL_LAST : 0u;
        remaining -= frames;
        count++;
    }

    ac97_clear_bdl_unused(count);
    return total_frames == 0u ? 0u : (total_frames + chunk_frames - 1u) / chunk_frames;
}

static uint32_t ac97_prepare_linear_bdl(uint32_t total_frames) {
    uint32_t remaining = total_frames;
    uint32_t count = 0;

    while (count < AC97_BDL_ENTRIES && remaining != 0u) {
        uint32_t frames = remaining > AC97_BUFFER_PAGE_FRAMES ? AC97_BUFFER_PAGE_FRAMES : remaining;

        g_ac97_bdl[count].addr = (uint32_t)g_ac97_buffer_phys[count];
        g_ac97_bdl[count].samples = (uint16_t)(frames * 2u);
        g_ac97_bdl[count].flags = remaining <= AC97_BUFFER_PAGE_FRAMES ? AC97_BDL_LAST : 0u;
        remaining -= frames;
        count++;
    }

    ac97_clear_bdl_unused(count);
    return count;
}

static int ac97_wait_channel_reset(uint16_t nabmbar) {
    uint32_t spins = 0;

    while (spins < 1000000u) {
        if ((ac97_bus_read8(nabmbar, AC97_PO_CR) & AC97_PO_CR_RESET) == 0u) {
            return 1;
        }
        spins++;
    }
    return 0;
}

static int ac97_wait_channel_done(uint16_t nabmbar, uint32_t duration_ms) {
    uint32_t timer_hz = hal_timer_hz();
    uint32_t wait_ticks;
    uint32_t start_ticks;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    wait_ticks = ((duration_ms + 39u) * timer_hz + 999u) / 1000u;
    start_ticks = hal_timer_current_ticks();

    if (wait_ticks < 2u) {
        wait_ticks = 2u;
    }
    while ((uint32_t)(hal_timer_current_ticks() - start_ticks) < wait_ticks) {
        uint16_t status = ac97_bus_read16(nabmbar, AC97_PO_SR);

        if ((status & AC97_PO_SR_DCH) != 0u) {
            return 1;
        }
        if ((status & AC97_PO_SR_CELV) != 0u) {
            return 1;
        }
        ac97_delay_local(20000u);
    }

    /* QEMU AC97 can complete audible playback without ever surfacing DCH. */
    return 1;
}

static void ac97_stop_channel(uint16_t nabmbar) {
    ac97_bus_write8(nabmbar, AC97_PO_CR, 0u);
    ac97_bus_write16(nabmbar, AC97_PO_SR, AC97_PO_SR_CLEAR);
}

static int ac97_configure_output_rate(uint16_t nambar, uint32_t sample_rate) {
    if (sample_rate < 8000u || sample_rate > 48000u) {
        return 0;
    }
    if ((g_ac97_status.ext_audio_id & 0x0001u) == 0u) {
        return sample_rate == AC97_SAMPLE_RATE;
    }

    ac97_mixer_write16(nambar, AC97_EXT_AUDIO_CTRL, 0x0001u);
    ac97_mixer_write16(nambar, AC97_PCM_FRONT_RATE, (uint16_t)sample_rate);
    g_ac97_status.ext_audio_ctrl = ac97_mixer_read16(nambar, AC97_EXT_AUDIO_CTRL);
    return 1;
}

static int16_t ac97_decode_u8_sample(uint8_t value) {
    return (int16_t)(((int32_t)value - 128) << 8);
}

static int16_t ac97_decode_le16_sample(const uint8_t *src) {
    return (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static void ac97_write_stereo_frame(uint32_t frame_index, int16_t left, int16_t right) {
    uint32_t page_index = frame_index / AC97_BUFFER_PAGE_FRAMES;
    uint32_t page_frame = frame_index % AC97_BUFFER_PAGE_FRAMES;
    int16_t *dst = (int16_t *)g_ac97_buffers[page_index];

    dst[page_frame * 2u] = left;
    dst[page_frame * 2u + 1u] = right;
}

static void ac97_zero_output_frames(uint32_t frame_count) {
    uint32_t page_count = (frame_count + AC97_BUFFER_PAGE_FRAMES - 1u) / AC97_BUFFER_PAGE_FRAMES;
    uint32_t page_index;

    for (page_index = 0; page_index < page_count; page_index++) {
        uint32_t *dst = (uint32_t *)g_ac97_buffers[page_index];
        uint32_t dwords = AC97_BUFFER_PAGE_BYTES / sizeof(uint32_t);
        uint32_t i;

        for (i = 0; i < dwords; i++) {
            dst[i] = 0u;
        }
    }
}

static int ac97_convert_pcm_pages(const uint8_t *src,
                                  uint32_t frame_count,
                                  uint32_t channels,
                                  uint32_t bits_per_sample) {
    uint32_t src_stride = channels * (bits_per_sample / 8u);
    uint32_t frame_index;
    uint32_t src_offset = 0;

    if (src == 0 || src_stride == 0u) {
        return 0;
    }

    ac97_zero_output_frames(frame_count);
    for (frame_index = 0; frame_index < frame_count; frame_index++) {
        int16_t left;
        int16_t right;

        if (bits_per_sample == 8u) {
            left = ac97_decode_u8_sample(src[src_offset]);
            right = channels == 1u ? left : ac97_decode_u8_sample(src[src_offset + 1u]);
        } else if (bits_per_sample == 16u) {
            left = ac97_decode_le16_sample(src + src_offset);
            right = channels == 1u ? left : ac97_decode_le16_sample(src + src_offset + 2u);
        } else {
            return 0;
        }

        ac97_write_stereo_frame(frame_index, left, right);
        src_offset += src_stride;
    }
    return 1;
}

static int ac97_start_and_wait(uint16_t nabmbar, uint32_t descriptors, uint32_t duration_ms) {
    ac97_stop_channel(nabmbar);
    ac97_bus_write8(nabmbar, AC97_PO_CR, AC97_PO_CR_RESET);
    if (!ac97_wait_channel_reset(nabmbar)) {
        return 0;
    }
    ac97_bus_write32(nabmbar, AC97_PO_BDBAR, (uint32_t)g_ac97_bdl_phys);
    ac97_bus_write8(nabmbar, AC97_PO_LVI, (uint8_t)(descriptors - 1u));
    ac97_bus_write16(nabmbar, AC97_PO_SR, AC97_PO_SR_CLEAR);
    ac97_bus_write8(nabmbar, AC97_PO_CR, AC97_PO_CR_START);
    if (!ac97_wait_channel_done(nabmbar, duration_ms)) {
        ac97_stop_channel(nabmbar);
        return 0;
    }
    ac97_stop_channel(nabmbar);
    return 1;
}

static int ac97_play_tone_local(void *ctx, uint32_t hz, uint32_t duration_ms) {
    uint16_t nabmbar = (uint16_t)(uintptr_t)ctx;
    uint16_t nambar = g_ac97_status.nambar;
    uint32_t chunk_frames;
    uint32_t total_frames;
    uint32_t effective_duration_ms;
    uint32_t max_frames;
    uint32_t descriptors;

    if (!g_ac97_status.initialized || nabmbar == 0 || nambar == 0 || !ac97_prepare_dma_memory()) {
        return 0;
    }
    if (duration_ms == 0u) {
        duration_ms = 150u;
    }
    if (!ac97_configure_output_rate(nambar, AC97_SAMPLE_RATE)) {
        return 0;
    }

    chunk_frames = ac97_generate_tone_buffer(hz);
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
    effective_duration_ms = (uint32_t)(((uint64_t)total_frames * 1000ull + (AC97_SAMPLE_RATE - 1u)) /
                                       (uint64_t)AC97_SAMPLE_RATE);
    descriptors = ac97_prepare_repeated_bdl(total_frames, chunk_frames);
    if (descriptors == 0u || descriptors > AC97_BDL_ENTRIES) {
        return 0;
    }
    return ac97_start_and_wait(nabmbar, descriptors, effective_duration_ms);
}

static int ac97_play_pcm_local(void *ctx,
                               const void *data,
                               uint32_t bytes,
                               uint32_t sample_rate,
                               uint32_t channels,
                               uint32_t bits_per_sample) {
    uint16_t nabmbar = (uint16_t)(uintptr_t)ctx;
    uint16_t nambar = g_ac97_status.nambar;
    uint32_t src_frame_bytes;
    uint32_t frames;
    uint32_t pages;
    uint32_t descriptors;
    uint32_t duration_ms;

    if (!g_ac97_status.initialized || nabmbar == 0 || nambar == 0 || data == 0 || bytes == 0u) {
        return 0;
    }
    if (channels == 0u || channels > 2u) {
        return 0;
    }
    if (bits_per_sample != 8u && bits_per_sample != 16u) {
        return 0;
    }
    src_frame_bytes = channels * (bits_per_sample / 8u);
    if (src_frame_bytes == 0u) {
        return 0;
    }
    frames = bytes / src_frame_bytes;
    if (frames == 0u) {
        return 0;
    }
    if (frames > AC97_STREAM_MAX_FRAMES) {
        frames = AC97_STREAM_MAX_FRAMES;
    }
    pages = (frames + AC97_BUFFER_PAGE_FRAMES - 1u) / AC97_BUFFER_PAGE_FRAMES;
    if (!ac97_prepare_bdl_page() || !ac97_prepare_buffer_pages(pages)) {
        return 0;
    }
    if (!ac97_configure_output_rate(nambar, sample_rate)) {
        return 0;
    }
    if (!ac97_convert_pcm_pages((const uint8_t *)data, frames, channels, bits_per_sample)) {
        return 0;
    }
    descriptors = ac97_prepare_linear_bdl(frames);
    if (descriptors == 0u || descriptors > AC97_BDL_ENTRIES) {
        return 0;
    }
    duration_ms = (uint32_t)(((uint64_t)frames * 1000ull + (sample_rate - 1u)) / (uint64_t)sample_rate);
    if (duration_ms == 0u) {
        duration_ms = 1u;
    }
    return ac97_start_and_wait(nabmbar, descriptors, duration_ms);
}

int ac97_init(void) {
    static const struct audio_device_ops audio_ops = {
        .play_tone = ac97_play_tone_local,
        .play_pcm = ac97_play_pcm_local,
    };
    struct pci_ac97_controller controller;
    struct audio_device_info info;
    uint16_t command;
    uint16_t nambar;
    uint16_t nabmbar;

    g_ac97_status.present = 0;
    g_ac97_status.initialized = 0;

    if (!pci_find_ac97_controller(&controller)) {
        return 0;
    }

    g_ac97_status.present = 1;
    g_ac97_status.bus = controller.bus;
    g_ac97_status.slot = controller.slot;
    g_ac97_status.function = controller.function;
    g_ac97_status.prog_if = controller.prog_if;
    g_ac97_status.irq_line = controller.irq_line;
    g_ac97_status.irq_pin = controller.irq_pin;
    g_ac97_status.vendor_id = controller.vendor_id;
    g_ac97_status.device_id = controller.device_id;
    nambar = ac97_io_base_from_bar(controller.nambar);
    nabmbar = ac97_io_base_from_bar(controller.nabmbar);
    g_ac97_status.nambar = nambar;
    g_ac97_status.nabmbar = nabmbar;
    if (nambar == 0 || nabmbar == 0) {
        return 1;
    }

    command = pci_config_read16(controller.bus, controller.slot, controller.function, 0x04);
    command |= (uint16_t)(PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);
    pci_config_write16(controller.bus, controller.slot, controller.function, 0x04, command);

    ac97_bus_write32(nabmbar, AC97_GLOB_CNT, AC97_GLOB_CNT_COLD);
    ac97_delay_local(200000u);
    (void)ac97_wait_codec_ready(nabmbar);
    ac97_mixer_write16(nambar, AC97_MIXER_RESET, 0);
    ac97_delay_local(200000u);
    ac97_mixer_write16(nambar, AC97_MASTER_VOLUME, 0x0000u);
    ac97_mixer_write16(nambar, AC97_PCM_OUT_VOLUME, 0x0000u);
    ac97_mixer_write16(nambar, AC97_POWERDOWN, 0x0000u);
    if ((ac97_mixer_read16(nambar, AC97_EXT_AUDIO_ID) & 0x0001u) != 0u) {
        ac97_mixer_write16(nambar, AC97_EXT_AUDIO_CTRL, 0x0001u);
        ac97_mixer_write16(nambar, AC97_PCM_FRONT_RATE, (uint16_t)AC97_SAMPLE_RATE);
    }
    ac97_delay_local(200000u);

    g_ac97_status.mixer_reset = ac97_mixer_read16(nambar, AC97_MIXER_RESET);
    g_ac97_status.powerdown = ac97_mixer_read16(nambar, AC97_POWERDOWN);
    g_ac97_status.ext_audio_id = ac97_mixer_read16(nambar, AC97_EXT_AUDIO_ID);
    g_ac97_status.ext_audio_ctrl = ac97_mixer_read16(nambar, AC97_EXT_AUDIO_CTRL);
    g_ac97_status.codec_id =
        ((uint32_t)ac97_mixer_read16(nambar, AC97_CODEC_VENDOR_ID1) << 16) |
        (uint32_t)ac97_mixer_read16(nambar, AC97_CODEC_VENDOR_ID2);
    g_ac97_status.global_control = ac97_bus_read32(nabmbar, AC97_GLOB_CNT);
    g_ac97_status.global_status = ac97_bus_read32(nabmbar, AC97_GLOB_STA);
    g_ac97_status.initialized = 1;

    if (!g_ac97_audio_registered) {
        info.present = 1;
        info.initialized = 1;
        info.caps = AUDIO_CAP_PLAYBACK | AUDIO_CAP_TONE;
        info.driver_kind = AUDIO_DRIVER_AC97;
        info.sample_rate = AC97_SAMPLE_RATE;
        info.channels = 2u;
        info.bits_per_sample = 16u;
        ac97_fill_name_local(info.name, "Intel 82801AA AC97");
        if (audio_register_device(&info, &audio_ops, (void *)(uintptr_t)nabmbar, 0)) {
            g_ac97_audio_registered = 1u;
        }
    }
    (void)ac97_prepare_dma_memory();
    return 1;
}

int ac97_query_status(struct ac97_status *out) {
    if (out == 0) {
        return 0;
    }
    *out = g_ac97_status;
    return g_ac97_status.present != 0;
}
