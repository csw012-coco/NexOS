#include "kernel/public/driver/driver_module.h"

enum {
    PCI_COMMAND_MEMORY = 1u << 1,
    PCI_COMMAND_BUS_MASTER = 1u << 2,
    HDA_PCI_CLASS_MULTIMEDIA = 0x04u,
    HDA_PCI_SUBCLASS_AUDIO = 0x03u,
    HDA_PCI_COMMAND_OFFSET = 0x04u,
    HDA_PCI_TCSEL_OFFSET = 0x44u,
    HDA_PCI_TCSEL_CLEAR_MASK = 0x07u,
    HDA_REG_GCAP = 0x00u,
    HDA_REG_VMIN = 0x02u,
    HDA_REG_VMAJ = 0x03u,
    HDA_REG_OUTPAY = 0x04u,
    HDA_REG_INPAY = 0x06u,
    HDA_REG_GCTL = 0x08u,
    HDA_REG_WAKEEN = 0x0cu,
    HDA_REG_STATESTS = 0x0eu,
    HDA_REG_INTCTL = 0x20u,
    HDA_REG_DPLBASE = 0x70u,
    HDA_REG_DPUBASE = 0x74u,
    HDA_DPLBASE_ENABLE = 1u << 0,
    HDA_REG_SD_BASE = 0x80u,
    HDA_REG_SD_SIZE = 0x20u,
    HDA_SD_CTL0 = 0x00u,
    HDA_SD_CTL2 = 0x02u,
    HDA_SD_STS = 0x03u,
    HDA_SD_LPIB = 0x04u,
    HDA_SD_CBL = 0x08u,
    HDA_SD_LVI = 0x0cu,
    HDA_SD_FMT = 0x12u,
    HDA_SD_BDPL = 0x18u,
    HDA_SD_BDPU = 0x1cu,
    HDA_SD_STS_BCIS = 1u << 2,
    HDA_SD_STS_CLEAR = 0x1fu,
    HDA_SD_CTL_IOCE = 1u << 2,
    HDA_SD_CTL_INT_MASK = 0x1cu,
    HDA_BDL_FLAG_IOC = 1u << 0,
    HDA_REG_CORBSIZE = 0x4eu,
    HDA_REG_RIRBSIZE = 0x5eu,
    HDA_REG_ICOI = 0x60u,
    HDA_REG_ICII = 0x64u,
    HDA_REG_ICIS = 0x68u,
    HDA_GCTL_CRST = 1u << 0,
    HDA_ICIS_ICB = 1u << 0,
    HDA_ICIS_IRV = 1u << 1,
    HDA_ICIS_ICES = 1u << 2,
    HDA_SD_CTL_RUN = 1u << 1,
    HDA_SD_CTL_SRST = 1u << 0,
    HDA_SAMPLE_RATE = 48000u,
    HDA_OUT_CHANNELS = 2u,
    HDA_OUTPUT_BYTES_PER_SECOND = HDA_SAMPLE_RATE * HDA_OUT_CHANNELS * 2u,
    HDA_CACHE_LINE_BYTES = 64u,
    HDA_BUFFER_BYTES = 32768u,
    HDA_PAGE_BYTES = 4096u,
    HDA_BUFFER_PAGES = HDA_BUFFER_BYTES / HDA_PAGE_BYTES,
    HDA_BDL_ENTRIES = 64u,
    HDA_BUFFER_BLOCK_COUNT = 8u,
    HDA_BUFFER_BLOCK_ENTRIES = HDA_BDL_ENTRIES / HDA_BUFFER_BLOCK_COUNT,
    HDA_BUFFER_BLOCK_PAGES = HDA_BUFFER_PAGES * HDA_BUFFER_BLOCK_ENTRIES,
    HDA_BUFFER_FRAMES = HDA_BUFFER_BYTES / 4u,
    HDA_DMA_POS_PAGES = 1u,
    HDA_PCM_POSITION_TOLERANCE_DESCRIPTORS = 2u,
    HDA_PCM_LPIB_GUARD_TRACE = 0u,
    HDA_PCM_MAX_POSITION_DELTA_BYTES = HDA_BUFFER_BYTES,
    HDA_STREAM_READ_PAGES = 128u,
    HDA_PCM_TRACE = 0u,
    HDA_PCM_LOW_TRACE = 0u,
    HDA_PCM_UNDERRUN_TRACE = 0u,
    HDA_PCM_EMPTY_TRACE = 0u,
    HDA_PCM_RING_HAZARD_TRACE = 0u,
    HDA_STREAM_SLOW_READ_TRACE = 0u,
    HDA_LOG_RATE_LIMIT_SECONDS = 1u,
    HDA_DMA_POSITION_BUFFER_ENABLE = 1u,
    HDA_CODEC_REALTEK_ALC887 = 0x10ec0887u,
    HDA_STREAM_FORMAT_48K_16B_2CH = 0x0011u,
    HDA_PREFERRED_PIN_NID = 0x14u,
    HDA_WTYPE_AUDIO_OUT = 0x0u,
    HDA_WTYPE_SELECTOR = 0x3u,
    HDA_WTYPE_PIN = 0x4u,
    HDA_PARAM_VENDOR_ID = 0x00u,
    HDA_PARAM_NODE_COUNT = 0x04u,
    HDA_PARAM_FG_TYPE = 0x05u,
    HDA_PARAM_AWCAP = 0x09u,
    HDA_PARAM_PIN_CAP = 0x0cu,
    HDA_PARAM_CONN_LIST_LEN = 0x0eu,
    HDA_VERB_GET_PARAMETER = 0xf00u,
    HDA_VERB_GET_CONN_LIST_ENTRY = 0xf02u,
    HDA_VERB_SET_COEF_INDEX = 0x500u,
    HDA_VERB_SET_SELECTED_INPUT = 0x701u,
    HDA_VERB_SET_POWER_STATE = 0x705u,
    HDA_VERB_SET_CONV_STREAM_CHAN = 0x706u,
    HDA_VERB_SET_PIN_WIDGET_CONTROL = 0x707u,
    HDA_VERB_SET_EAPD_BTL = 0x70cu,
    HDA_VERB_SET_OUTPUT_CONV_CHAN_CNT = 0x72du,
    HDA_VERB_AFG_RESET = 0x7ffu,
    HDA_VERB_GET_PROC_COEF = 0xc00u,
    HDA_VERB_GET_PIN_CFG_DEFAULT = 0xf1cu,
    HDA_VERB4_SET_CONV_FORMAT = 0x2u,
    HDA_VERB4_SET_AMP_GAIN_MUTE = 0x3u,
    HDA_VERB4_SET_PROC_COEF = 0x4u,
    HDA_REALTEK_VENDOR_NID = 0x20u,
    HDA_REALTEK_PLL_COEF_INDEX = 0x0au,
    HDA_REALTEK_PLL_COEF_BIT = 10u,
    HDA_AMP_SET_OUTPUT = 0x8000u,
    HDA_AMP_SET_INPUT = 0x4000u,
    HDA_AMP_SET_LEFT = 0x2000u,
    HDA_AMP_SET_RIGHT = 0x1000u,
    HDA_AMP_SET_INDEX_SHIFT = 8u,
    HDA_AMP_SET_GAIN_MASK = 0x007fu,
    HDA_DEFAULT_AMP_GAIN = 0x7fu,
    HDA_INVALID_CONN_INDEX = 0xffu
};

enum hda_pos_mode {
    HDA_POS_MODE_LPIB = 0u,
    HDA_POS_MODE_DMA_POSBUF = 1u,
    HDA_POS_MODE_TIMER = 2u,
    HDA_POS_MODE_TIMER_LPIB_GUARD = 3u
};

struct hda_pcm_policy {
    uint8_t pos_mode;
    uint8_t dma_posbuf_enable;
    uint8_t prebuffer_desc;
    uint8_t safe_margin_desc;
    uint8_t lpib_guard_desc;
    uint8_t lpib_guard_start_desc;
    uint8_t xrun_survive_desc;
    uint8_t read_chunk_desc;
    uint8_t write_quantum_desc;
};

struct hda_codec_quirk {
    uint32_t codec_id;
    struct hda_pcm_policy policy;
};

static const struct hda_pcm_policy hda_common_pcm_policy = {
    HDA_POS_MODE_LPIB,
    HDA_DMA_POSITION_BUFFER_ENABLE,
    48u,
    8u,
    2u,
    8u,
    1u,
    8u,
    1u
};

static const struct hda_codec_quirk hda_quirks[] = {
    {
        HDA_CODEC_REALTEK_ALC887,
        {
            HDA_POS_MODE_TIMER_LPIB_GUARD,
            0u,
            56u,
            8u,
            2u,
            16u,
            1u,
            16u,
            1u
        }
    }
};

#define HDA_DMA32_MAX_PHYS 0x100000000ull

struct hda_mod_bdl_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

struct hda_mod_status {
    uint8_t present;
    uint8_t initialized;
    uint8_t audio_registered;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t prog_if;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t play_ready;
    uint8_t play_cad;
    uint8_t play_afg;
    uint8_t play_pin;
    uint8_t play_dac;
    uint8_t play_stream_id;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t pci_command;
    uint32_t mmio_base_lo;
    uint32_t mmio_base_hi;
    uint32_t gcap;
    uint32_t vmaj;
    uint32_t vmin;
    uint32_t outpay;
    uint32_t inpay;
    uint32_t gctl;
    uint32_t statests;
    uint32_t wakeen;
    uint32_t corb_size;
    uint32_t rirb_size;
    uint32_t codec_mask;
    uint32_t codec_vendor;
    uint32_t output_stream_offset;
    uint64_t bdl_phys;
    uint64_t dma_pos_phys;
    uint64_t buffer_block_phys[HDA_BUFFER_BLOCK_COUNT];
    uint64_t buffer_phys[HDA_BDL_ENTRIES];
    volatile uint8_t *mmio;
    struct hda_mod_bdl_entry *bdl;
    volatile uint32_t *dma_pos;
    uint8_t *buffer_block[HDA_BUFFER_BLOCK_COUNT];
    uint8_t *buffers[HDA_BDL_ENTRIES];
    uint32_t buffer_count;
    uint8_t dma_pos_enabled;
    uint8_t dma_pos_trusted;
    uint8_t dma_pos_logged;
    uint8_t q_pos_mode;
    uint8_t q_dma_posbuf_enable;
    uint8_t q_prebuffer_desc;
    uint8_t q_safe_margin_desc;
    uint8_t q_lpib_guard_desc;
    uint8_t q_lpib_guard_start_desc;
    uint8_t q_xrun_survive_desc;
    uint8_t q_read_chunk_desc;
    uint8_t q_write_quantum_desc;
    uint8_t pcm_active;
    uint8_t pcm_started;
    uint8_t pcm_write_index;
    uint8_t pcm_play_index;
    uint8_t pcm_fill_count;
    uint32_t pcm_partial_frames;
    uint32_t pcm_cbl_bytes;
    uint32_t pcm_last_lpib;
    uint32_t pcm_last_hw_offset;
    uint32_t pcm_last_lpib_delta;
    uint32_t pcm_last_lpib_tick;
    enum hda_pos_mode position_mode;
    uint32_t pcm_start_tick;
    uint64_t pcm_start_hw_pos_bytes;
    uint32_t pcm_last_dma_pos;
    uint32_t pcm_position_lpib;
    uint32_t pcm_position_tick;
    uint32_t pcm_position_reject_count;
    uint8_t pcm_position_valid;
    uint32_t pcm_reclaim_tick;
    uint32_t pcm_reclaim_bytes;
    uint32_t pcm_reclaim_budget_bytes;
    uint64_t pcm_app_pos_bytes;
    uint64_t pcm_hw_pos_bytes;
    uint32_t pcm_input_sample_rate;
    uint32_t pcm_input_channels;
    uint32_t pcm_input_bits;
    uint32_t pcm_output_sample_rate;
    uint32_t pcm_last_debug_tick;
    uint32_t pcm_call_seq;
    uint32_t pcm_current_call_seq;
    uint32_t pcm_current_call_base_frame;
    uint32_t pcm_total_input_frames;
    uint32_t pcm_submit_seq;
    uint32_t pcm_reclaim_seq;
    uint32_t pcm_last_submit_src_last;
    uint32_t pcm_last_reclaim_src_last;
    uint32_t pcm_last_hw_src_frame;
    uint32_t pcm_hw_jump_count;
    uint32_t pcm_empty_count;
    uint32_t pcm_underrun_count;
    uint32_t pcm_fake_xrun_count;
    uint32_t pcm_log_xrun_tick;
    uint32_t pcm_log_xrun_suppressed;
    uint32_t pcm_log_fake_xrun_tick;
    uint32_t pcm_log_fake_xrun_suppressed;
    uint32_t pcm_log_recover_tick;
    uint32_t pcm_log_recover_suppressed;
    uint32_t pcm_log_slow_read_tick;
    uint32_t pcm_log_slow_read_suppressed;
    uint32_t pcm_desc_call[HDA_BDL_ENTRIES];
    uint32_t pcm_desc_submit[HDA_BDL_ENTRIES];
    uint32_t pcm_desc_src_first[HDA_BDL_ENTRIES];
    uint32_t pcm_desc_src_last[HDA_BDL_ENTRIES];
    uint32_t pcm_desc_frames[HDA_BDL_ENTRIES];
    uint8_t pcm_buffer_silent[HDA_BDL_ENTRIES];
    uint64_t pcm_src_remainder;
    void *pcm_cancel_ctx;
    uint32_t (*pcm_cancelled)(void *ctx);
};

static struct hda_mod_status g_hda_mod;
static uint32_t g_hda_profile_spin;
static uint32_t g_hda_profile_flush;
static uint32_t g_hda_profile_codec_cmd;
static uint32_t g_hda_profile_pcm_convert;
static uint32_t g_hda_profile_stream_read;
static uint32_t g_hda_profile_pcm_wait;
static uint32_t g_hda_profile_pcm_drain;

static void (*volatile hda_mod_log)(const char *fmt, ...) = driver_log;

static void hda_mod_silent_log_local(const char *fmt, ...) {
    (void)fmt;
}

static int hda_mod_restore_log_and_return_local(void (*saved_log)(const char *fmt, ...),
                                                int result) {
    hda_mod_log = saved_log;
    return result;
}

static uint32_t hda_mod_log_rate_interval_ticks_local(void) {
    uint32_t timer_hz = driver_timer_hz();

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    return timer_hz * HDA_LOG_RATE_LIMIT_SECONDS;
}

static int hda_mod_log_ratelimit_local(uint32_t *last_tick,
                                       uint32_t *suppressed) {
    uint32_t now;
    uint32_t interval;

    if (last_tick == NULL || suppressed == NULL) {
        return 1;
    }
    now = driver_timer_current_ticks();
    interval = hda_mod_log_rate_interval_ticks_local();
    if (*last_tick == 0u || (uint32_t)(now - *last_tick) >= interval) {
        *last_tick = now;
        return 1;
    }
    *suppressed = *suppressed + 1u;
    return 0;
}

static uint32_t hda_mod_log_take_suppressed_local(uint32_t *suppressed) {
    uint32_t value;

    if (suppressed == NULL) {
        return 0u;
    }
    value = *suppressed;
    *suppressed = 0u;
    return value;
}

static int hda_mod_pcm_cancelled_local(void) {
    return g_hda_mod.pcm_cancelled != NULL &&
           g_hda_mod.pcm_cancelled(g_hda_mod.pcm_cancel_ctx) != 0u;
}

static uint64_t hda_mod_mmio_base_from_bar_local(uint32_t bar_lo, uint32_t bar_hi) {
    uint64_t base;

    if ((bar_lo & 0x1u) != 0u) {
        return 0;
    }
    base = (uint64_t)(bar_lo & 0xfffffff0u);
    if ((bar_lo & 0x6u) == 0x4u) {
        base |= (uint64_t)bar_hi << 32;
    }
    return base;
}

static uint8_t hda_mod_read8_local(uint32_t offset) {
    return *(volatile uint8_t *)(g_hda_mod.mmio + offset);
}

static uint16_t hda_mod_read16_local(uint32_t offset) {
    return *(volatile uint16_t *)(g_hda_mod.mmio + offset);
}

static uint32_t hda_mod_read32_local(uint32_t offset) {
    return *(volatile uint32_t *)(g_hda_mod.mmio + offset);
}

static void hda_mod_write8_local(uint32_t offset, uint8_t value) {
    *(volatile uint8_t *)(g_hda_mod.mmio + offset) = value;
}

static void hda_mod_write16_local(uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(g_hda_mod.mmio + offset) = value;
}

static void hda_mod_write32_local(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(g_hda_mod.mmio + offset) = value;
}

static void hda_mod_delay_local(uint32_t spins) {
    volatile uint32_t i;
    uint64_t start = driver_profile_clock();

    for (i = 0; i < spins; i++) {
        __asm__ __volatile__("" ::: "memory");
    }
    driver_profile_record(g_hda_profile_spin,
                          driver_profile_clock() - start,
                          spins);
}

static void hda_mod_wait_for_event_profiled_local(uint32_t profile_handle) {
    uint64_t start = driver_profile_clock();

    driver_cpu_wait_for_event();
    driver_profile_record(profile_handle,
                          driver_profile_clock() - start,
                          1u);
}

static void hda_mod_flush_range_local(const void *ptr, uint32_t bytes) {
    uintptr_t addr;
    uintptr_t end;
    uint64_t start;

    if (ptr == NULL || bytes == 0u) {
        return;
    }
    start = driver_profile_clock();
    addr = (uintptr_t)ptr & ~(uintptr_t)(HDA_CACHE_LINE_BYTES - 1u);
    end = ((uintptr_t)ptr + bytes + HDA_CACHE_LINE_BYTES - 1u) &
          ~(uintptr_t)(HDA_CACHE_LINE_BYTES - 1u);
    while (addr < end) {
        __asm__ __volatile__("clflush (%0)" :: "r"((void *)addr) : "memory");
        addr += HDA_CACHE_LINE_BYTES;
    }
    __asm__ __volatile__("mfence" ::: "memory");
    driver_profile_record(g_hda_profile_flush,
                          driver_profile_clock() - start,
                          bytes);
}

static void hda_mod_flush_descriptor_local(uint32_t index) {
    if (index >= HDA_BDL_ENTRIES || g_hda_mod.bdl == NULL) {
        return;
    }
    hda_mod_flush_range_local(&g_hda_mod.bdl[index], sizeof(g_hda_mod.bdl[index]));
}

static void hda_mod_flush_buffer_local(uint32_t index) {
    if (index >= HDA_BDL_ENTRIES || g_hda_mod.buffers[index] == NULL) {
        return;
    }
    hda_mod_flush_range_local(g_hda_mod.buffers[index], HDA_BUFFER_BYTES);
}

static void hda_mod_flush_bdl_local(void) {
    if (g_hda_mod.bdl == NULL) {
        return;
    }
    hda_mod_flush_range_local(g_hda_mod.bdl, sizeof(g_hda_mod.bdl[0]) * HDA_BDL_ENTRIES);
}

static int hda_mod_wait_crst_local(uint32_t want_set) {
    uint32_t spins;

    for (spins = 0; spins < 16384u; spins++) {
        uint32_t set = hda_mod_read32_local(HDA_REG_GCTL) & HDA_GCTL_CRST;

        if ((want_set != 0u && set != 0u) ||
            (want_set == 0u && set == 0u)) {
            return 1;
        }
    }
    return 0;
}

static int hda_mod_wait_state_sts_local(void) {
    uint32_t spins;

    for (spins = 0; spins < 16384u; spins++) {
        if ((hda_mod_read16_local(HDA_REG_STATESTS) & 0x7fffu) != 0u) {
            return 1;
        }
    }
    return 0;
}

static int hda_mod_controller_reset_local(void) {
    uint32_t gctl;

    gctl = hda_mod_read32_local(HDA_REG_GCTL);
    hda_mod_write32_local(HDA_REG_GCTL, gctl & ~HDA_GCTL_CRST);
    if (!hda_mod_wait_crst_local(0u)) {
        return 0;
    }
    hda_mod_delay_local(2000u);
    hda_mod_write32_local(HDA_REG_GCTL, gctl | HDA_GCTL_CRST);
    if (!hda_mod_wait_crst_local(1u)) {
        return 0;
    }
    hda_mod_delay_local(2000u);
    (void)hda_mod_wait_state_sts_local();
    return 1;
}

static int hda_mod_send_cmd20_local(uint8_t cad,
                                    uint8_t nid,
                                    uint32_t cmd20,
                                    uint32_t *out_resp) {
    uint32_t cmd;
    uint32_t spins;
    uint64_t start = driver_profile_clock();

    if (cad >= 15u) {
        driver_profile_record(g_hda_profile_codec_cmd,
                              driver_profile_clock() - start,
                              0u);
        return 0;
    }
    cmd = ((uint32_t)cad << 28) | ((uint32_t)nid << 20) | (cmd20 & 0xfffffu);
    for (spins = 0; spins < 1000000u; spins++) {
        if ((hda_mod_read16_local(HDA_REG_ICIS) & HDA_ICIS_ICB) == 0u) {
            break;
        }
    }
    if ((hda_mod_read16_local(HDA_REG_ICIS) & HDA_ICIS_ICB) != 0u) {
        driver_profile_record(g_hda_profile_codec_cmd,
                              driver_profile_clock() - start,
                              0u);
        return 0;
    }
    hda_mod_write16_local(HDA_REG_ICIS, (uint16_t)(HDA_ICIS_IRV | HDA_ICIS_ICES));
    hda_mod_write32_local(HDA_REG_ICOI, cmd);
    hda_mod_write16_local(HDA_REG_ICIS, HDA_ICIS_ICB);
    for (spins = 0; spins < 2000000u; spins++) {
        uint16_t status = hda_mod_read16_local(HDA_REG_ICIS);

        if ((status & HDA_ICIS_ICB) != 0u) {
            continue;
        }
        if ((status & HDA_ICIS_ICES) != 0u) {
            hda_mod_write16_local(HDA_REG_ICIS, HDA_ICIS_ICES);
            driver_profile_record(g_hda_profile_codec_cmd,
                                  driver_profile_clock() - start,
                                  0u);
            return 0;
        }
        if ((status & HDA_ICIS_IRV) == 0u) {
            driver_profile_record(g_hda_profile_codec_cmd,
                                  driver_profile_clock() - start,
                                  0u);
            return 0;
        }
        if (out_resp != NULL) {
            *out_resp = hda_mod_read32_local(HDA_REG_ICII);
        } else {
            (void)hda_mod_read32_local(HDA_REG_ICII);
        }
        hda_mod_write16_local(HDA_REG_ICIS, HDA_ICIS_IRV);
        driver_profile_record(g_hda_profile_codec_cmd,
                              driver_profile_clock() - start,
                              1u);
        return 1;
    }
    driver_profile_record(g_hda_profile_codec_cmd,
                          driver_profile_clock() - start,
                          0u);
    return 0;
}

static int hda_mod_send_verb_local(uint8_t cad,
                                   uint8_t nid,
                                   uint16_t verb,
                                   uint8_t payload,
                                   uint32_t *out_resp) {
    uint32_t cmd20 = ((uint32_t)(verb & 0x0fffu) << 8) | (uint32_t)payload;

    return hda_mod_send_cmd20_local(cad, nid, cmd20, out_resp);
}

static int hda_mod_send_verb4_local(uint8_t cad,
                                    uint8_t nid,
                                    uint8_t verb,
                                    uint16_t payload,
                                    uint32_t *out_resp) {
    uint32_t cmd20 = ((uint32_t)(verb & 0x0fu) << 16) | (uint32_t)payload;

    return hda_mod_send_cmd20_local(cad, nid, cmd20, out_resp);
}

static int hda_mod_get_parameter_local(uint8_t cad,
                                       uint8_t nid,
                                       uint8_t parameter,
                                       uint32_t *out_value) {
    return hda_mod_send_verb_local(cad,
                                   nid,
                                   HDA_VERB_GET_PARAMETER,
                                   parameter,
                                   out_value);
}

static int hda_mod_set_coef_index_local(uint8_t cad, uint8_t nid, uint8_t index) {
    return hda_mod_send_verb_local(cad, nid, HDA_VERB_SET_COEF_INDEX, index, NULL);
}

static int hda_mod_read_proc_coef_local(uint8_t cad, uint8_t nid, uint16_t *out_value) {
    uint32_t value;

    if (out_value == NULL ||
        !hda_mod_send_verb_local(cad, nid, HDA_VERB_GET_PROC_COEF, 0u, &value)) {
        return 0;
    }
    *out_value = (uint16_t)value;
    return 1;
}

static int hda_mod_write_proc_coef_local(uint8_t cad, uint8_t nid, uint16_t value) {
    return hda_mod_send_verb4_local(cad, nid, HDA_VERB4_SET_PROC_COEF, value, NULL);
}

static int hda_mod_update_coef_local(uint8_t cad,
                                     uint8_t nid,
                                     uint8_t index,
                                     uint16_t mask,
                                     uint16_t bits_set) {
    uint16_t value;
    uint16_t next;

    if (!hda_mod_set_coef_index_local(cad, nid, index) ||
        !hda_mod_read_proc_coef_local(cad, nid, &value)) {
        return 0;
    }
    next = (uint16_t)((value & ~mask) | (bits_set & mask));
    if (next == value) {
        return 1;
    }
    return hda_mod_set_coef_index_local(cad, nid, index) &&
           hda_mod_write_proc_coef_local(cad, nid, next);
}

static void hda_mod_apply_realtek_fixups_local(uint8_t cad) {
    if (g_hda_mod.codec_vendor != HDA_CODEC_REALTEK_ALC887) {
        return;
    }
    if (hda_mod_update_coef_local(cad,
                                  HDA_REALTEK_VENDOR_NID,
                                  HDA_REALTEK_PLL_COEF_INDEX,
                                  (uint16_t)(1u << HDA_REALTEK_PLL_COEF_BIT),
                                  0u)) {
        driver_log("driver: HDAMOD alc887 pll coef fixed nid=%u idx=%u bit=%u\n",
                   HDA_REALTEK_VENDOR_NID,
                   HDA_REALTEK_PLL_COEF_INDEX,
                   HDA_REALTEK_PLL_COEF_BIT);
    } else {
        driver_log("driver: HDAMOD alc887 pll coef fix failed\n");
    }
}

static uint8_t hda_mod_widget_type_local(uint32_t awcap) {
    return (uint8_t)((awcap >> 20) & 0x0fu);
}

static int hda_mod_find_afg_local(uint8_t cad, uint8_t *out_afg) {
    uint32_t nodes;
    uint8_t start;
    uint8_t count;
    uint8_t i;

    if (out_afg == NULL ||
        !hda_mod_get_parameter_local(cad, 0u, HDA_PARAM_NODE_COUNT, &nodes)) {
        return 0;
    }
    start = (uint8_t)((nodes >> 16) & 0xffu);
    count = (uint8_t)(nodes & 0xffu);
    for (i = 0; i < count; i++) {
        uint8_t nid = (uint8_t)(start + i);
        uint32_t type;

        if (!hda_mod_get_parameter_local(cad, nid, HDA_PARAM_FG_TYPE, &type)) {
            continue;
        }
        if ((type & 0xffu) == 0x01u) {
            *out_afg = nid;
            return 1;
        }
    }
    return 0;
}

static int hda_mod_get_connections_local(uint8_t cad,
                                         uint8_t nid,
                                         uint8_t *out_list,
                                         uint32_t max,
                                         uint32_t *out_count) {
    uint32_t conn_len;
    uint32_t count;
    uint32_t step;
    uint32_t index;
    uint32_t out_index = 0;
    uint32_t long_form;

    if (out_list == NULL || out_count == NULL || max == 0u) {
        return 0;
    }
    if (!hda_mod_get_parameter_local(cad, nid, HDA_PARAM_CONN_LIST_LEN, &conn_len)) {
        return 0;
    }
    count = conn_len & 0x7fu;
    long_form = conn_len & 0x80u;
    if (count == 0u) {
        *out_count = 0u;
        return 1;
    }
    step = long_form != 0u ? 2u : 4u;
    for (index = 0; index < count && out_index < max; index += step) {
        uint32_t resp;

        if (!hda_mod_send_verb_local(cad,
                                     nid,
                                     HDA_VERB_GET_CONN_LIST_ENTRY,
                                     (uint8_t)index,
                                     &resp)) {
            return 0;
        }
        if (long_form != 0u) {
            if (index < count && out_index < max) {
                out_list[out_index++] = (uint8_t)(resp & 0xffu);
            }
            if (index + 1u < count && out_index < max) {
                out_list[out_index++] = (uint8_t)((resp >> 16) & 0xffu);
            }
        } else {
            uint32_t shift;

            for (shift = 0; shift < 32u && index + shift / 8u < count; shift += 8u) {
                if (out_index >= max) {
                    break;
                }
                out_list[out_index++] = (uint8_t)((resp >> shift) & 0xffu);
            }
        }
    }
    *out_count = out_index;
    return 1;
}

static int hda_mod_path_contains_local(const uint8_t *path,
                                       uint32_t depth,
                                       uint8_t nid) {
    uint32_t i;

    for (i = 0; i < depth; i++) {
        if (path[i] == nid) {
            return 1;
        }
    }
    return 0;
}

static int hda_mod_dfs_to_dac_local(uint8_t cad,
                                    uint8_t nid,
                                    uint8_t *path,
                                    uint8_t *path_input,
                                    uint32_t depth,
                                    uint8_t *out_dac,
                                    uint32_t *out_path_len) {
    uint32_t awcap;
    uint8_t type;
    uint8_t conns[32];
    uint32_t conn_count;
    uint32_t index;

    if (path == NULL ||
        path_input == NULL ||
        out_dac == NULL ||
        out_path_len == NULL ||
        depth >= 16u) {
        return 0;
    }
    if (hda_mod_path_contains_local(path, depth, nid)) {
        return 0;
    }
    path[depth] = nid;
    path_input[depth] = HDA_INVALID_CONN_INDEX;
    if (!hda_mod_get_parameter_local(cad, nid, HDA_PARAM_AWCAP, &awcap)) {
        return 0;
    }
    type = hda_mod_widget_type_local(awcap);
    if (type == HDA_WTYPE_AUDIO_OUT) {
        *out_dac = nid;
        *out_path_len = depth + 1u;
        return 1;
    }
    if (!hda_mod_get_connections_local(cad,
                                       nid,
                                       conns,
                                       (uint32_t)sizeof(conns),
                                       &conn_count)) {
        return 0;
    }
    for (index = 0; index < conn_count; index++) {
        uint8_t next = conns[index];

        if (next == 0u) {
            continue;
        }
        if (type == HDA_WTYPE_SELECTOR || type == HDA_WTYPE_PIN) {
            (void)hda_mod_send_verb_local(cad,
                                          nid,
                                          HDA_VERB_SET_SELECTED_INPUT,
                                          (uint8_t)index,
                                          NULL);
        }
        if (hda_mod_dfs_to_dac_local(cad,
                                     next,
                                     path,
                                     path_input,
                                     depth + 1u,
                                     out_dac,
                                     out_path_len)) {
            path_input[depth] = (uint8_t)index;
            return 1;
        }
    }
    return 0;
}

static int hda_mod_score_output_pin_local(uint8_t nid,
                                          uint32_t pincap,
                                          uint32_t config) {
    int score = 0;
    uint8_t port_conn;
    uint8_t device_type;

    if ((pincap & (1u << 4)) != 0u) {
        score += 100;
    }
    if ((pincap & (1u << 5)) != 0u && (pincap & (1u << 4)) == 0u) {
        score -= 10;
    }
    if (config != 0u) {
        score += 5;
    }
    port_conn = (uint8_t)((config >> 30) & 0x3u);
    device_type = (uint8_t)((config >> 20) & 0xfu);
    if (config != 0u && port_conn == 1u) {
        return -100000;
    }
    if (port_conn == 0u || port_conn == 2u || port_conn == 3u) {
        score += 20;
    }
    if (device_type == 0u) {
        score += 50;
    } else if (device_type == 1u) {
        score += 45;
    } else if (device_type == 2u) {
        score += 40;
    } else {
        score += 10;
    }
    if (nid == HDA_PREFERRED_PIN_NID && (config == 0u || port_conn != 1u)) {
        score += 30;
    }
    return score;
}

static int hda_mod_select_output_path_local(uint8_t cad,
                                            uint8_t afg,
                                            uint8_t *out_pin,
                                            uint8_t *out_dac,
                                            uint8_t *out_path,
                                            uint8_t *out_path_input,
                                            uint32_t *out_path_len) {
    uint8_t pins[32];
    int scores[32];
    uint32_t pin_count = 0;
    uint32_t nodes;
    uint8_t start;
    uint8_t count;
    uint8_t i;

    if (out_pin == NULL ||
        out_dac == NULL ||
        out_path == NULL ||
        out_path_input == NULL ||
        out_path_len == NULL ||
        !hda_mod_get_parameter_local(cad, afg, HDA_PARAM_NODE_COUNT, &nodes)) {
        return 0;
    }
    start = (uint8_t)((nodes >> 16) & 0xffu);
    count = (uint8_t)(nodes & 0xffu);
    for (i = 0; i < count && pin_count < 32u; i++) {
        uint8_t nid = (uint8_t)(start + i);
        uint32_t awcap;
        uint32_t pincap = 0;
        uint32_t config = 0;

        if (!hda_mod_get_parameter_local(cad, nid, HDA_PARAM_AWCAP, &awcap)) {
            continue;
        }
        if (hda_mod_widget_type_local(awcap) != HDA_WTYPE_PIN) {
            continue;
        }
        (void)hda_mod_get_parameter_local(cad, nid, HDA_PARAM_PIN_CAP, &pincap);
        (void)hda_mod_send_verb_local(cad,
                                      nid,
                                      HDA_VERB_GET_PIN_CFG_DEFAULT,
                                      0u,
                                      &config);
        pins[pin_count] = nid;
        scores[pin_count] = hda_mod_score_output_pin_local(nid, pincap, config);
        driver_log("driver: HDAMOD pin cand nid=%u pincap=%x cfg=%x score=%d\n",
                   (uint32_t)nid,
                   pincap,
                   config,
                   scores[pin_count]);
        pin_count++;
    }
    if (pin_count == 0u) {
        return 0;
    }
    for (i = 0; i < pin_count; i++) {
        if (pins[i] == HDA_PREFERRED_PIN_NID &&
            hda_mod_dfs_to_dac_local(cad,
                                     pins[i],
                                     out_path,
                                     out_path_input,
                                     0u,
                                     out_dac,
                                     out_path_len)) {
            *out_pin = pins[i];
            return 1;
        }
    }
    for (i = 0; i < pin_count; i++) {
        uint32_t j;
        uint32_t best_index = 0u;
        int best_score = -100000;

        for (j = 0; j < pin_count; j++) {
            if (scores[j] > best_score) {
                best_score = scores[j];
                best_index = j;
            }
        }
        if (best_score <= -100000) {
            break;
        }
        scores[best_index] = -100000;
        if (hda_mod_dfs_to_dac_local(cad,
                                     pins[best_index],
                                     out_path,
                                     out_path_input,
                                     0u,
                                     out_dac,
                                     out_path_len)) {
            *out_pin = pins[best_index];
            return 1;
        }
    }
    return 0;
}

static void hda_mod_set_power_d0_local(uint8_t cad, uint8_t nid) {
    (void)hda_mod_send_verb_local(cad, nid, HDA_VERB_SET_POWER_STATE, 0u, NULL);
}

static void hda_mod_unmute_amp_local(uint8_t cad,
                                     uint8_t nid,
                                     uint32_t output,
                                     uint8_t index,
                                     uint8_t gain) {
    uint16_t payload;

    payload = (uint16_t)((output != 0u ? HDA_AMP_SET_OUTPUT : HDA_AMP_SET_INPUT) |
                         HDA_AMP_SET_LEFT |
                         HDA_AMP_SET_RIGHT |
                         ((uint16_t)(index & 0x0fu) << HDA_AMP_SET_INDEX_SHIFT) |
                         (gain & HDA_AMP_SET_GAIN_MASK));
    (void)hda_mod_send_verb4_local(cad,
                                   nid,
                                   HDA_VERB4_SET_AMP_GAIN_MUTE,
                                   payload,
                                   NULL);
}

static int hda_mod_setup_output_path_local(void) {
    uint8_t cad = 0xffu;
    uint8_t afg = 0u;
    uint8_t pin = 0u;
    uint8_t dac = 0u;
    uint8_t path[16];
    uint8_t path_input[16];
    uint32_t path_len = 0u;
    uint32_t i;
    uint32_t input_streams;
    uint32_t output_streams;

    if (g_hda_mod.play_ready != 0u) {
        return 1;
    }
    for (i = 0; i < 15u; i++) {
        if ((g_hda_mod.codec_mask & (1u << i)) != 0u) {
            cad = (uint8_t)i;
            break;
        }
    }
    if (cad == 0xffu || !hda_mod_find_afg_local(cad, &afg)) {
        hda_mod_log("driver: HDAMOD no playable codec path\n");
        return 0;
    }
    (void)hda_mod_send_verb_local(cad, afg, HDA_VERB_AFG_RESET, 0u, NULL);
    hda_mod_delay_local(100000u);
    hda_mod_apply_realtek_fixups_local(cad);
    if (!hda_mod_select_output_path_local(cad,
                                          afg,
                                          &pin,
                                          &dac,
                                          path,
                                          path_input,
                                          &path_len)) {
        hda_mod_log("driver: HDAMOD output pin/dac path not found\n");
        return 0;
    }
    hda_mod_set_power_d0_local(cad, afg);
    for (i = 0; i < path_len; i++) {
        hda_mod_set_power_d0_local(cad, path[i]);
        if (path_input[i] != HDA_INVALID_CONN_INDEX && path_input[i] < 16u) {
            hda_mod_unmute_amp_local(cad, path[i], 0u, path_input[i], HDA_DEFAULT_AMP_GAIN);
        }
        hda_mod_unmute_amp_local(cad, path[i], 1u, 0u, HDA_DEFAULT_AMP_GAIN);
    }
    (void)hda_mod_send_verb_local(cad,
                                  pin,
                                  HDA_VERB_SET_PIN_WIDGET_CONTROL,
                                  0xc0u,
                                  NULL);
    (void)hda_mod_send_verb_local(cad, pin, HDA_VERB_SET_EAPD_BTL, 0x02u, NULL);

    input_streams = (g_hda_mod.gcap >> 8) & 0x0fu;
    output_streams = (g_hda_mod.gcap >> 12) & 0x0fu;
    if (output_streams == 0u) {
        hda_mod_log("driver: HDAMOD no output streams gcap=%x\n", g_hda_mod.gcap);
        return 0;
    }
    g_hda_mod.play_ready = 1u;
    g_hda_mod.play_cad = cad;
    g_hda_mod.play_afg = afg;
    g_hda_mod.play_pin = pin;
    g_hda_mod.play_dac = dac;
    g_hda_mod.play_stream_id = 1u;
    g_hda_mod.output_stream_offset =
        HDA_REG_SD_BASE + HDA_REG_SD_SIZE * input_streams;
    driver_log("driver: HDAMOD output cad=%u afg=%u pin=%u dac=%u sd=%x codec=%x path_len=%u\n",
               (uint32_t)cad,
               (uint32_t)afg,
               (uint32_t)pin,
               (uint32_t)dac,
               g_hda_mod.output_stream_offset,
               g_hda_mod.codec_vendor,
               path_len);
    return 1;
}

static int hda_mod_prepare_dma_local(void) {
    uint32_t block;
    uint32_t index;

    if (g_hda_mod.dma_pos == NULL) {
        g_hda_mod.dma_pos =
            (volatile uint32_t *)driver_alloc_pages_below(HDA_DMA_POS_PAGES,
                                                          HDA_DMA32_MAX_PHYS,
                                                          &g_hda_mod.dma_pos_phys);
        if (g_hda_mod.dma_pos == NULL) {
            hda_mod_log("driver: HDAMOD low dma position allocation failed\n");
            return 0;
        }
        driver_memset((void *)g_hda_mod.dma_pos, 0, HDA_DMA_POS_PAGES * HDA_PAGE_BYTES);
    }
    if (g_hda_mod.bdl == NULL) {
        g_hda_mod.bdl =
            (struct hda_mod_bdl_entry *)driver_alloc_pages_below(1u,
                                                                 HDA_DMA32_MAX_PHYS,
                                                                 &g_hda_mod.bdl_phys);
        if (g_hda_mod.bdl == NULL) {
            hda_mod_log("driver: HDAMOD low bdl allocation failed\n");
            return 0;
        }
        driver_memset(g_hda_mod.bdl, 0, 4096u);
    }
    for (block = 0; block < HDA_BUFFER_BLOCK_COUNT; block++) {
        if (g_hda_mod.buffer_block[block] != NULL) {
            continue;
        }
        g_hda_mod.buffer_block[block] =
            (uint8_t *)driver_alloc_pages_below(HDA_BUFFER_BLOCK_PAGES,
                                                HDA_DMA32_MAX_PHYS,
                                                &g_hda_mod.buffer_block_phys[block]);
        if (g_hda_mod.buffer_block[block] == NULL) {
            hda_mod_log("driver: HDAMOD low buffer block allocation failed block=%u pages=%u\n",
                        block,
                        HDA_BUFFER_BLOCK_PAGES);
            return 0;
        }
        driver_memset(g_hda_mod.buffer_block[block],
                      0,
                      HDA_BUFFER_BYTES * HDA_BUFFER_BLOCK_ENTRIES);
    }
    for (index = 0; index < HDA_BDL_ENTRIES; index++) {
        uint32_t block_index = index / HDA_BUFFER_BLOCK_ENTRIES;
        uint32_t block_entry = index % HDA_BUFFER_BLOCK_ENTRIES;
        uint64_t offset = (uint64_t)block_entry * HDA_BUFFER_BYTES;

        g_hda_mod.buffer_phys[index] = g_hda_mod.buffer_block_phys[block_index] + offset;
        g_hda_mod.buffers[index] = g_hda_mod.buffer_block[block_index] + offset;
        g_hda_mod.bdl[index].addr = g_hda_mod.buffer_phys[index];
        g_hda_mod.bdl[index].length = HDA_BUFFER_BYTES;
        g_hda_mod.bdl[index].flags = HDA_BDL_FLAG_IOC;
    }
    g_hda_mod.buffer_count = HDA_BDL_ENTRIES;
    return 1;
}

static uint32_t hda_mod_output_stream_index_local(void) {
    if (g_hda_mod.output_stream_offset < HDA_REG_SD_BASE) {
        return 0u;
    }
    return (g_hda_mod.output_stream_offset - HDA_REG_SD_BASE) / HDA_REG_SD_SIZE;
}

static void hda_mod_apply_codec_quirk_local(void) {
    const struct hda_pcm_policy *policy = &hda_common_pcm_policy;
    uint32_t index;

    for (index = 0u; index < sizeof(hda_quirks) / sizeof(hda_quirks[0]); index++) {
        if (hda_quirks[index].codec_id == g_hda_mod.codec_vendor) {
            policy = &hda_quirks[index].policy;
            break;
        }
    }

    g_hda_mod.q_pos_mode = policy->pos_mode;
    g_hda_mod.q_dma_posbuf_enable = policy->dma_posbuf_enable;
    g_hda_mod.q_prebuffer_desc = policy->prebuffer_desc;
    g_hda_mod.q_safe_margin_desc = policy->safe_margin_desc;
    g_hda_mod.q_lpib_guard_desc = policy->lpib_guard_desc;
    g_hda_mod.q_lpib_guard_start_desc = policy->lpib_guard_start_desc;
    g_hda_mod.q_xrun_survive_desc = policy->xrun_survive_desc;
    g_hda_mod.q_read_chunk_desc = policy->read_chunk_desc;
    g_hda_mod.q_write_quantum_desc = policy->write_quantum_desc;

    if (g_hda_mod.q_pos_mode > HDA_POS_MODE_TIMER_LPIB_GUARD) {
        g_hda_mod.q_pos_mode = HDA_POS_MODE_LPIB;
    }
    if (g_hda_mod.q_prebuffer_desc == 0u ||
        g_hda_mod.q_prebuffer_desc >= HDA_BDL_ENTRIES) {
        g_hda_mod.q_prebuffer_desc = 1u;
    }
    if (g_hda_mod.q_safe_margin_desc == 0u ||
        g_hda_mod.q_safe_margin_desc >= HDA_BDL_ENTRIES) {
        g_hda_mod.q_safe_margin_desc = 1u;
    }
    if (g_hda_mod.q_lpib_guard_desc >= HDA_BDL_ENTRIES) {
        g_hda_mod.q_lpib_guard_desc = 1u;
    }
    if (g_hda_mod.q_lpib_guard_start_desc >= HDA_BDL_ENTRIES) {
        g_hda_mod.q_lpib_guard_start_desc = g_hda_mod.q_safe_margin_desc;
    }
    if (g_hda_mod.q_xrun_survive_desc == 0u ||
        g_hda_mod.q_xrun_survive_desc >= HDA_BDL_ENTRIES) {
        g_hda_mod.q_xrun_survive_desc = 1u;
    }
    if (g_hda_mod.q_read_chunk_desc == 0u) {
        g_hda_mod.q_read_chunk_desc = 1u;
    }
    if (g_hda_mod.q_write_quantum_desc == 0u) {
        g_hda_mod.q_write_quantum_desc = 1u;
    }
    g_hda_mod.position_mode = g_hda_mod.q_pos_mode;

    driver_log("driver: HDAMOD policy codec=%x mode=%u dma_pos=%u prebuf=%u margin=%u guard=%u guard_start=%u xrun=%u read=%u write=%u\n",
               g_hda_mod.codec_vendor,
               (uint32_t)g_hda_mod.q_pos_mode,
               (uint32_t)g_hda_mod.q_dma_posbuf_enable,
               (uint32_t)g_hda_mod.q_prebuffer_desc,
               (uint32_t)g_hda_mod.q_safe_margin_desc,
               (uint32_t)g_hda_mod.q_lpib_guard_desc,
               (uint32_t)g_hda_mod.q_lpib_guard_start_desc,
               (uint32_t)g_hda_mod.q_xrun_survive_desc,
               (uint32_t)g_hda_mod.q_read_chunk_desc,
               (uint32_t)g_hda_mod.q_write_quantum_desc);
}

static int hda_mod_dma_position_allowed_local(void) {
    return g_hda_mod.q_dma_posbuf_enable != 0u;
}

static void hda_mod_enable_dma_position_buffer_local(void) {
    if (g_hda_mod.dma_pos == NULL || g_hda_mod.dma_pos_phys == 0u) {
        g_hda_mod.dma_pos_enabled = 0u;
        g_hda_mod.dma_pos_trusted = 0u;
        g_hda_mod.dma_pos_logged = 0u;
        return;
    }
    if (!hda_mod_dma_position_allowed_local()) {
        g_hda_mod.dma_pos_enabled = 0u;
        g_hda_mod.dma_pos_trusted = 0u;
        g_hda_mod.dma_pos_logged = 0u;
        hda_mod_write32_local(HDA_REG_DPLBASE, 0u);
        hda_mod_write32_local(HDA_REG_DPUBASE, 0u);
        driver_log("driver: HDAMOD dma position buffer disabled by policy codec=%x mode=%u\n",
                   g_hda_mod.codec_vendor,
                   (uint32_t)g_hda_mod.q_pos_mode);
        return;
    }
    driver_memset((void *)g_hda_mod.dma_pos, 0, HDA_DMA_POS_PAGES * HDA_PAGE_BYTES);
    hda_mod_flush_range_local((const void *)g_hda_mod.dma_pos,
                              HDA_DMA_POS_PAGES * HDA_PAGE_BYTES);
    hda_mod_write32_local(HDA_REG_DPUBASE, (uint32_t)(g_hda_mod.dma_pos_phys >> 32));
    hda_mod_write32_local(HDA_REG_DPLBASE,
                          (uint32_t)((g_hda_mod.dma_pos_phys & 0xffffff80ull) |
                                     HDA_DPLBASE_ENABLE));
    g_hda_mod.dma_pos_enabled = 1u;
    g_hda_mod.dma_pos_trusted = 0u;
    g_hda_mod.dma_pos_logged = 0u;
    g_hda_mod.pcm_last_dma_pos = 0u;
    driver_log("driver: HDAMOD dma position buffer enabled phys=%lx stream_index=%u\n",
               g_hda_mod.dma_pos_phys,
               hda_mod_output_stream_index_local());
}

static void hda_mod_reset_pcm_state_local(void) {
    g_hda_mod.pcm_active = 0u;
    g_hda_mod.pcm_started = 0u;
    g_hda_mod.pcm_write_index = 0u;
    g_hda_mod.pcm_play_index = 0u;
    g_hda_mod.pcm_fill_count = 0u;
    g_hda_mod.pcm_partial_frames = 0u;
    g_hda_mod.pcm_cbl_bytes = 0u;
    g_hda_mod.pcm_last_lpib = 0u;
    g_hda_mod.pcm_last_lpib_delta = 0u;
    g_hda_mod.pcm_last_lpib_tick = 0u;
    g_hda_mod.position_mode = g_hda_mod.q_pos_mode;
    g_hda_mod.pcm_start_tick = 0u;
    g_hda_mod.pcm_start_hw_pos_bytes = 0u;
    g_hda_mod.pcm_last_dma_pos = 0u;
    g_hda_mod.pcm_last_hw_offset = 0u;
    g_hda_mod.pcm_position_lpib = 0u;
    g_hda_mod.pcm_position_tick = 0u;
    g_hda_mod.pcm_position_reject_count = 0u;
    g_hda_mod.pcm_position_valid = 0u;
    g_hda_mod.pcm_reclaim_tick = 0u;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    g_hda_mod.pcm_app_pos_bytes = 0u;
    g_hda_mod.pcm_hw_pos_bytes = 0u;
    g_hda_mod.pcm_input_sample_rate = 0u;
    g_hda_mod.pcm_input_channels = 0u;
    g_hda_mod.pcm_input_bits = 0u;
    g_hda_mod.pcm_output_sample_rate = 0u;
    g_hda_mod.pcm_last_debug_tick = 0u;
    g_hda_mod.pcm_current_call_seq = 0u;
    g_hda_mod.pcm_current_call_base_frame = 0u;
    g_hda_mod.pcm_total_input_frames = 0u;
    g_hda_mod.pcm_submit_seq = 0u;
    g_hda_mod.pcm_reclaim_seq = 0u;
    g_hda_mod.pcm_last_submit_src_last = 0u;
    g_hda_mod.pcm_last_reclaim_src_last = 0u;
    g_hda_mod.pcm_last_hw_src_frame = 0u;
    g_hda_mod.pcm_hw_jump_count = 0u;
    g_hda_mod.pcm_empty_count = 0u;
    g_hda_mod.pcm_underrun_count = 0u;
    g_hda_mod.pcm_fake_xrun_count = 0u;
    g_hda_mod.pcm_log_xrun_tick = 0u;
    g_hda_mod.pcm_log_xrun_suppressed = 0u;
    g_hda_mod.pcm_log_fake_xrun_tick = 0u;
    g_hda_mod.pcm_log_fake_xrun_suppressed = 0u;
    g_hda_mod.pcm_log_recover_tick = 0u;
    g_hda_mod.pcm_log_recover_suppressed = 0u;
    g_hda_mod.pcm_log_slow_read_tick = 0u;
    g_hda_mod.pcm_log_slow_read_suppressed = 0u;
    g_hda_mod.dma_pos_trusted = 0u;
    g_hda_mod.dma_pos_logged = 0u;
    driver_memset(g_hda_mod.pcm_desc_call, 0, sizeof(g_hda_mod.pcm_desc_call));
    driver_memset(g_hda_mod.pcm_desc_submit, 0, sizeof(g_hda_mod.pcm_desc_submit));
    driver_memset(g_hda_mod.pcm_desc_src_first, 0, sizeof(g_hda_mod.pcm_desc_src_first));
    driver_memset(g_hda_mod.pcm_desc_src_last, 0, sizeof(g_hda_mod.pcm_desc_src_last));
    driver_memset(g_hda_mod.pcm_desc_frames, 0, sizeof(g_hda_mod.pcm_desc_frames));
    driver_memset(g_hda_mod.pcm_buffer_silent, 0, sizeof(g_hda_mod.pcm_buffer_silent));
    g_hda_mod.pcm_src_remainder = 0u;
    g_hda_mod.pcm_cancel_ctx = NULL;
    g_hda_mod.pcm_cancelled = NULL;
}

static int16_t hda_mod_decode_u8_sample_local(uint8_t value) {
    return (int16_t)(((int32_t)value - 128) << 8);
}

static int16_t hda_mod_decode_le16_sample_local(const uint8_t *src) {
    return (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static void hda_mod_zero_buffer_local(uint32_t buffer_index) {
    if (buffer_index >= HDA_BDL_ENTRIES || g_hda_mod.buffers[buffer_index] == NULL) {
        return;
    }
    driver_memset(g_hda_mod.buffers[buffer_index], 0, HDA_BUFFER_BYTES);
    g_hda_mod.pcm_buffer_silent[buffer_index] = 1u;
}

static void hda_mod_zero_dma_buffer_local(uint32_t buffer_index) {
    hda_mod_zero_buffer_local(buffer_index);
    hda_mod_flush_buffer_local(buffer_index);
}

static void hda_mod_pcm_fill_silence_ahead_local(uint64_t from_pos,
                                                 uint32_t bytes) {
    uint32_t ring_bytes = g_hda_mod.pcm_cbl_bytes;
    uint32_t ring_offset;
    uint32_t left;

    if (ring_bytes == 0u || bytes == 0u) {
        return;
    }

    ring_offset = (uint32_t)(from_pos % ring_bytes);
    left = bytes;

    while (left != 0u) {
        uint32_t desc = ring_offset / HDA_BUFFER_BYTES;
        uint32_t offset = ring_offset % HDA_BUFFER_BYTES;
        uint32_t chunk = HDA_BUFFER_BYTES - offset;

        if (desc >= HDA_BDL_ENTRIES || g_hda_mod.buffers[desc] == NULL) {
            return;
        }

        if (chunk > left) {
            chunk = left;
        }

        driver_memset(g_hda_mod.buffers[desc] + offset, 0, chunk);
        g_hda_mod.pcm_buffer_silent[desc] = 1u;
        hda_mod_flush_range_local(g_hda_mod.buffers[desc] + offset, chunk);

        left -= chunk;
        ring_offset += chunk;
        if (ring_offset >= ring_bytes) {
            ring_offset = 0u;
        }
    }
}

static int hda_mod_pcm_delta_plausible_local(uint32_t delta,
                                             uint32_t elapsed_ticks,
                                             uint32_t timer_hz,
                                             uint32_t *out_plausible) {
    uint64_t expected64;
    uint64_t tolerance64;
    uint64_t plausible64;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }

    expected64 =
        ((uint64_t)HDA_OUTPUT_BYTES_PER_SECOND *
         (uint64_t)elapsed_ticks) / (uint64_t)timer_hz;
    tolerance64 =
        (uint64_t)HDA_BUFFER_BYTES *
        (uint64_t)HDA_PCM_POSITION_TOLERANCE_DESCRIPTORS;
    plausible64 = expected64 + tolerance64;

    if (g_hda_mod.pcm_cbl_bytes != 0u &&
        plausible64 > (uint64_t)g_hda_mod.pcm_cbl_bytes / 2u) {
        plausible64 = (uint64_t)g_hda_mod.pcm_cbl_bytes / 2u;
    }
    if (plausible64 < 4096u) {
        plausible64 = 4096u;
    }

    if (out_plausible != NULL) {
        *out_plausible = (uint32_t)plausible64;
    }
    return delta <= (uint32_t)plausible64;
}

static uint32_t hda_mod_read_u32_unaligned_local(const uint8_t *src, uint32_t offset) {
    return (uint32_t)src[offset] |
           ((uint32_t)src[offset + 1u] << 8) |
           ((uint32_t)src[offset + 2u] << 16) |
           ((uint32_t)src[offset + 3u] << 24);
}

static uint32_t hda_mod_sample_bytes_local(const uint8_t *src,
                                           uint32_t bytes,
                                           uint32_t offset) {
    if (src == NULL || bytes == 0u) {
        return 0u;
    }
    if (bytes < 4u) {
        uint32_t value = 0u;
        uint32_t i;

        for (i = 0; i < bytes; i++) {
            value |= (uint32_t)src[i] << (i * 8u);
        }
        return value;
    }
    if (offset > bytes - 4u) {
        offset = bytes - 4u;
    }
    return hda_mod_read_u32_unaligned_local(src, offset);
}

static void hda_mod_set_bdl_descriptor_local(uint32_t index,
                                             uint32_t length,
                                             uint32_t flags) {
    if (index >= HDA_BDL_ENTRIES) {
        return;
    }
    g_hda_mod.bdl[index].addr = g_hda_mod.buffer_phys[index];
    g_hda_mod.bdl[index].length = length;
    g_hda_mod.bdl[index].flags = flags | HDA_BDL_FLAG_IOC;
}

static void hda_mod_pcm_reset_desc_meta_local(uint32_t index) {
    if (index >= HDA_BDL_ENTRIES) {
        return;
    }
    g_hda_mod.pcm_desc_call[index] = 0u;
    g_hda_mod.pcm_desc_submit[index] = 0u;
    g_hda_mod.pcm_desc_src_first[index] = 0u;
    g_hda_mod.pcm_desc_src_last[index] = 0u;
    g_hda_mod.pcm_desc_frames[index] = 0u;
}

static int hda_mod_configure_playback_converter_local(uint16_t format) {
    (void)hda_mod_send_verb4_local(g_hda_mod.play_cad,
                                   g_hda_mod.play_dac,
                                   HDA_VERB4_SET_CONV_FORMAT,
                                   format,
                                   NULL);
    (void)hda_mod_send_verb_local(g_hda_mod.play_cad,
                                  g_hda_mod.play_dac,
                                  HDA_VERB_SET_OUTPUT_CONV_CHAN_CNT,
                                  (uint8_t)(HDA_OUT_CHANNELS - 1u),
                                  NULL);
    (void)hda_mod_send_verb_local(g_hda_mod.play_cad,
                                  g_hda_mod.play_dac,
                                  HDA_VERB_SET_CONV_STREAM_CHAN,
                                  (uint8_t)((g_hda_mod.play_stream_id << 4) | 0u),
                                  NULL);
    return 1;
}

static void hda_mod_fill_tone_buffer_local(uint32_t buffer_index,
                                           uint32_t hz,
                                           uint32_t *phase_frame) {
    int16_t *out = (int16_t *)g_hda_mod.buffers[buffer_index];
    uint32_t period_frames;
    uint32_t half_period;
    uint32_t frame;

    if (hz < 40u) {
        hz = 40u;
    }
    if (hz > 4000u) {
        hz = 4000u;
    }
    period_frames = HDA_SAMPLE_RATE / hz;
    if (period_frames < 2u) {
        period_frames = 2u;
    }
    half_period = period_frames / 2u;
    if (half_period == 0u) {
        half_period = 1u;
    }
    for (frame = 0; frame < HDA_BUFFER_FRAMES; frame++) {
        int16_t sample =
            ((*phase_frame % period_frames) < half_period) ? 0x2800 : (int16_t)-0x2800;

        out[frame * 2u] = sample;
        out[frame * 2u + 1u] = sample;
        *phase_frame = *phase_frame + 1u;
    }
    if (buffer_index < HDA_BDL_ENTRIES) {
        g_hda_mod.pcm_buffer_silent[buffer_index] = 0u;
    }
}

static void hda_mod_sd_stop_local(uint32_t sd_off) {
    uint8_t ctl0 = hda_mod_read8_local(sd_off + HDA_SD_CTL0);
    uint32_t spins;

    ctl0 = (uint8_t)(ctl0 & ~(HDA_SD_CTL_RUN | HDA_SD_CTL_INT_MASK));
    hda_mod_write8_local(sd_off + HDA_SD_CTL0, ctl0);
    for (spins = 0; spins < 1000000u; spins++) {
        if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_RUN) == 0u) {
            break;
        }
    }
}

static int hda_mod_sd_reset_local(uint32_t sd_off) {
    uint8_t ctl0;
    uint32_t spins;

    hda_mod_sd_stop_local(sd_off);
    hda_mod_write8_local(sd_off + HDA_SD_STS, HDA_SD_STS_CLEAR);
    ctl0 = (uint8_t)(hda_mod_read8_local(sd_off + HDA_SD_CTL0) &
                     ~(HDA_SD_CTL_RUN | HDA_SD_CTL_INT_MASK));
    hda_mod_write8_local(sd_off + HDA_SD_CTL0, (uint8_t)(ctl0 | HDA_SD_CTL_SRST));
    for (spins = 0; spins < 1000000u; spins++) {
        if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_SRST) != 0u) {
            break;
        }
    }
    if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_SRST) == 0u) {
        return 0;
    }
    hda_mod_write8_local(sd_off + HDA_SD_CTL0, (uint8_t)(ctl0 & ~HDA_SD_CTL_SRST));
    for (spins = 0; spins < 1000000u; spins++) {
        if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_SRST) == 0u) {
            hda_mod_write32_local(sd_off + HDA_SD_LPIB, 0u);
            hda_mod_write32_local(sd_off + HDA_SD_BDPL, 0u);
            hda_mod_write32_local(sd_off + HDA_SD_BDPU, 0u);
            return 1;
        }
    }
    return 0;
}

static void hda_mod_sd_halt_local(uint32_t sd_off) {
    hda_mod_sd_stop_local(sd_off);
    hda_mod_write8_local(sd_off + HDA_SD_STS, HDA_SD_STS_CLEAR);
}

static int hda_mod_sd_start_nowait_local(uint32_t sd_off,
                                         uint8_t stream_id,
                                         uint16_t format,
                                         uint32_t cbl_bytes,
                                         uint8_t lvi,
                                         uint64_t bdl_phys) {
    uint8_t ctl2;
    uint8_t ctl0;

    if (!hda_mod_sd_reset_local(sd_off)) {
        return 0;
    }
    hda_mod_enable_dma_position_buffer_local();
    hda_mod_write32_local(sd_off + HDA_SD_LPIB, 0u);
    hda_mod_write8_local(sd_off + HDA_SD_STS, HDA_SD_STS_CLEAR);
    hda_mod_write32_local(sd_off + HDA_SD_BDPL, (uint32_t)(bdl_phys & 0xfffffc00u));
    hda_mod_write32_local(sd_off + HDA_SD_BDPU, (uint32_t)(bdl_phys >> 32));

    /* Memory barrier after BDL setup to ensure data is visible to DMA */
    __asm__ __volatile__("mfence" ::: "memory");
    hda_mod_delay_local(1000u);

    if (HDA_PCM_TRACE != 0u) {
        hda_mod_log("DBG sd_start sd_off=0x%x bdpl=0x%08x bdpu=0x%08x cbl=%u lvi=%u bdl_phys=%llu\n",
                    sd_off,
                    (uint32_t)(bdl_phys & 0xffffffffu),
                    (uint32_t)(bdl_phys >> 32),
                    cbl_bytes,
                    (uint32_t)lvi,
                    (unsigned long long)bdl_phys);
    }

    hda_mod_write32_local(sd_off + HDA_SD_CBL, cbl_bytes);
    hda_mod_write16_local(sd_off + HDA_SD_LVI, lvi);
    hda_mod_write16_local(sd_off + HDA_SD_FMT, format);
    ctl2 = hda_mod_read8_local(sd_off + HDA_SD_CTL2);
    ctl2 = (uint8_t)((ctl2 & 0x0fu) | ((stream_id & 0x0fu) << 4));
    hda_mod_write8_local(sd_off + HDA_SD_CTL2, ctl2);

    /* Memory barrier before starting DMA */
    __asm__ __volatile__("mfence" ::: "memory");
    hda_mod_delay_local(1000u);

    ctl0 = hda_mod_read8_local(sd_off + HDA_SD_CTL0);
    hda_mod_write8_local(sd_off + HDA_SD_CTL0,
                         (uint8_t)(ctl0 | HDA_SD_CTL_IOCE | HDA_SD_CTL_RUN));
    return 1;
}

static int hda_mod_sd_start_local(uint32_t sd_off,
                                  uint8_t stream_id,
                                  uint16_t format,
                                  uint32_t cbl_bytes,
                                  uint8_t lvi,
                                  uint64_t bdl_phys) {
    uint32_t lpib_start;
    uint32_t spins;

    if (!hda_mod_sd_start_nowait_local(sd_off,
                                       stream_id,
                                       format,
                                       cbl_bytes,
                                       lvi,
                                       bdl_phys)) {
        return 0;
    }
    lpib_start = hda_mod_read32_local(sd_off + HDA_SD_LPIB);
    for (spins = 0; spins < 2000000u; spins++) {
        if (hda_mod_read32_local(sd_off + HDA_SD_LPIB) != lpib_start) {
            return 1;
        }
    }
    driver_log("driver: HDAMOD lpib did not advance yet sd=%x ctl=%x lpib=%u\n",
               sd_off,
               (uint32_t)hda_mod_read8_local(sd_off + HDA_SD_CTL0),
               lpib_start);
    return (hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_RUN) != 0u;
}

static void hda_mod_zero_all_pcm_buffers_local(void) {
    uint32_t index;

    for (index = 0; index < HDA_BDL_ENTRIES; index++) {
        hda_mod_zero_dma_buffer_local(index);
        hda_mod_set_bdl_descriptor_local(index, HDA_BUFFER_BYTES, 0u);
        hda_mod_pcm_reset_desc_meta_local(index);
    }
    hda_mod_flush_bdl_local();
}

static uint64_t hda_mod_pos_get_lpib_bytes_local(void) {
    uint32_t sd_off = g_hda_mod.output_stream_offset;
    uint32_t lpib;
    uint32_t verify;

    if (g_hda_mod.pcm_cbl_bytes == 0u) {
        return 0u;
    }
    lpib = hda_mod_read32_local(sd_off + HDA_SD_LPIB);
    hda_mod_delay_local(100u);
    verify = hda_mod_read32_local(sd_off + HDA_SD_LPIB);
    if (verify != lpib) {
        lpib = verify;
    }
    return lpib % g_hda_mod.pcm_cbl_bytes;
}

static uint64_t hda_mod_pos_get_dma_posbuf_bytes_local(void) {
    uint32_t stream_index;
    uint32_t dword_index;
    uint32_t position;
    uint32_t verify;

    if (g_hda_mod.dma_pos_enabled == 0u ||
        g_hda_mod.dma_pos == NULL ||
        g_hda_mod.pcm_cbl_bytes == 0u) {
        return hda_mod_pos_get_lpib_bytes_local();
    }

    stream_index = hda_mod_output_stream_index_local();
    dword_index = stream_index * 2u;
    if (dword_index >= HDA_PAGE_BYTES / sizeof(uint32_t)) {
        return hda_mod_pos_get_lpib_bytes_local();
    }

    hda_mod_flush_range_local((const void *)&g_hda_mod.dma_pos[dword_index],
                              sizeof(uint64_t));
    position = g_hda_mod.dma_pos[dword_index];
    __asm__ __volatile__("lfence" ::: "memory");
    verify = g_hda_mod.dma_pos[dword_index];
    if (verify != position) {
        position = verify;
    }
    g_hda_mod.pcm_last_dma_pos = position;
    return position % g_hda_mod.pcm_cbl_bytes;
}

static int hda_mod_pcm_input_matches_local(uint32_t sample_rate,
                                           uint32_t channels,
                                           uint32_t bits_per_sample,
                                           uint32_t output_sample_rate) {
    return g_hda_mod.pcm_input_sample_rate == sample_rate &&
           g_hda_mod.pcm_input_channels == channels &&
           g_hda_mod.pcm_input_bits == bits_per_sample &&
           g_hda_mod.pcm_output_sample_rate == output_sample_rate;
}

static uint16_t hda_mod_pcm_format_for_rate_local(uint32_t sample_rate,
                                                  uint32_t *out_output_rate) {
    (void)sample_rate;
    if (out_output_rate != NULL) {
        *out_output_rate = HDA_SAMPLE_RATE;
    }
    return HDA_STREAM_FORMAT_48K_16B_2CH;
}

static uint32_t hda_mod_pcm_ring_bytes_local(void) {
    return HDA_BUFFER_BYTES * HDA_BDL_ENTRIES;
}

static uint32_t hda_mod_pcm_safe_margin_bytes_local(void) {
    return HDA_BUFFER_BYTES * g_hda_mod.q_safe_margin_desc;
}

static uint32_t hda_mod_pcm_lpib_guard_bytes_local(void) {
    return HDA_BUFFER_BYTES * g_hda_mod.q_lpib_guard_desc;
}

static uint32_t hda_mod_pcm_write_quantum_bytes_local(void) {
    return HDA_BUFFER_BYTES * g_hda_mod.q_write_quantum_desc;
}

static uint32_t hda_mod_stream_read_chunk_bytes_local(void) {
    return HDA_BUFFER_BYTES * g_hda_mod.q_read_chunk_desc;
}

static uint32_t hda_mod_pcm_xrun_survive_bytes_local(void) {
    return HDA_BUFFER_BYTES * g_hda_mod.q_xrun_survive_desc;
}

static uint32_t hda_mod_pcm_ring_forward_distance_local(uint32_t from,
                                                        uint32_t to,
                                                        uint32_t ring_bytes) {
    if (ring_bytes == 0u) {
        return 0u;
    }
    from %= ring_bytes;
    to %= ring_bytes;
    if (to >= from) {
        return to - from;
    }
    return ring_bytes - from + to;
}

static int hda_mod_pos_mode_uses_timer_local(void) {
    return g_hda_mod.position_mode == HDA_POS_MODE_TIMER ||
           g_hda_mod.position_mode == HDA_POS_MODE_TIMER_LPIB_GUARD;
}

static int hda_mod_pos_mode_uses_lpib_guard_local(void) {
    return g_hda_mod.position_mode == HDA_POS_MODE_TIMER_LPIB_GUARD;
}

static uint32_t hda_mod_pcm_guarded_write_bytes_local(uint32_t write_offset,
                                                      uint32_t wanted_bytes) {
    uint32_t ring_bytes = g_hda_mod.pcm_cbl_bytes;
    uint32_t lpib;
    uint32_t guard;
    uint32_t start_dist;
    uint32_t distance_to_guard;
    uint32_t safe_bytes;
    uint64_t queued;

    if (wanted_bytes == 0u ||
        g_hda_mod.pcm_started == 0u ||
        g_hda_mod.pcm_active == 0u ||
        !hda_mod_pos_mode_uses_lpib_guard_local()) {
        return wanted_bytes;
    }
    queued = g_hda_mod.pcm_app_pos_bytes > g_hda_mod.pcm_hw_pos_bytes ?
        g_hda_mod.pcm_app_pos_bytes - g_hda_mod.pcm_hw_pos_bytes : 0u;
    if (queued <=
        (uint64_t)HDA_BUFFER_BYTES * g_hda_mod.q_lpib_guard_start_desc) {
        return wanted_bytes;
    }
    if (ring_bytes == 0u) {
        return wanted_bytes;
    }

    lpib = (uint32_t)hda_mod_pos_get_lpib_bytes_local();
    guard = hda_mod_pcm_lpib_guard_bytes_local();
    if (guard == 0u) {
        return wanted_bytes;
    }
    write_offset %= ring_bytes;
    if (wanted_bytes >= ring_bytes) {
        wanted_bytes = ring_bytes - 4u;
    }
    start_dist = hda_mod_pcm_ring_forward_distance_local(lpib, write_offset, ring_bytes);
    if (start_dist < guard) {
        safe_bytes = 0u;
    } else {
        distance_to_guard = ring_bytes - start_dist;
        safe_bytes = distance_to_guard > guard ?
            distance_to_guard - guard : 0u;
        if (safe_bytes > wanted_bytes) {
            safe_bytes = wanted_bytes;
        }
    }
    safe_bytes &= ~3u;

    if (HDA_PCM_LPIB_GUARD_TRACE != 0u && safe_bytes != wanted_bytes) {
        driver_log("driver: HDAMOD guarded write lpib=%u write=%u wanted=%u safe=%u start_dist=%u guard=%u queued=%lx app=%lx hw=%lx\n",
                   lpib,
                   write_offset,
                   wanted_bytes,
                   safe_bytes,
                   start_dist,
                   guard,
                   queued,
                   g_hda_mod.pcm_app_pos_bytes,
                   g_hda_mod.pcm_hw_pos_bytes);
    }
    return safe_bytes;
}

static uint32_t hda_mod_pcm_prebuffer_bytes_local(void) {
    return HDA_BUFFER_BYTES * g_hda_mod.q_prebuffer_desc;
}

static uint32_t hda_mod_pcm_output_bytes_remaining_local(uint64_t src_pos,
                                                         uint64_t input_end,
                                                         uint64_t src_step) {
    uint64_t remaining;
    uint64_t frames;

    if (src_pos >= input_end || src_step == 0u) {
        return 0u;
    }
    remaining = input_end - src_pos;
    frames = (remaining + src_step - 1u) / src_step;
    if (frames > 0xffffffffu / 4u) {
        return 0xffffffffu;
    }
    return (uint32_t)frames * 4u;
}

static uint64_t hda_mod_pcm_queued_bytes_local(void) {
    if (g_hda_mod.pcm_app_pos_bytes <= g_hda_mod.pcm_hw_pos_bytes) {
        return 0u;
    }
    return g_hda_mod.pcm_app_pos_bytes - g_hda_mod.pcm_hw_pos_bytes;
}

static uint64_t hda_mod_pos_get_timer_bytes_local(void) {
    uint32_t now;
    uint32_t timer_hz;
    uint32_t elapsed_ticks;
    uint64_t elapsed_bytes;

    now = driver_timer_current_ticks();
    timer_hz = driver_timer_hz();
    if (timer_hz == 0u) {
        timer_hz = 100u;
    }

    elapsed_ticks = now - g_hda_mod.pcm_start_tick;
    elapsed_bytes =
        ((uint64_t)HDA_OUTPUT_BYTES_PER_SECOND *
         (uint64_t)elapsed_ticks) / (uint64_t)timer_hz;

    return g_hda_mod.pcm_start_hw_pos_bytes + elapsed_bytes;
}

static uint64_t hda_mod_pos_get_hw_bytes_local(void) {
    switch (g_hda_mod.position_mode) {
        case HDA_POS_MODE_DMA_POSBUF:
            return hda_mod_pos_get_dma_posbuf_bytes_local();
        case HDA_POS_MODE_TIMER:
        case HDA_POS_MODE_TIMER_LPIB_GUARD:
            return hda_mod_pos_get_timer_bytes_local();
        case HDA_POS_MODE_LPIB:
        default:
            return hda_mod_pos_get_lpib_bytes_local();
    }
}

static uint64_t hda_mod_pcm_free_bytes_local(void) {
    uint64_t queued;
    uint32_t ring_bytes;

    ring_bytes = g_hda_mod.pcm_cbl_bytes != 0u ?
        g_hda_mod.pcm_cbl_bytes : hda_mod_pcm_ring_bytes_local();
    queued = hda_mod_pcm_queued_bytes_local();
    if (queued >= ring_bytes) {
        return 0u;
    }
    return (uint64_t)ring_bytes - queued;
}

static uint32_t hda_mod_pcm_lpib_delta_local(uint32_t old_lpib,
                                             uint32_t new_lpib,
                                             uint32_t ring_bytes) {
    if (ring_bytes == 0u) {
        return 0u;
    }
    old_lpib %= ring_bytes;
    new_lpib %= ring_bytes;
    if (new_lpib >= old_lpib) {
        return new_lpib - old_lpib;
    }
    return ring_bytes - old_lpib + new_lpib;
}

static void hda_mod_pcm_sync_debug_indices_local(void) {
    uint32_t ring_bytes = g_hda_mod.pcm_cbl_bytes;
    uint64_t queued = hda_mod_pcm_queued_bytes_local();

    if (ring_bytes == 0u) {
        g_hda_mod.pcm_write_index = 0u;
        g_hda_mod.pcm_play_index = 0u;
        g_hda_mod.pcm_fill_count = 0u;
        g_hda_mod.pcm_partial_frames = 0u;
        g_hda_mod.pcm_reclaim_bytes = 0u;
        return;
    }
    if (queued > ring_bytes) {
        queued = ring_bytes;
    }
    g_hda_mod.pcm_play_index =
        (uint8_t)(((uint32_t)(g_hda_mod.pcm_hw_pos_bytes % ring_bytes) /
                   HDA_BUFFER_BYTES) % HDA_BDL_ENTRIES);
    g_hda_mod.pcm_write_index =
        (uint8_t)(((uint32_t)(g_hda_mod.pcm_app_pos_bytes % ring_bytes) /
                   HDA_BUFFER_BYTES) % HDA_BDL_ENTRIES);
    g_hda_mod.pcm_fill_count =
        (uint8_t)((queued + HDA_BUFFER_BYTES - 1u) / HDA_BUFFER_BYTES);
    g_hda_mod.pcm_partial_frames =
        ((uint32_t)(g_hda_mod.pcm_app_pos_bytes % HDA_BUFFER_BYTES)) / 4u;
    g_hda_mod.pcm_reclaim_bytes = (uint32_t)(queued % HDA_BUFFER_BYTES);
}

static void hda_mod_pcm_log_byte_state_local(const char *event) {
    driver_log("driver: HDAMOD byte state event=%s lpib=%u delta=%u app=%lx hw=%lx queued=%lx free=%lx started=%u active=%u play=%u write=%u fill=%u\n",
               event,
               g_hda_mod.pcm_last_lpib,
               g_hda_mod.pcm_last_lpib_delta,
               g_hda_mod.pcm_app_pos_bytes,
               g_hda_mod.pcm_hw_pos_bytes,
               hda_mod_pcm_queued_bytes_local(),
               hda_mod_pcm_free_bytes_local(),
               (uint32_t)g_hda_mod.pcm_started,
               (uint32_t)g_hda_mod.pcm_active,
               (uint32_t)g_hda_mod.pcm_play_index,
               (uint32_t)g_hda_mod.pcm_write_index,
               (uint32_t)g_hda_mod.pcm_fill_count);
}

static void hda_mod_pcm_prepare_byte_ring_local(void) {
    g_hda_mod.pcm_cbl_bytes = hda_mod_pcm_ring_bytes_local();
    g_hda_mod.pcm_app_pos_bytes = 0u;
    g_hda_mod.pcm_hw_pos_bytes = 0u;
    g_hda_mod.pcm_last_lpib = 0u;
    g_hda_mod.pcm_last_hw_offset = 0u;
    g_hda_mod.pcm_last_lpib_delta = 0u;
    g_hda_mod.pcm_last_lpib_tick = 0u;
    g_hda_mod.pcm_start_tick = 0u;
    g_hda_mod.pcm_start_hw_pos_bytes = 0u;
    g_hda_mod.pcm_last_dma_pos = 0u;
    g_hda_mod.pcm_position_lpib = 0u;
    g_hda_mod.pcm_position_tick = 0u;
    g_hda_mod.pcm_position_reject_count = 0u;
    g_hda_mod.pcm_position_valid = 0u;
    g_hda_mod.pcm_reclaim_tick = 0u;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    hda_mod_pcm_sync_debug_indices_local();
}

static void hda_mod_pcm_flush_ring_range_local(uint32_t start, uint32_t bytes) {
    uint32_t ring_bytes = g_hda_mod.pcm_cbl_bytes;

    if (ring_bytes == 0u || bytes == 0u) {
        return;
    }
    start %= ring_bytes;
    while (bytes != 0u) {
        uint32_t desc = start / HDA_BUFFER_BYTES;
        uint32_t offset = start % HDA_BUFFER_BYTES;
        uint32_t chunk = HDA_BUFFER_BYTES - offset;

        if (desc >= HDA_BDL_ENTRIES || g_hda_mod.buffers[desc] == NULL) {
            return;
        }
        if (chunk > bytes) {
            chunk = bytes;
        }
        hda_mod_flush_range_local(g_hda_mod.buffers[desc] + offset, chunk);
        bytes -= chunk;
        start += chunk;
        if (start >= ring_bytes) {
            start = 0u;
        }
    }
}

static uint32_t hda_mod_pcm_fill_ring_frames_local(uint32_t ring_offset,
                                                   uint32_t max_frames,
                                                   const uint8_t *src,
                                                   uint32_t input_frames,
                                                   uint64_t *src_pos,
                                                   uint64_t src_step,
                                                   uint32_t src_stride,
                                                   uint32_t channels,
                                                   uint32_t bits_per_sample) {
    uint32_t written_frames = 0u;
    uint32_t ring_bytes = g_hda_mod.pcm_cbl_bytes;

    if (ring_bytes == 0u || src == NULL || src_pos == NULL ||
        src_stride == 0u || src_step == 0u) {
        return 0u;
    }
    ring_offset %= ring_bytes;
    while (written_frames < max_frames &&
           (uint32_t)(*src_pos >> 32) < input_frames) {
        uint32_t desc = ring_offset / HDA_BUFFER_BYTES;
        uint32_t offset = ring_offset % HDA_BUFFER_BYTES;
        uint32_t segment_frames = (HDA_BUFFER_BYTES - offset) / 4u;
        uint32_t segment_written = 0u;
        int16_t *out;

        if (desc >= HDA_BDL_ENTRIES || g_hda_mod.buffers[desc] == NULL) {
            break;
        }
        if (segment_frames > max_frames - written_frames) {
            segment_frames = max_frames - written_frames;
        }
        out = (int16_t *)(g_hda_mod.buffers[desc] + offset);
        while (segment_written < segment_frames) {
            uint32_t src_frame = (uint32_t)(*src_pos >> 32);
            uint32_t src_offset;
            int16_t left;
            int16_t right;

            if (src_frame >= input_frames) {
                break;
            }
            src_offset = src_frame * src_stride;
            if (bits_per_sample == 8u) {
                left = hda_mod_decode_u8_sample_local(src[src_offset]);
                right = channels == 1u ? left :
                    hda_mod_decode_u8_sample_local(src[src_offset + 1u]);
            } else {
                left = hda_mod_decode_le16_sample_local(src + src_offset);
                right = channels == 1u ? left :
                    hda_mod_decode_le16_sample_local(src + src_offset + 2u);
            }
            out[segment_written * 2u] = left;
            out[segment_written * 2u + 1u] = right;
            *src_pos += src_step;
            segment_written++;
        }
        if (segment_written == 0u) {
            break;
        }
        g_hda_mod.pcm_buffer_silent[desc] = 0u;
        written_frames += segment_written;
        ring_offset += segment_written * 4u;
        if (ring_offset >= ring_bytes) {
            ring_offset -= ring_bytes;
        }
    }
    return written_frames;
}

static void hda_mod_pcm_ack_bcis_local(void) {
    uint32_t bcis =
        hda_mod_read8_local(g_hda_mod.output_stream_offset + HDA_SD_STS) &
        HDA_SD_STS_BCIS;

    if (bcis != 0u) {
        hda_mod_write8_local(g_hda_mod.output_stream_offset + HDA_SD_STS,
                             HDA_SD_STS_BCIS);
    }
}

static void hda_mod_pcm_update_timer_position_local(uint32_t halt_on_xrun) {
    uint64_t old_hw = g_hda_mod.pcm_hw_pos_bytes;
    uint64_t timer_hw = hda_mod_pos_get_hw_bytes_local();
    uint64_t advanced;

    (void)halt_on_xrun;
    if (timer_hw > g_hda_mod.pcm_app_pos_bytes) {
        timer_hw = g_hda_mod.pcm_app_pos_bytes;
    }
    if (timer_hw > old_hw) {
        advanced = timer_hw - old_hw;
        if (advanced > 0xffffffffu) {
            advanced = 0xffffffffu;
        }
        g_hda_mod.pcm_hw_pos_bytes = timer_hw;
        g_hda_mod.pcm_last_lpib_delta = (uint32_t)advanced;
    } else {
        g_hda_mod.pcm_last_lpib_delta = 0u;
    }

    g_hda_mod.pcm_last_hw_offset =
        (uint32_t)(g_hda_mod.pcm_hw_pos_bytes % g_hda_mod.pcm_cbl_bytes);
    g_hda_mod.pcm_last_lpib = (uint32_t)hda_mod_pos_get_lpib_bytes_local();
    g_hda_mod.pcm_last_lpib_tick = driver_timer_current_ticks();
}

static void hda_mod_pcm_update_register_position_local(uint64_t queued_before) {
    uint32_t position =
        (uint32_t)(hda_mod_pos_get_hw_bytes_local() % g_hda_mod.pcm_cbl_bytes);
    uint32_t now = driver_timer_current_ticks();
    uint32_t delta;
    uint32_t timer_hz;
    uint32_t elapsed_ticks;
    uint32_t plausible_delta = 0u;

    g_hda_mod.pcm_last_lpib = (uint32_t)hda_mod_pos_get_lpib_bytes_local();
    if (g_hda_mod.pcm_last_lpib_tick == 0u) {
        g_hda_mod.pcm_last_hw_offset = position;
        g_hda_mod.pcm_last_lpib_delta = 0u;
        g_hda_mod.pcm_last_lpib_tick = now;
        return;
    }

    delta = hda_mod_pcm_lpib_delta_local(g_hda_mod.pcm_last_hw_offset,
                                         position,
                                         g_hda_mod.pcm_cbl_bytes);
    timer_hz = driver_timer_hz();
    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    elapsed_ticks = now - g_hda_mod.pcm_last_lpib_tick;

    if (hda_mod_pcm_delta_plausible_local(delta,
                                          elapsed_ticks,
                                          timer_hz,
                                          &plausible_delta)) {
        g_hda_mod.pcm_hw_pos_bytes += delta;
        g_hda_mod.pcm_last_lpib_delta = delta;
    } else {
        uint32_t wall_delta =
            (uint32_t)(((uint64_t)HDA_OUTPUT_BYTES_PER_SECOND *
                        (uint64_t)elapsed_ticks) / (uint64_t)timer_hz);

        if (wall_delta > queued_before) {
            wall_delta = (uint32_t)queued_before;
        }
        g_hda_mod.pcm_hw_pos_bytes += wall_delta;
        g_hda_mod.pcm_last_lpib_delta = wall_delta;
        g_hda_mod.pcm_position_reject_count++;

        if (hda_mod_log_ratelimit_local(&g_hda_mod.pcm_log_recover_tick,
                                        &g_hda_mod.pcm_log_recover_suppressed)) {
            driver_log("driver: HDAMOD position recover mode=%u count=%u old=%u new=%u raw_delta=%u wall_delta=%u plausible=%u ticks=%u app=%lx hw=%lx queued=%lx free=%lx suppressed=%u\n",
                       (uint32_t)g_hda_mod.position_mode,
                       g_hda_mod.pcm_position_reject_count,
                       g_hda_mod.pcm_last_hw_offset,
                       position,
                       delta,
                       wall_delta,
                       plausible_delta,
                       elapsed_ticks,
                       g_hda_mod.pcm_app_pos_bytes,
                       g_hda_mod.pcm_hw_pos_bytes,
                       hda_mod_pcm_queued_bytes_local(),
                       hda_mod_pcm_free_bytes_local(),
                       hda_mod_log_take_suppressed_local(
                           &g_hda_mod.pcm_log_recover_suppressed));
        }
    }

    g_hda_mod.pcm_last_hw_offset = position;
    g_hda_mod.pcm_last_lpib_tick = now;
}

static uint64_t hda_mod_pcm_timer_keep_back_bytes_local(void) {
    return HDA_BUFFER_BYTES * 4u;
}

static int hda_mod_pcm_handle_fake_xrun_local(uint64_t queued_before) {
    uint64_t timer_keep_back;

    if (!hda_mod_pos_mode_uses_timer_local()) {
        return 0;
    }
    timer_keep_back = hda_mod_pcm_timer_keep_back_bytes_local();
    if (queued_before <= timer_keep_back) {
        return 0;
    }

    g_hda_mod.pcm_fake_xrun_count++;
    if (g_hda_mod.pcm_app_pos_bytes > timer_keep_back) {
        g_hda_mod.pcm_hw_pos_bytes =
            g_hda_mod.pcm_app_pos_bytes - timer_keep_back;
    } else {
        g_hda_mod.pcm_hw_pos_bytes = 0u;
    }
    g_hda_mod.pcm_start_tick = driver_timer_current_ticks();
    g_hda_mod.pcm_start_hw_pos_bytes = g_hda_mod.pcm_hw_pos_bytes;
    g_hda_mod.pcm_last_lpib_tick = g_hda_mod.pcm_start_tick;
    g_hda_mod.pcm_last_hw_offset =
        (uint32_t)(g_hda_mod.pcm_hw_pos_bytes %
                   g_hda_mod.pcm_cbl_bytes);
    g_hda_mod.pcm_last_lpib_delta = 0u;
    g_hda_mod.pcm_started = 1u;
    g_hda_mod.pcm_active = 1u;
    hda_mod_pcm_sync_debug_indices_local();

    if (hda_mod_log_ratelimit_local(&g_hda_mod.pcm_log_fake_xrun_tick,
                                    &g_hda_mod.pcm_log_fake_xrun_suppressed)) {
        driver_log("driver: HDAMOD fake XRUN rebase mode=%u count=%u keep=%lx queued_before=%lx app=%lx hw=%lx queued=%lx suppressed=%u\n",
                   (uint32_t)g_hda_mod.position_mode,
                   g_hda_mod.pcm_fake_xrun_count,
                   timer_keep_back,
                   queued_before,
                   g_hda_mod.pcm_app_pos_bytes,
                   g_hda_mod.pcm_hw_pos_bytes,
                   hda_mod_pcm_queued_bytes_local(),
                   hda_mod_log_take_suppressed_local(
                       &g_hda_mod.pcm_log_fake_xrun_suppressed));
    }
    return 1;
}

static void hda_mod_pcm_handle_xrun_local(uint32_t halt_on_xrun,
                                          uint64_t queued_before) {
    uint32_t silence_bytes;
    uint64_t target_app_pos;

    if (g_hda_mod.pcm_cbl_bytes == 0u) {
        return;
    }
    if (g_hda_mod.pcm_hw_pos_bytes < g_hda_mod.pcm_app_pos_bytes) {
        return;
    }
    if (halt_on_xrun == 0u) {
        hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
        g_hda_mod.pcm_started = 0u;
        g_hda_mod.pcm_active = 0u;
        hda_mod_pcm_sync_debug_indices_local();
        return;
    }

    if (halt_on_xrun != 0u && hda_mod_pcm_handle_fake_xrun_local(queued_before)) {
        return;
    }

    g_hda_mod.pcm_underrun_count++;
    if (hda_mod_log_ratelimit_local(&g_hda_mod.pcm_log_xrun_tick,
                                    &g_hda_mod.pcm_log_xrun_suppressed)) {
        driver_log("driver: HDAMOD XRUN survive mode=%u count=%u app=%lx hw=%lx queued_before=%lx lpib=%u pad_desc=%u call=%u base=%u total=%u rem=%u:%x play=%u write=%u fill=%u suppressed=%u\n",
                   (uint32_t)g_hda_mod.position_mode,
                   g_hda_mod.pcm_underrun_count,
                   g_hda_mod.pcm_app_pos_bytes,
                   g_hda_mod.pcm_hw_pos_bytes,
                   queued_before,
                   g_hda_mod.pcm_last_lpib,
                   (uint32_t)g_hda_mod.q_xrun_survive_desc,
                   g_hda_mod.pcm_current_call_seq,
                   g_hda_mod.pcm_current_call_base_frame,
                   g_hda_mod.pcm_total_input_frames,
                   (uint32_t)(g_hda_mod.pcm_src_remainder >> 32),
                   (uint32_t)g_hda_mod.pcm_src_remainder,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   hda_mod_log_take_suppressed_local(
                       &g_hda_mod.pcm_log_xrun_suppressed));
    }

    silence_bytes = hda_mod_pcm_xrun_survive_bytes_local();
    if (silence_bytes == 0u) {
        silence_bytes = HDA_BUFFER_BYTES;
    }
    hda_mod_pcm_fill_silence_ahead_local(g_hda_mod.pcm_hw_pos_bytes,
                                         silence_bytes);
    target_app_pos = g_hda_mod.pcm_hw_pos_bytes + silence_bytes;
    if (g_hda_mod.pcm_app_pos_bytes < target_app_pos) {
        g_hda_mod.pcm_app_pos_bytes = target_app_pos;
    }

    if (hda_mod_pos_mode_uses_timer_local()) {
        g_hda_mod.pcm_start_tick = driver_timer_current_ticks();
        g_hda_mod.pcm_start_hw_pos_bytes = g_hda_mod.pcm_hw_pos_bytes;
        g_hda_mod.pcm_last_lpib_tick = g_hda_mod.pcm_start_tick;
    }
    g_hda_mod.pcm_started = 1u;
    g_hda_mod.pcm_active = 1u;
    hda_mod_pcm_sync_debug_indices_local();
}

static int hda_mod_pcm_update_hw_pos_local(uint32_t halt_on_xrun) {
    uint64_t queued_before;

    if (g_hda_mod.pcm_started == 0u || g_hda_mod.pcm_cbl_bytes == 0u) {
        g_hda_mod.pcm_last_lpib_delta = 0u;
        hda_mod_pcm_sync_debug_indices_local();
        return 1;
    }

    queued_before = hda_mod_pcm_queued_bytes_local();
    if (hda_mod_pos_mode_uses_timer_local()) {
        hda_mod_pcm_update_timer_position_local(halt_on_xrun);
    } else {
        hda_mod_pcm_update_register_position_local(queued_before);
    }

    hda_mod_pcm_ack_bcis_local();
    if (g_hda_mod.pcm_hw_pos_bytes >= g_hda_mod.pcm_app_pos_bytes) {
        hda_mod_pcm_handle_xrun_local(halt_on_xrun, queued_before);
        hda_mod_pcm_sync_debug_indices_local();
        return 1;
    }

    if ((hda_mod_read8_local(g_hda_mod.output_stream_offset + HDA_SD_CTL0) &
         HDA_SD_CTL_RUN) == 0u) {
        g_hda_mod.pcm_active = 0u;
    }

    hda_mod_pcm_sync_debug_indices_local();
    return 1;
}

static int hda_mod_pcm_start_local(uint16_t format) {
    uint32_t sd_off = g_hda_mod.output_stream_offset;
    uint32_t initial_hw_offset;
    uint64_t queued;
    uint32_t start_tick;

    if (g_hda_mod.pcm_started != 0u) {
        return 1;
    }
    if (g_hda_mod.pcm_cbl_bytes == 0u) {
        g_hda_mod.pcm_cbl_bytes = hda_mod_pcm_ring_bytes_local();
    }
    queued = hda_mod_pcm_queued_bytes_local();
    if (queued == 0u) {
        return 1;
    }

    hda_mod_flush_bdl_local();

    if (!hda_mod_sd_start_local(sd_off,
                                g_hda_mod.play_stream_id,
                                format,
                                g_hda_mod.pcm_cbl_bytes,
                                (uint8_t)(HDA_BDL_ENTRIES - 1u),
                                g_hda_mod.bdl_phys)) {
        return 0;
    }

    hda_mod_delay_local(10000u);

    g_hda_mod.position_mode = g_hda_mod.q_pos_mode;
    start_tick = driver_timer_current_ticks();
    g_hda_mod.pcm_start_tick = start_tick;
    g_hda_mod.pcm_start_hw_pos_bytes = 0u;
    g_hda_mod.pcm_hw_pos_bytes = 0u;
    initial_hw_offset =
        (uint32_t)(hda_mod_pos_get_hw_bytes_local() % g_hda_mod.pcm_cbl_bytes);
    g_hda_mod.pcm_last_hw_offset = initial_hw_offset;
    g_hda_mod.pcm_last_lpib = (uint32_t)hda_mod_pos_get_lpib_bytes_local();
    g_hda_mod.pcm_last_lpib_delta = 0u;
    g_hda_mod.pcm_position_lpib = g_hda_mod.pcm_last_lpib;
    g_hda_mod.pcm_position_tick = start_tick;
    g_hda_mod.pcm_last_lpib_tick = start_tick;
    g_hda_mod.pcm_position_reject_count = 0u;
    g_hda_mod.pcm_position_valid = 1u;
    g_hda_mod.pcm_reclaim_tick = start_tick;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    g_hda_mod.pcm_active = 1u;
    g_hda_mod.pcm_started = 1u;

    hda_mod_pcm_sync_debug_indices_local();
    driver_log("driver: HDAMOD byte ring start ok sd=%x ctl=%x cbl=%u queued=%lx app=%lx hw=%lx play=%u write=%u lpib=%u posmode=%u timer_hz=%u\n",
               sd_off,
               (uint32_t)hda_mod_read8_local(sd_off + HDA_SD_CTL0),
               g_hda_mod.pcm_cbl_bytes,
               hda_mod_pcm_queued_bytes_local(),
               g_hda_mod.pcm_app_pos_bytes,
               g_hda_mod.pcm_hw_pos_bytes,
               (uint32_t)g_hda_mod.pcm_play_index,
               (uint32_t)g_hda_mod.pcm_write_index,
               g_hda_mod.pcm_last_lpib,
               (uint32_t)g_hda_mod.position_mode,
               driver_timer_hz());
    return 1;
}

static int hda_mod_pcm_wait_for_byte_space_local(uint16_t format,
                                                  uint32_t min_bytes) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t start_ticks;
    uint32_t timeout_ticks;
    uint32_t safe_margin = hda_mod_pcm_safe_margin_bytes_local();

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    start_ticks = driver_timer_current_ticks();
    timeout_ticks = timer_hz * 5u;
    if (timeout_ticks < 10u) {
        timeout_ticks = 10u;
    }
    if (min_bytes < 4u) {
        min_bytes = 4u;
    }
    while (1) {
        uint64_t free_bytes;
        uint64_t queued;

        if (g_hda_mod.pcm_started != 0u) {
            if (!hda_mod_pcm_update_hw_pos_local(1u)) {
                return 0;
            }
        }
        queued = hda_mod_pcm_queued_bytes_local();
        free_bytes = hda_mod_pcm_free_bytes_local();
        if (free_bytes > safe_margin &&
            free_bytes - safe_margin >= min_bytes) {
            return 1;
        }
        if (hda_mod_pcm_cancelled_local()) {
            return 0;
        }
        if (g_hda_mod.pcm_started == 0u &&
            queued != 0u &&
            !hda_mod_pcm_start_local(format)) {
            return 0;
        }
        if (g_hda_mod.pcm_started != 0u && g_hda_mod.pcm_active == 0u) {
            hda_mod_log("driver: HDAMOD byte ring stopped while waiting queued=%lx free=%lx app=%lx hw=%lx lpib=%u\n",
                        queued,
                        free_bytes,
                        g_hda_mod.pcm_app_pos_bytes,
                        g_hda_mod.pcm_hw_pos_bytes,
                        g_hda_mod.pcm_last_lpib);
            return 0;
        }
        if ((uint32_t)(driver_timer_current_ticks() - start_ticks) > timeout_ticks) {
            hda_mod_log("driver: HDAMOD byte ring wait timeout queued=%lx free=%lx app=%lx hw=%lx lpib=%u\n",
                        queued,
                        free_bytes,
                        g_hda_mod.pcm_app_pos_bytes,
                        g_hda_mod.pcm_hw_pos_bytes,
                        g_hda_mod.pcm_last_lpib);
            return 0;
        }
        hda_mod_wait_for_event_profiled_local(g_hda_profile_pcm_wait);
    }
}

static int hda_mod_pcm_enqueue_local(const uint8_t *src,
                                     uint32_t input_frames,
                                     uint64_t src_step,
                                     uint32_t channels,
                                     uint32_t bits_per_sample,
                                     uint16_t format) {
    uint64_t src_pos = g_hda_mod.pcm_src_remainder;
    uint64_t input_end = (uint64_t)input_frames << 32;
    uint32_t src_stride = channels * (bits_per_sample / 8u);

    if (src == NULL || src_stride == 0u || input_frames == 0u) {
        return 0;
    }
    if (g_hda_mod.pcm_cbl_bytes == 0u) {
        g_hda_mod.pcm_cbl_bytes = hda_mod_pcm_ring_bytes_local();
    }

    hda_mod_log("driver: HDAMOD byte enqueue enter call=%u base=%u frames=%u queued=%lx app=%lx hw=%lx play=%u write=%u started=%u active=%u\n",
                g_hda_mod.pcm_current_call_seq,
                g_hda_mod.pcm_current_call_base_frame,
                input_frames,
                hda_mod_pcm_queued_bytes_local(),
                g_hda_mod.pcm_app_pos_bytes,
                g_hda_mod.pcm_hw_pos_bytes,
                (uint32_t)g_hda_mod.pcm_play_index,
                (uint32_t)g_hda_mod.pcm_write_index,
                (uint32_t)g_hda_mod.pcm_started,
                (uint32_t)g_hda_mod.pcm_active);
    while ((uint32_t)(src_pos >> 32) < input_frames) {
        uint64_t src_before;
        uint64_t src_last_pos;
        uint64_t app_before;
        uint64_t queued;
        uint64_t free_bytes;
        uint32_t safe_margin;
        uint32_t remaining_output_bytes;
        uint32_t refill_bytes;
        uint32_t write_offset;
        uint32_t safe_write_bytes;
        uint32_t max_frames;
        uint32_t abs_first;
        uint32_t abs_last;
        uint32_t written_frames;
        uint32_t written_bytes;
        uint64_t convert_start;

        if (g_hda_mod.pcm_started != 0u) {
            if (!hda_mod_pcm_update_hw_pos_local(1u)) {
                return 0;
            }
        }
        if (hda_mod_pcm_cancelled_local()) {
            return 0;
        }
        queued = hda_mod_pcm_queued_bytes_local();
        if (g_hda_mod.pcm_started == 0u &&
            queued >= hda_mod_pcm_prebuffer_bytes_local() &&
            !hda_mod_pcm_start_local(format)) {
            return 0;
        }
        free_bytes = hda_mod_pcm_free_bytes_local();
        safe_margin = hda_mod_pcm_safe_margin_bytes_local();
        remaining_output_bytes =
            hda_mod_pcm_output_bytes_remaining_local(src_pos,
                                                     input_end,
                                                     src_step);
        refill_bytes = remaining_output_bytes;
        if (refill_bytes > hda_mod_pcm_write_quantum_bytes_local()) {
            refill_bytes = hda_mod_pcm_write_quantum_bytes_local();
        }
        if (refill_bytes < 4u) {
            refill_bytes = 4u;
        }
        if (free_bytes <= safe_margin ||
            free_bytes - safe_margin < refill_bytes) {
            if (!hda_mod_pcm_wait_for_byte_space_local(format, refill_bytes)) {
                driver_log("driver: HDAMOD byte enqueue wait failed call=%u src=%u:%x queued=%lx free=%lx app=%lx hw=%lx started=%u active=%u lpib=%u\n",
                           g_hda_mod.pcm_current_call_seq,
                           (uint32_t)(src_pos >> 32),
                           (uint32_t)src_pos,
                           hda_mod_pcm_queued_bytes_local(),
                           hda_mod_pcm_free_bytes_local(),
                           g_hda_mod.pcm_app_pos_bytes,
                           g_hda_mod.pcm_hw_pos_bytes,
                           (uint32_t)g_hda_mod.pcm_started,
                           (uint32_t)g_hda_mod.pcm_active,
                           g_hda_mod.pcm_last_lpib);
                return 0;
            }
            free_bytes = hda_mod_pcm_free_bytes_local();
        }
        if (free_bytes <= safe_margin) {
            continue;
        }
        max_frames = (uint32_t)((free_bytes - safe_margin) / 4u);
        if (max_frames > hda_mod_pcm_write_quantum_bytes_local() / 4u) {
            max_frames = hda_mod_pcm_write_quantum_bytes_local() / 4u;
        }
        if (max_frames == 0u) {
            continue;
        }
        src_before = src_pos;
        app_before = g_hda_mod.pcm_app_pos_bytes;
        write_offset = (uint32_t)(app_before % g_hda_mod.pcm_cbl_bytes);
        safe_write_bytes =
            hda_mod_pcm_guarded_write_bytes_local(write_offset,
                                                  max_frames * 4u);
        if (safe_write_bytes < 4u) {
            (void)hda_mod_pcm_update_hw_pos_local(1u);
            hda_mod_delay_local(1000u);
            continue;
        }
        max_frames = safe_write_bytes / 4u;
        convert_start = driver_profile_clock();
        written_frames =
            hda_mod_pcm_fill_ring_frames_local(write_offset,
                                               max_frames,
                                               src,
                                               input_frames,
                                               &src_pos,
                                               src_step,
                                               src_stride,
                                               channels,
                                               bits_per_sample);
        driver_profile_record(g_hda_profile_pcm_convert,
                              driver_profile_clock() - convert_start,
                              (uint64_t)written_frames * 4u);
        if (written_frames == 0u) {
            break;
        }
        written_bytes = written_frames * 4u;
        hda_mod_pcm_flush_ring_range_local(write_offset, written_bytes);
        g_hda_mod.pcm_app_pos_bytes += written_bytes;
        hda_mod_pcm_sync_debug_indices_local();
        src_last_pos = src_pos >= src_step ? src_pos - src_step : src_pos;
        abs_first = g_hda_mod.pcm_current_call_base_frame +
            (uint32_t)(src_before >> 32);
        abs_last = g_hda_mod.pcm_current_call_base_frame +
            (uint32_t)(src_last_pos >> 32);
        if (written_frames != max_frames ||
            g_hda_mod.pcm_current_call_seq == 0u) {
            hda_mod_log("driver: HDAMOD byte fill call=%u off=%u bytes=%u src=%u..%u queued=%lx app=%lx hw=%lx play=%u write=%u rem=%u:%x\n",
                        g_hda_mod.pcm_current_call_seq,
                        write_offset,
                        written_bytes,
                        abs_first,
                        abs_last,
                        hda_mod_pcm_queued_bytes_local(),
                        g_hda_mod.pcm_app_pos_bytes,
                        g_hda_mod.pcm_hw_pos_bytes,
                        (uint32_t)g_hda_mod.pcm_play_index,
                        (uint32_t)g_hda_mod.pcm_write_index,
                        (uint32_t)(src_pos >> 32),
                        (uint32_t)src_pos);
        }
        if (g_hda_mod.pcm_started == 0u &&
            hda_mod_pcm_queued_bytes_local() >= hda_mod_pcm_prebuffer_bytes_local() &&
            !hda_mod_pcm_start_local(format)) {
            return 0;
        }
    }
    if (src_pos > input_end) {
        g_hda_mod.pcm_src_remainder = src_pos - input_end;
    } else {
        g_hda_mod.pcm_src_remainder = 0u;
    }
    return 1;
}

static int hda_mod_pcm_drain_local(void) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t start_ticks;
    uint32_t timeout_ticks;
    uint64_t queued_bytes;
    uint64_t drain_ticks;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    if (g_hda_mod.pcm_started == 0u) {
        hda_mod_reset_pcm_state_local();
        return 1;
    }
    start_ticks = driver_timer_current_ticks();
    (void)hda_mod_pcm_update_hw_pos_local(0u);
    queued_bytes = hda_mod_pcm_queued_bytes_local();
    drain_ticks = (queued_bytes * timer_hz + HDA_OUTPUT_BYTES_PER_SECOND - 1u) /
        HDA_OUTPUT_BYTES_PER_SECOND;
    timeout_ticks = (uint32_t)drain_ticks + timer_hz * 5u;
    if (timeout_ticks < timer_hz * 10u) {
        timeout_ticks = timer_hz * 10u;
    }
    while (hda_mod_pcm_queued_bytes_local() != 0u) {
        (void)hda_mod_pcm_update_hw_pos_local(0u);
        if (hda_mod_pcm_cancelled_local()) {
            return 0;
        }
        if (hda_mod_pcm_queued_bytes_local() == 0u) {
            break;
        }
        if (g_hda_mod.pcm_active == 0u) {
            hda_mod_log("driver: HDAMOD byte drain stopped lpib=%u queued=%lx app=%lx hw=%lx\n",
                        g_hda_mod.pcm_last_lpib,
                        hda_mod_pcm_queued_bytes_local(),
                        g_hda_mod.pcm_app_pos_bytes,
                        g_hda_mod.pcm_hw_pos_bytes);
            return 0;
        }
        if ((uint32_t)(driver_timer_current_ticks() - start_ticks) > timeout_ticks) {
            hda_mod_log("driver: HDAMOD byte drain timeout lpib=%u queued=%lx app=%lx hw=%lx\n",
                        g_hda_mod.pcm_last_lpib,
                        hda_mod_pcm_queued_bytes_local(),
                        g_hda_mod.pcm_app_pos_bytes,
                        g_hda_mod.pcm_hw_pos_bytes);
            return 0;
        }
        hda_mod_wait_for_event_profiled_local(g_hda_profile_pcm_drain);
    }
    hda_mod_pcm_log_byte_state_local("drain-end");
    hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
    hda_mod_reset_pcm_state_local();
    return 1;
}

static int hda_mod_play_tone_local(void *ctx, uint32_t hz, uint32_t duration_ms) {
    uint32_t phase_frame = 0u;
    uint32_t index;
    uint32_t cbl_bytes;
    uint32_t total_frames;
    uint32_t target_bytes;
    uint32_t played_bytes = 0u;
    uint32_t previous_lpib;
    uint32_t sd_off;
    uint16_t format = HDA_STREAM_FORMAT_48K_16B_2CH;

    (void)ctx;
    if (g_hda_mod.initialized == 0u) {
        return 0;
    }
    if (hz == 0u || duration_ms == 0u) {
        return 1;
    }
    if (duration_ms > 60000u) {
        duration_ms = 60000u;
    }
    if (!hda_mod_setup_output_path_local() || !hda_mod_prepare_dma_local()) {
        return 0;
    }
    hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
    hda_mod_reset_pcm_state_local();
    for (index = 0; index < HDA_BDL_ENTRIES; index++) {
        hda_mod_fill_tone_buffer_local(index, hz, &phase_frame);
        g_hda_mod.bdl[index].addr = g_hda_mod.buffer_phys[index];
        g_hda_mod.bdl[index].length = HDA_BUFFER_BYTES;
        g_hda_mod.bdl[index].flags = HDA_BDL_FLAG_IOC;
        hda_mod_flush_buffer_local(index);
        hda_mod_flush_descriptor_local(index);
    }
    hda_mod_flush_bdl_local();
    (void)hda_mod_configure_playback_converter_local(format);
    cbl_bytes = HDA_BUFFER_BYTES * HDA_BDL_ENTRIES;
    sd_off = g_hda_mod.output_stream_offset;
    if (!hda_mod_sd_start_local(sd_off,
                                g_hda_mod.play_stream_id,
                                format,
                                cbl_bytes,
                                (uint8_t)(HDA_BDL_ENTRIES - 1u),
                                g_hda_mod.bdl_phys)) {
        hda_mod_sd_halt_local(sd_off);
        return 0;
    }
    total_frames = (duration_ms * HDA_SAMPLE_RATE + 999u) / 1000u;
    target_bytes = total_frames * (HDA_OUT_CHANNELS * 2u);
    if (target_bytes == 0u) {
        target_bytes = HDA_OUT_CHANNELS * 2u;
    }
    previous_lpib = hda_mod_read32_local(sd_off + HDA_SD_LPIB) % cbl_bytes;
    while (played_bytes < target_bytes) {
        uint32_t current_lpib = hda_mod_read32_local(sd_off + HDA_SD_LPIB) % cbl_bytes;

        if (current_lpib != previous_lpib) {
            uint32_t delta;

            if (current_lpib >= previous_lpib) {
                delta = current_lpib - previous_lpib;
            } else {
                delta = cbl_bytes - previous_lpib + current_lpib;
            }
            played_bytes += delta;
            previous_lpib = current_lpib;
        } else if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_RUN) == 0u) {
            hda_mod_log("driver: HDAMOD stream halted sd=%x lpib=%u played=%u target=%u\n",
                        sd_off,
                        current_lpib,
                        played_bytes,
                        target_bytes);
            break;
        } else {
            hda_mod_delay_local(1000u);
        }
    }
    hda_mod_sd_halt_local(sd_off);
    return played_bytes >= target_bytes;
}

static int hda_mod_play_pcm_local(void *ctx,
                                  const void *data,
                                  uint32_t bytes,
                                  uint32_t sample_rate,
                                  uint32_t channels,
                                  uint32_t bits_per_sample,
                                  uint32_t flags) {
    uint32_t src_frame_bytes;
    uint32_t input_frames;
    uint32_t output_sample_rate;
    uint64_t src_step;
    uint16_t format;
    uint32_t async;
    uint32_t new_stream = 0u;
    uint32_t call_seq;
    uint32_t call_base_frame;
    uint32_t src_fp_a;
    uint32_t src_fp_b;
    uint32_t src_fp_c;
    void (*saved_log)(const char *fmt, ...);

    (void)ctx;
    if (g_hda_mod.initialized == 0u || data == NULL || bytes == 0u) {
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
    if (sample_rate < 8000u || sample_rate > 192000u) {
        return 0;
    }
    src_frame_bytes = channels * (bits_per_sample / 8u);
    if (src_frame_bytes == 0u) {
        return 0;
    }
    input_frames = bytes / src_frame_bytes;
    if (input_frames == 0u) {
        return 0;
    }
    saved_log = hda_mod_log;
    if (HDA_PCM_TRACE == 0u) {
        hda_mod_log = hda_mod_silent_log_local;
    }
    if (!hda_mod_setup_output_path_local() || !hda_mod_prepare_dma_local()) {
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }
    format = hda_mod_pcm_format_for_rate_local(sample_rate, &output_sample_rate);
    async = flags & DRIVER_AUDIO_PLAY_F_ASYNC;
    if (g_hda_mod.pcm_started != 0u &&
        !hda_mod_pcm_input_matches_local(sample_rate,
                                         channels,
                                         bits_per_sample,
                                         output_sample_rate)) {
        if (!hda_mod_pcm_drain_local()) {
            hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
            hda_mod_reset_pcm_state_local();
            return hda_mod_restore_log_and_return_local(saved_log, 0);
        }
    }
    if (g_hda_mod.pcm_started == 0u) {
        uint32_t keep_buffered =
            g_hda_mod.pcm_cbl_bytes != 0u &&
            hda_mod_pcm_queued_bytes_local() != 0u &&
            hda_mod_pcm_input_matches_local(sample_rate,
                                            channels,
                                            bits_per_sample,
                                            output_sample_rate);

        if (!keep_buffered) {
            new_stream = 1u;
            if (g_hda_mod.pcm_fill_count != 0u ||
                g_hda_mod.pcm_partial_frames != 0u ||
                g_hda_mod.pcm_write_index != 0u ||
                g_hda_mod.pcm_active != 0u) {
                hda_mod_log("driver: HDAMOD pcm reset before call old active=%u fill=%u play=%u write=%u part=%u rem=%u:%x total=%u\n",
                            (uint32_t)g_hda_mod.pcm_active,
                            (uint32_t)g_hda_mod.pcm_fill_count,
                            (uint32_t)g_hda_mod.pcm_play_index,
                            (uint32_t)g_hda_mod.pcm_write_index,
                            g_hda_mod.pcm_partial_frames,
                            (uint32_t)(g_hda_mod.pcm_src_remainder >> 32),
                            (uint32_t)g_hda_mod.pcm_src_remainder,
                            g_hda_mod.pcm_total_input_frames);
            }
            hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
            hda_mod_reset_pcm_state_local();
            g_hda_mod.pcm_input_sample_rate = sample_rate;
            g_hda_mod.pcm_input_channels = channels;
            g_hda_mod.pcm_input_bits = bits_per_sample;
            g_hda_mod.pcm_output_sample_rate = output_sample_rate;
            hda_mod_zero_all_pcm_buffers_local();
            hda_mod_pcm_prepare_byte_ring_local();
            if (!hda_mod_configure_playback_converter_local(format)) {
                return hda_mod_restore_log_and_return_local(saved_log, 0);
            }
        } else {
            hda_mod_pcm_sync_debug_indices_local();
        }
        hda_mod_log("driver: HDAMOD pcm begin in=%u out=%u fmt=%x ch=%u bits=%u\n",
                    sample_rate,
                    output_sample_rate,
                    (uint32_t)format,
                    channels,
                    bits_per_sample);
    }
    call_seq = ++g_hda_mod.pcm_call_seq;
    call_base_frame = g_hda_mod.pcm_total_input_frames;
    g_hda_mod.pcm_current_call_seq = call_seq;
    g_hda_mod.pcm_current_call_base_frame = call_base_frame;
    g_hda_mod.pcm_total_input_frames += input_frames;
    src_step = ((uint64_t)sample_rate << 32) / (uint64_t)output_sample_rate;
    if (src_step == 0u) {
        src_step = 1u;
    }
    src_fp_a = hda_mod_sample_bytes_local((const uint8_t *)data, bytes, 0u);
    src_fp_b = hda_mod_sample_bytes_local((const uint8_t *)data,
                                          bytes,
                                          bytes / 2u);
    src_fp_c = hda_mod_sample_bytes_local((const uint8_t *)data,
                                          bytes,
                                          bytes - 4u);
    hda_mod_log("driver: HDAMOD pcm call begin id=%u new=%u async=%u bytes=%u frames=%u base=%u in=%u out=%u fmt=%x ch=%u bits=%u srcfp=%x:%x:%x state st=%u act=%u fill=%u play=%u write=%u part=%u rem=%u:%x step=%x:%x\n",
                call_seq,
                new_stream,
                async != 0u ? 1u : 0u,
                bytes,
                input_frames,
                call_base_frame,
                sample_rate,
                output_sample_rate,
                (uint32_t)format,
                channels,
                bits_per_sample,
                src_fp_a,
                src_fp_b,
                src_fp_c,
                (uint32_t)g_hda_mod.pcm_started,
                (uint32_t)g_hda_mod.pcm_active,
                (uint32_t)g_hda_mod.pcm_fill_count,
                (uint32_t)g_hda_mod.pcm_play_index,
                (uint32_t)g_hda_mod.pcm_write_index,
                g_hda_mod.pcm_partial_frames,
                (uint32_t)(g_hda_mod.pcm_src_remainder >> 32),
                (uint32_t)g_hda_mod.pcm_src_remainder,
                (uint32_t)(src_step >> 32),
                (uint32_t)src_step);
    if (!hda_mod_pcm_enqueue_local((const uint8_t *)data,
                                   input_frames,
                                   src_step,
                                   channels,
                                   bits_per_sample,
                                   format)) {
        hda_mod_log("driver: HDAMOD pcm call enqueue failed id=%u fill=%u play=%u write=%u part=%u started=%u active=%u\n",
                    call_seq,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    g_hda_mod.pcm_partial_frames,
                    (uint32_t)g_hda_mod.pcm_started,
                    (uint32_t)g_hda_mod.pcm_active);
        hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
        hda_mod_reset_pcm_state_local();
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }
    if (g_hda_mod.pcm_started == 0u &&
        async != 0u &&
        hda_mod_pcm_queued_bytes_local() < hda_mod_pcm_prebuffer_bytes_local()) {
        return hda_mod_restore_log_and_return_local(saved_log, 1);
    }
    if (g_hda_mod.pcm_started == 0u &&
        hda_mod_pcm_queued_bytes_local() != 0u &&
        !hda_mod_pcm_start_local(format)) {
        hda_mod_log("driver: HDAMOD byte call start failed id=%u queued=%lx play=%u write=%u part=%u\n",
                    call_seq,
                    hda_mod_pcm_queued_bytes_local(),
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    g_hda_mod.pcm_partial_frames);
        hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
        hda_mod_reset_pcm_state_local();
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }
    if (async != 0u) {
        hda_mod_log("driver: HDAMOD pcm call end id=%u async=1 fill=%u play=%u write=%u part=%u rem=%u:%x started=%u active=%u\n",
                    call_seq,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    g_hda_mod.pcm_partial_frames,
                    (uint32_t)(g_hda_mod.pcm_src_remainder >> 32),
                    (uint32_t)g_hda_mod.pcm_src_remainder,
                    (uint32_t)g_hda_mod.pcm_started,
                    (uint32_t)g_hda_mod.pcm_active);
        return hda_mod_restore_log_and_return_local(saved_log, 1);
    }
    if (!hda_mod_pcm_drain_local()) {
        hda_mod_log("driver: HDAMOD pcm call drain failed id=%u fill=%u play=%u write=%u part=%u\n",
                    call_seq,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    g_hda_mod.pcm_partial_frames);
        hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
        hda_mod_reset_pcm_state_local();
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }
    hda_mod_log("driver: HDAMOD pcm call end id=%u async=0 fill=%u play=%u write=%u part=%u rem=%u:%x started=%u active=%u\n",
                call_seq,
                (uint32_t)g_hda_mod.pcm_fill_count,
                (uint32_t)g_hda_mod.pcm_play_index,
                (uint32_t)g_hda_mod.pcm_write_index,
                g_hda_mod.pcm_partial_frames,
                (uint32_t)(g_hda_mod.pcm_src_remainder >> 32),
                (uint32_t)g_hda_mod.pcm_src_remainder,
                (uint32_t)g_hda_mod.pcm_started,
                (uint32_t)g_hda_mod.pcm_active);
    return hda_mod_restore_log_and_return_local(saved_log, 1);
}

static int hda_mod_play_stream_local(void *ctx,
                                     struct driver_audio_pcm_stream *stream) {
    uint32_t src_frame_bytes;
    uint32_t read_capacity;
    uint32_t remaining;
    uint32_t output_sample_rate;
    uint32_t input_frames;
    uint32_t call_seq;
    uint32_t call_base_frame;
    uint64_t src_step;
    uint64_t read_phys;
    uint8_t *read_buffer;
    uint16_t format;
    int ok = 0;
    void (*saved_log)(const char *fmt, ...);
    uint32_t slow_read_ticks;
    uint32_t fail_stage = 0u;
    uint32_t read_seq = 0u;

    (void)ctx;
    if (g_hda_mod.initialized == 0u || stream == NULL || stream->read == NULL) {
        driver_log("driver: HDAMOD stream reject init=%u stream=%lx read=%lx\n",
                   (uint32_t)g_hda_mod.initialized,
                   (uint64_t)(uintptr_t)stream,
                   stream != NULL ? (uint64_t)(uintptr_t)stream->read : 0u);
        return 0;
    }
    if (stream->channels == 0u || stream->channels > 2u) {
        driver_log("driver: HDAMOD stream reject channels=%u\n", stream->channels);
        return 0;
    }
    if (stream->bits_per_sample != 8u && stream->bits_per_sample != 16u) {
        driver_log("driver: HDAMOD stream reject bits=%u\n", stream->bits_per_sample);
        return 0;
    }
    if (stream->sample_rate < 8000u || stream->sample_rate > 192000u) {
        driver_log("driver: HDAMOD stream reject rate=%u\n", stream->sample_rate);
        return 0;
    }
    if ((stream->flags & ~DRIVER_AUDIO_PLAY_F_ASYNC) != 0u) {
        driver_log("driver: HDAMOD stream reject flags=%x\n", stream->flags);
        return 0;
    }
    src_frame_bytes = stream->channels * (stream->bits_per_sample / 8u);
    if (src_frame_bytes == 0u || stream->data_bytes < src_frame_bytes) {
        driver_log("driver: HDAMOD stream reject bytes=%u frame_bytes=%u\n",
                   stream->data_bytes,
                   src_frame_bytes);
        return 0;
    }

    saved_log = hda_mod_log;
    if (HDA_PCM_TRACE == 0u) {
        hda_mod_log = hda_mod_silent_log_local;
    }
    if (!hda_mod_setup_output_path_local() || !hda_mod_prepare_dma_local()) {
        driver_log("driver: HDAMOD stream setup failed ready=%u bdl=%lx buffers=%u\n",
                   (uint32_t)g_hda_mod.play_ready,
                   (uint64_t)(uintptr_t)g_hda_mod.bdl,
                   g_hda_mod.buffer_count);
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }

    read_buffer = (uint8_t *)driver_alloc_pages(HDA_STREAM_READ_PAGES, &read_phys);
    (void)read_phys;
    if (read_buffer == NULL) {
        driver_log("driver: HDAMOD stream read buffer allocation failed pages=%u\n",
                   HDA_STREAM_READ_PAGES);
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }
    read_capacity = HDA_STREAM_READ_PAGES * HDA_PAGE_BYTES;
    if (read_capacity > hda_mod_stream_read_chunk_bytes_local()) {
        read_capacity = hda_mod_stream_read_chunk_bytes_local();
    }
    read_capacity -= read_capacity % src_frame_bytes;
    if (read_capacity == 0u) {
        driver_free_pages(read_buffer, HDA_STREAM_READ_PAGES);
        driver_log("driver: HDAMOD stream read capacity zero frame_bytes=%u\n",
                   src_frame_bytes);
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }

    format = hda_mod_pcm_format_for_rate_local(stream->sample_rate, &output_sample_rate);
    driver_log("driver: HDAMOD stream begin bytes=%u rate=%u out=%u ch=%u bits=%u readcap=%u fmt=%x\n",
               stream->data_bytes,
               stream->sample_rate,
               output_sample_rate,
               stream->channels,
               stream->bits_per_sample,
               read_capacity,
               (uint32_t)format);
    hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
    hda_mod_reset_pcm_state_local();
    g_hda_mod.pcm_input_sample_rate = stream->sample_rate;
    g_hda_mod.pcm_input_channels = stream->channels;
    g_hda_mod.pcm_input_bits = stream->bits_per_sample;
    g_hda_mod.pcm_output_sample_rate = output_sample_rate;
    g_hda_mod.pcm_cancel_ctx = stream->ctx;
    g_hda_mod.pcm_cancelled = stream->cancelled;
    hda_mod_zero_all_pcm_buffers_local();
    hda_mod_pcm_prepare_byte_ring_local();
    if (!hda_mod_configure_playback_converter_local(format)) {
        fail_stage = 1u;
        goto done;
    }

    src_step = ((uint64_t)stream->sample_rate << 32) / (uint64_t)output_sample_rate;
    if (src_step == 0u) {
        src_step = 1u;
    }
    slow_read_ticks = driver_timer_hz() / 4u;
    if (slow_read_ticks == 0u) {
        slow_read_ticks = 1u;
    }
    remaining = stream->data_bytes - (stream->data_bytes % src_frame_bytes);
    while (remaining != 0u) {
        uint32_t want = remaining > read_capacity ? read_capacity : remaining;
        uint32_t got;
        uint32_t play_bytes;
        uint32_t read_start;
        uint32_t read_ticks;
        uint32_t enqueue_start = 0u;
        uint32_t enqueue_ticks = 0u;
        uint64_t read_cycle_start;
        uint64_t enqueue_app_before = g_hda_mod.pcm_app_pos_bytes;
        uint8_t started_before;

        want -= want % src_frame_bytes;
        if (want == 0u) {
            break;
        }
        if (hda_mod_pcm_cancelled_local()) {
            fail_stage = 2u;
            goto done;
        }
        read_start = driver_timer_current_ticks();
        read_cycle_start = driver_profile_clock();
        got = stream->read(stream->ctx, read_buffer, want);
        driver_profile_record(g_hda_profile_stream_read,
                              driver_profile_clock() - read_cycle_start,
                              got);
        read_ticks = (uint32_t)(driver_timer_current_ticks() - read_start);
        if (HDA_STREAM_SLOW_READ_TRACE != 0u &&
            read_ticks >= slow_read_ticks &&
            hda_mod_log_ratelimit_local(&g_hda_mod.pcm_log_slow_read_tick,
                                        &g_hda_mod.pcm_log_slow_read_suppressed)) {
            driver_log("driver: HDAMOD slow read ticks=%u want=%u got=%u remaining=%u lpib=%u delta=%u app=%lx hw=%lx queued=%lx free=%lx started=%u active=%u suppressed=%u\n",
                       read_ticks,
                       want,
                       got,
                       remaining,
                       g_hda_mod.pcm_last_lpib,
                       g_hda_mod.pcm_last_lpib_delta,
                       g_hda_mod.pcm_app_pos_bytes,
                       g_hda_mod.pcm_hw_pos_bytes,
                       hda_mod_pcm_queued_bytes_local(),
                       hda_mod_pcm_free_bytes_local(),
                       (uint32_t)g_hda_mod.pcm_started,
                       (uint32_t)g_hda_mod.pcm_active,
                       hda_mod_log_take_suppressed_local(
                           &g_hda_mod.pcm_log_slow_read_suppressed));
        }
        if (got == 0u) {
            fail_stage = 3u;
            goto done;
        }
        if (hda_mod_pcm_cancelled_local()) {
            fail_stage = 4u;
            goto done;
        }
        if (got > want) {
            got = want;
        }
        play_bytes = got - (got % src_frame_bytes);
        read_seq++;
        started_before = g_hda_mod.pcm_started;
        if (play_bytes != 0u) {
            input_frames = play_bytes / src_frame_bytes;
            call_seq = ++g_hda_mod.pcm_call_seq;
            call_base_frame = g_hda_mod.pcm_total_input_frames;
            g_hda_mod.pcm_current_call_seq = call_seq;
            g_hda_mod.pcm_current_call_base_frame = call_base_frame;
            g_hda_mod.pcm_total_input_frames += input_frames;
            enqueue_start = driver_timer_current_ticks();
            if (!hda_mod_pcm_enqueue_local(read_buffer,
                                           input_frames,
                                           src_step,
                                           stream->channels,
                                           stream->bits_per_sample,
                                           format)) {
                enqueue_ticks =
                    (uint32_t)(driver_timer_current_ticks() - enqueue_start);
                driver_log("driver: HDAMOD stream enqueue failed seq=%u read_ticks=%u enqueue_ticks=%u got=%u wrote=%lx lpib=%u delta=%u app=%lx hw=%lx queued=%lx free=%lx\n",
                           read_seq,
                           read_ticks,
                           enqueue_ticks,
                           got,
                           g_hda_mod.pcm_app_pos_bytes - enqueue_app_before,
                           g_hda_mod.pcm_last_lpib,
                           g_hda_mod.pcm_last_lpib_delta,
                           g_hda_mod.pcm_app_pos_bytes,
                           g_hda_mod.pcm_hw_pos_bytes,
                           hda_mod_pcm_queued_bytes_local(),
                           hda_mod_pcm_free_bytes_local());
                fail_stage = 5u;
                goto done;
            }
            enqueue_ticks =
                (uint32_t)(driver_timer_current_ticks() - enqueue_start);
        }
        if (read_seq <= 4u ||
            started_before != g_hda_mod.pcm_started ||
            (read_seq & 63u) == 0u) {
            driver_log("driver: HDAMOD stream chunk seq=%u got=%u play=%u remain=%u read_ticks=%u enqueue_ticks=%u wrote=%lx lpib=%u delta=%u app=%lx hw=%lx queued=%lx free=%lx started=%u active=%u playi=%u write=%u fill=%u part=%u\n",
                       read_seq,
                       got,
                       play_bytes,
                       remaining,
                       read_ticks,
                       enqueue_ticks,
                       g_hda_mod.pcm_app_pos_bytes - enqueue_app_before,
                       g_hda_mod.pcm_last_lpib,
                       g_hda_mod.pcm_last_lpib_delta,
                       g_hda_mod.pcm_app_pos_bytes,
                       g_hda_mod.pcm_hw_pos_bytes,
                       hda_mod_pcm_queued_bytes_local(),
                       hda_mod_pcm_free_bytes_local(),
                       (uint32_t)g_hda_mod.pcm_started,
                       (uint32_t)g_hda_mod.pcm_active,
                       (uint32_t)g_hda_mod.pcm_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       g_hda_mod.pcm_partial_frames);
        }
        if (got >= remaining) {
            remaining = 0u;
        } else {
            remaining -= got;
        }
        if (got < want && remaining != 0u) {
            fail_stage = 6u;
            goto done;
        }
    }
    if (hda_mod_pcm_cancelled_local()) {
        fail_stage = 8u;
        goto done;
    }
    if (g_hda_mod.pcm_started == 0u &&
        hda_mod_pcm_queued_bytes_local() != 0u &&
        !hda_mod_pcm_start_local(format)) {
        fail_stage = 9u;
        goto done;
    }
    if (!hda_mod_pcm_drain_local()) {
        fail_stage = 10u;
        goto done;
    }
    ok = 1;

done:
    driver_free_pages(read_buffer, HDA_STREAM_READ_PAGES);
    if (ok) {
        driver_log("driver: HDAMOD stream ok chunks=%u lpib=%u delta=%u app=%lx hw=%lx queued=%lx free=%lx started=%u active=%u play=%u write=%u fill=%u part=%u\n",
                   read_seq,
                   g_hda_mod.pcm_last_lpib,
                   g_hda_mod.pcm_last_lpib_delta,
                   g_hda_mod.pcm_app_pos_bytes,
                   g_hda_mod.pcm_hw_pos_bytes,
                   hda_mod_pcm_queued_bytes_local(),
                   hda_mod_pcm_free_bytes_local(),
                   (uint32_t)g_hda_mod.pcm_started,
                   (uint32_t)g_hda_mod.pcm_active,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   g_hda_mod.pcm_partial_frames);
    } else {
        driver_log("driver: HDAMOD stream failed stage=%u lpib=%u delta=%u app=%lx hw=%lx queued=%lx free=%lx started=%u active=%u play=%u write=%u fill=%u part=%u\n",
                   fail_stage,
                   g_hda_mod.pcm_last_lpib,
                   g_hda_mod.pcm_last_lpib_delta,
                   g_hda_mod.pcm_app_pos_bytes,
                   g_hda_mod.pcm_hw_pos_bytes,
                   hda_mod_pcm_queued_bytes_local(),
                   hda_mod_pcm_free_bytes_local(),
                   (uint32_t)g_hda_mod.pcm_started,
                   (uint32_t)g_hda_mod.pcm_active,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   g_hda_mod.pcm_partial_frames);
        hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
        hda_mod_reset_pcm_state_local();
    }
    return hda_mod_restore_log_and_return_local(saved_log, ok);
}

static void hda_mod_fill_name_local(char dst[32], const char *src) {
    uint32_t i = 0;

    while (src != NULL && src[i] != '\0' && i + 1u < 32u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void hda_mod_refresh_registers_local(void) {
    if (g_hda_mod.mmio == NULL) {
        return;
    }
    g_hda_mod.gcap = hda_mod_read16_local(HDA_REG_GCAP);
    g_hda_mod.vmin = hda_mod_read8_local(HDA_REG_VMIN);
    g_hda_mod.vmaj = hda_mod_read8_local(HDA_REG_VMAJ);
    g_hda_mod.outpay = hda_mod_read16_local(HDA_REG_OUTPAY);
    g_hda_mod.inpay = hda_mod_read16_local(HDA_REG_INPAY);
    g_hda_mod.gctl = hda_mod_read32_local(HDA_REG_GCTL);
    g_hda_mod.statests = hda_mod_read16_local(HDA_REG_STATESTS);
    g_hda_mod.wakeen = hda_mod_read16_local(HDA_REG_WAKEEN);
    g_hda_mod.corb_size = hda_mod_read8_local(HDA_REG_CORBSIZE);
    g_hda_mod.rirb_size = hda_mod_read8_local(HDA_REG_RIRBSIZE);
    g_hda_mod.codec_mask = g_hda_mod.statests & 0x7fffu;
}

static int hda_mod_register_audio_local(void) {
    static const struct driver_audio_device_ops audio_ops = {
        hda_mod_play_tone_local,
        hda_mod_play_pcm_local,
        hda_mod_play_stream_local
    };
    struct driver_audio_device_info info;

    if (g_hda_mod.audio_registered == 1u || g_hda_mod.present == 0u) {
        return 1;
    }
    info.present = 1u;
    info.initialized = g_hda_mod.initialized;
    info.caps = DRIVER_AUDIO_CAP_PLAYBACK | DRIVER_AUDIO_CAP_TONE | DRIVER_AUDIO_CAP_STREAM;
    info.driver_kind = DRIVER_AUDIO_KIND_HDA;
    info.sample_rate = HDA_SAMPLE_RATE;
    info.channels = 2u;
    info.bits_per_sample = 16u;
    hda_mod_fill_name_local(info.name, "Intel HD Audio DRV");
    if (!driver_audio_register_device(&info, &audio_ops, &g_hda_mod, NULL)) {
        return 0;
    }
    g_hda_mod.audio_registered = 1u;
    return 1;
}

static void hda_mod_publish_status_local(void) {
    struct driver_hda_device_info info;

    driver_memset(&info, 0, sizeof(info));
    info.present = g_hda_mod.present;
    info.initialized = g_hda_mod.initialized;
    info.irq_line = g_hda_mod.irq_line;
    info.irq_pin = g_hda_mod.irq_pin;
    info.bus = g_hda_mod.bus;
    info.slot = g_hda_mod.slot;
    info.function = g_hda_mod.function;
    info.prog_if = g_hda_mod.prog_if;
    info.vendor_id = g_hda_mod.vendor_id;
    info.device_id = g_hda_mod.device_id;
    info.mmio_base_lo = g_hda_mod.mmio_base_lo;
    info.mmio_base_hi = g_hda_mod.mmio_base_hi;
    info.pci_command = g_hda_mod.pci_command;
    info.gcap = g_hda_mod.gcap;
    info.vmaj = g_hda_mod.vmaj;
    info.vmin = g_hda_mod.vmin;
    info.outpay = g_hda_mod.outpay;
    info.inpay = g_hda_mod.inpay;
    info.gctl = g_hda_mod.gctl;
    info.statests = g_hda_mod.statests;
    info.wakeen = g_hda_mod.wakeen;
    info.corb_size = g_hda_mod.corb_size;
    info.rirb_size = g_hda_mod.rirb_size;
    info.codec_mask = g_hda_mod.codec_mask;
    (void)driver_hda_publish_device(&info);
}

static void hda_mod_probe_codec_vendor_local(void) {
    uint8_t cad;

    g_hda_mod.codec_vendor = 0u;
    for (cad = 0; cad < 15u; cad++) {
        if ((g_hda_mod.codec_mask & (1u << cad)) == 0u) {
            continue;
        }
        (void)hda_mod_get_parameter_local(cad,
                                          0u,
                                          HDA_PARAM_VENDOR_ID,
                                          &g_hda_mod.codec_vendor);
        return;
    }
}

static int hda_mod_init(void) {
    struct driver_pci_device hda;
    uint64_t mmio_base;
    uint16_t command;

    driver_log("driver: HDAMOD init enter\n");
    driver_memset(&g_hda_mod, 0, sizeof(g_hda_mod));
    driver_log("driver: HDAMOD profiles begin\n");
    g_hda_profile_spin = driver_profile_register("hda.spin");
    g_hda_profile_flush = driver_profile_register("hda.cache-flush");
    g_hda_profile_codec_cmd = driver_profile_register("hda.codec-cmd");
    g_hda_profile_pcm_convert = driver_profile_register("hda.pcm-convert");
    g_hda_profile_stream_read = driver_profile_register("hda.stream-read");
    g_hda_profile_pcm_wait = driver_profile_register("hda.pcm-wait");
    g_hda_profile_pcm_drain = driver_profile_register("hda.pcm-drain");
    driver_log("driver: HDAMOD pci scan\n");
    if (!driver_pci_find_by_class(HDA_PCI_CLASS_MULTIMEDIA,
                                  HDA_PCI_SUBCLASS_AUDIO,
                                  0u,
                                  &hda)) {
        driver_log("driver: HDAMOD controller not found\n");
        return 0;
    }
    driver_log("driver: HDAMOD pci found bdf=%u:%u.%u bar0=%x bar1=%x\n",
               (uint32_t)hda.bus,
               (uint32_t)hda.slot,
               (uint32_t)hda.function,
               hda.bar[0],
               hda.bar[1]);

    g_hda_mod.present = 1u;
    g_hda_mod.bus = hda.bus;
    g_hda_mod.slot = hda.slot;
    g_hda_mod.function = hda.function;
    g_hda_mod.prog_if = hda.prog_if;
    g_hda_mod.irq_line = hda.irq_line;
    g_hda_mod.irq_pin = hda.irq_pin;
    g_hda_mod.vendor_id = hda.vendor_id;
    g_hda_mod.device_id = hda.device_id;
    g_hda_mod.mmio_base_lo = hda.bar[0];
    g_hda_mod.mmio_base_hi = hda.bar[1];

    mmio_base = hda_mod_mmio_base_from_bar_local(hda.bar[0], hda.bar[1]);
    if (mmio_base == 0u) {
        (void)hda_mod_register_audio_local();
        hda_mod_publish_status_local();
        hda_mod_log("driver: HDAMOD invalid mmio bar0=%x bar1=%x\n",
                    hda.bar[0],
                    hda.bar[1]);
        return 0;
    }

    command = driver_pci_read16(&hda, HDA_PCI_COMMAND_OFFSET);
    command = (uint16_t)(command | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER);
    driver_pci_write16(&hda, HDA_PCI_COMMAND_OFFSET, command);
    g_hda_mod.pci_command = driver_pci_read16(&hda, HDA_PCI_COMMAND_OFFSET);
    driver_pci_write8(&hda,
                      HDA_PCI_TCSEL_OFFSET,
                      (uint8_t)(driver_pci_read8(&hda, HDA_PCI_TCSEL_OFFSET) &
                                ~HDA_PCI_TCSEL_CLEAR_MASK));

    g_hda_mod.mmio = (volatile uint8_t *)driver_mmio_map(mmio_base);
    if (g_hda_mod.mmio == NULL) {
        (void)hda_mod_register_audio_local();
        hda_mod_publish_status_local();
        hda_mod_log("driver: HDAMOD mmio map failed base=%lx\n", mmio_base);
        return 0;
    }

    driver_log("driver: HDAMOD reset begin mmio=%lx\n", mmio_base);
    hda_mod_write32_local(HDA_REG_INTCTL, 0u);
    g_hda_mod.initialized = hda_mod_controller_reset_local() ? 1u : 0u;
    driver_log("driver: HDAMOD reset done init=%u\n", (uint32_t)g_hda_mod.initialized);
    hda_mod_refresh_registers_local();
    driver_log("driver: HDAMOD codec probe begin statests=%x\n", (uint32_t)g_hda_mod.statests);
    hda_mod_probe_codec_vendor_local();
    driver_log("driver: HDAMOD codec probe done codec=%x\n", g_hda_mod.codec_vendor);
    hda_mod_apply_codec_quirk_local();
    driver_log("driver: HDAMOD register audio\n");
    if (!hda_mod_register_audio_local()) {
        return 0;
    }
    hda_mod_publish_status_local();
    hda_mod_log("driver: HDAMOD build=byte-ring32-contig-refill margin=%u pos_tol=%u prebuf=%u ring=%u write_quantum=%u stream_chunk=%u\n",
                (uint32_t)g_hda_mod.q_safe_margin_desc,
                HDA_PCM_POSITION_TOLERANCE_DESCRIPTORS,
                (uint32_t)g_hda_mod.q_prebuffer_desc,
                HDA_BDL_ENTRIES,
                hda_mod_pcm_write_quantum_bytes_local(),
                hda_mod_stream_read_chunk_bytes_local());
    hda_mod_log("driver: HDAMOD init bdf=%u:%u.%u cmd=%x mmio=%x:%x gcap=%x ver=%u.%u codecs=%x codec=%x init=%u\n",
                (uint32_t)g_hda_mod.bus,
                (uint32_t)g_hda_mod.slot,
                (uint32_t)g_hda_mod.function,
                g_hda_mod.pci_command,
                g_hda_mod.mmio_base_hi,
                g_hda_mod.mmio_base_lo,
                g_hda_mod.gcap,
                g_hda_mod.vmaj,
                g_hda_mod.vmin,
                g_hda_mod.codec_mask,
                g_hda_mod.codec_vendor,
                (uint32_t)g_hda_mod.initialized);
    return g_hda_mod.initialized != 0u;
}

const struct kernel_driver kernel_driver = {
    "HDA",
    KERNEL_DRIVER_KIND_AUDIO,
    hda_mod_init,
    NULL
};
