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
    HDA_BDL_ENTRIES = 32u,
    HDA_BUFFER_BLOCK_COUNT = 8u,
    HDA_BUFFER_BLOCK_ENTRIES = HDA_BDL_ENTRIES / HDA_BUFFER_BLOCK_COUNT,
    HDA_BUFFER_BLOCK_PAGES = HDA_BUFFER_PAGES * HDA_BUFFER_BLOCK_ENTRIES,
    HDA_BUFFER_FRAMES = HDA_BUFFER_BYTES / 4u,
    HDA_DMA_POS_PAGES = 1u,
    HDA_DMA_POS_ENTRY_DWORDS = 2u,
    HDA_PCM_PREBUFFER_DESCRIPTORS = 24u,
    HDA_PCM_HW_GUARD_DESCRIPTORS = 6u,
    HDA_PCM_BCIS_CATCHUP_DESCRIPTORS = HDA_PCM_HW_GUARD_DESCRIPTORS,
    HDA_PCM_RECOVERY_GUARD_DESCRIPTORS =
        HDA_PCM_HW_GUARD_DESCRIPTORS + HDA_PCM_BCIS_CATCHUP_DESCRIPTORS,
    HDA_PCM_MAX_QUEUED_DESCRIPTORS = HDA_BDL_ENTRIES - HDA_PCM_HW_GUARD_DESCRIPTORS - 1u,
    HDA_STREAM_READ_PAGES = 64u,
    HDA_STREAM_READ_CHUNK_BYTES = HDA_BUFFER_BYTES * 8u,
    HDA_PCM_TRACE = 0u,
    HDA_PCM_LOW_TRACE = 0u,
    HDA_PCM_UNDERRUN_TRACE = 0u,
    HDA_PCM_EMPTY_TRACE = 0u,
    HDA_PCM_RING_HAZARD_TRACE = 0u,
    HDA_STREAM_SLOW_READ_TRACE = 0u,
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
    uint8_t pcm_active;
    uint8_t pcm_started;
    uint8_t pcm_write_index;
    uint8_t pcm_play_index;
    uint8_t pcm_fill_count;
    uint32_t pcm_partial_frames;
    uint32_t pcm_cbl_bytes;
    uint32_t pcm_last_lpib;
    uint32_t pcm_last_lpib_tick;
    uint32_t pcm_last_dma_pos;
    uint32_t pcm_position_lpib;
    uint32_t pcm_position_tick;
    uint32_t pcm_position_reject_count;
    uint8_t pcm_position_valid;
    uint32_t pcm_reclaim_tick;
    uint32_t pcm_reclaim_bytes;
    uint32_t pcm_reclaim_budget_bytes;
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

static void (*volatile hda_mod_log)(const char *fmt, ...) = driver_log;

static void hda_mod_silent_log_local(const char *fmt, ...) {
    (void)fmt;
}

static int hda_mod_restore_log_and_return_local(void (*saved_log)(const char *fmt, ...),
                                                int result) {
    hda_mod_log = saved_log;
    return result;
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

    for (i = 0; i < spins; i++) {
        __asm__ __volatile__("" ::: "memory");
    }
}

static void hda_mod_flush_range_local(const void *ptr, uint32_t bytes) {
    uintptr_t addr;
    uintptr_t end;

    if (ptr == NULL || bytes == 0u) {
        return;
    }
    addr = (uintptr_t)ptr & ~(uintptr_t)(HDA_CACHE_LINE_BYTES - 1u);
    end = ((uintptr_t)ptr + bytes + HDA_CACHE_LINE_BYTES - 1u) &
          ~(uintptr_t)(HDA_CACHE_LINE_BYTES - 1u);
    while (addr < end) {
        __asm__ __volatile__("clflush (%0)" :: "r"((void *)addr) : "memory");
        addr += HDA_CACHE_LINE_BYTES;
    }
    __asm__ __volatile__("mfence" ::: "memory");
}

static void hda_mod_invalidate_range_local(const void *ptr, uint32_t bytes) {
    hda_mod_flush_range_local(ptr, bytes);
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

    for (spins = 0; spins < 1000000u; spins++) {
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

    for (spins = 0; spins < 1000000u; spins++) {
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
    hda_mod_delay_local(200000u);
    hda_mod_write32_local(HDA_REG_GCTL, gctl | HDA_GCTL_CRST);
    if (!hda_mod_wait_crst_local(1u)) {
        return 0;
    }
    hda_mod_delay_local(200000u);
    (void)hda_mod_wait_state_sts_local();
    return 1;
}

static int hda_mod_send_cmd20_local(uint8_t cad,
                                    uint8_t nid,
                                    uint32_t cmd20,
                                    uint32_t *out_resp) {
    uint32_t cmd;
    uint32_t spins;

    if (cad >= 15u) {
        return 0;
    }
    cmd = ((uint32_t)cad << 28) | ((uint32_t)nid << 20) | (cmd20 & 0xfffffu);
    for (spins = 0; spins < 1000000u; spins++) {
        if ((hda_mod_read16_local(HDA_REG_ICIS) & HDA_ICIS_ICB) == 0u) {
            break;
        }
    }
    if ((hda_mod_read16_local(HDA_REG_ICIS) & HDA_ICIS_ICB) != 0u) {
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
            return 0;
        }
        if ((status & HDA_ICIS_IRV) == 0u) {
            return 0;
        }
        if (out_resp != NULL) {
            *out_resp = hda_mod_read32_local(HDA_REG_ICII);
        } else {
            (void)hda_mod_read32_local(HDA_REG_ICII);
        }
        hda_mod_write16_local(HDA_REG_ICIS, HDA_ICIS_IRV);
        return 1;
    }
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

static int hda_mod_dma_position_allowed_local(void) {
    uint32_t codec_id;

    if (HDA_DMA_POSITION_BUFFER_ENABLE == 0u) {
        return 0;
    }
    codec_id = g_hda_mod.codec_vendor;
    if (codec_id == HDA_CODEC_REALTEK_ALC887) {
        return 0;
    }
    return 1;
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
        if (g_hda_mod.codec_vendor == HDA_CODEC_REALTEK_ALC887) {
            driver_log("driver: HDAMOD dma position buffer disabled for alc887 codec=%x\n",
                       g_hda_mod.codec_vendor);
        }
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
    g_hda_mod.pcm_last_lpib_tick = 0u;
    g_hda_mod.pcm_last_dma_pos = 0u;
    g_hda_mod.pcm_position_lpib = 0u;
    g_hda_mod.pcm_position_tick = 0u;
    g_hda_mod.pcm_position_reject_count = 0u;
    g_hda_mod.pcm_position_valid = 0u;
    g_hda_mod.pcm_reclaim_tick = 0u;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
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

static void hda_mod_write_stereo_frame_local(uint32_t buffer_index,
                                             uint32_t frame,
                                             int16_t left,
                                             int16_t right) {
    int16_t *out = (int16_t *)g_hda_mod.buffers[buffer_index];

    out[frame * 2u] = left;
    out[frame * 2u + 1u] = right;
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

static void hda_mod_pcm_buffer_fingerprint_local(uint32_t buffer_index,
                                                 uint32_t *out_a,
                                                 uint32_t *out_b,
                                                 uint32_t *out_c) {
    const uint8_t *buffer;

    if (out_a != NULL) {
        *out_a = 0u;
    }
    if (out_b != NULL) {
        *out_b = 0u;
    }
    if (out_c != NULL) {
        *out_c = 0u;
    }
    if (buffer_index >= HDA_BDL_ENTRIES || g_hda_mod.buffers[buffer_index] == NULL) {
        return;
    }
    buffer = g_hda_mod.buffers[buffer_index];
    if (out_a != NULL) {
        *out_a = hda_mod_sample_bytes_local(buffer, HDA_BUFFER_BYTES, 0u);
    }
    if (out_b != NULL) {
        *out_b = hda_mod_sample_bytes_local(buffer,
                                            HDA_BUFFER_BYTES,
                                            HDA_BUFFER_BYTES / 2u);
    }
    if (out_c != NULL) {
        *out_c = hda_mod_sample_bytes_local(buffer,
                                            HDA_BUFFER_BYTES,
                                            HDA_BUFFER_BYTES - 4u);
    }
}

static uint32_t hda_mod_fill_pcm_frames_local(uint32_t buffer_index,
                                              uint32_t start_frame,
                                              const uint8_t *src,
                                              uint32_t input_frames,
                                              uint64_t *src_pos,
                                              uint64_t src_step,
                                              uint32_t channels,
                                              uint32_t bits_per_sample) {
    uint32_t src_stride = channels * (bits_per_sample / 8u);
    uint32_t frame;
    uint32_t frames_written = 0u;

    if (buffer_index >= HDA_BDL_ENTRIES ||
        start_frame >= HDA_BUFFER_FRAMES ||
        src == NULL ||
        src_pos == NULL ||
        src_stride == 0u ||
        input_frames == 0u ||
        g_hda_mod.buffers[buffer_index] == NULL) {
        return 0u;
    }
    for (frame = start_frame; frame < HDA_BUFFER_FRAMES; frame++) {
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
        hda_mod_write_stereo_frame_local(buffer_index, frame, left, right);
        *src_pos += src_step;
        frames_written++;
    }
    if (frames_written != 0u) {
        g_hda_mod.pcm_buffer_silent[buffer_index] = 0u;
    }
    return frames_written;
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

static void hda_mod_zero_pcm_tail_local(uint32_t buffer_index, uint32_t first_frame) {
    uint32_t frame;

    for (frame = first_frame; frame < HDA_BUFFER_FRAMES; frame++) {
        hda_mod_write_stereo_frame_local(buffer_index, frame, 0, 0);
    }
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

static uint8_t hda_mod_pcm_buffer_from_lpib_local(uint32_t lpib) {
    return (uint8_t)((lpib / HDA_BUFFER_BYTES) % HDA_BDL_ENTRIES);
}

static uint32_t hda_mod_pcm_read_lpib_register_local(void) {
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

static int hda_mod_pcm_read_dma_position_local(uint32_t *out_position) {
    uint32_t stream_index;
    uint32_t entry_dword;
    uint32_t position;
    uint32_t verify;

    if (out_position == NULL ||
        g_hda_mod.dma_pos_enabled == 0u ||
        g_hda_mod.dma_pos == NULL ||
        g_hda_mod.pcm_cbl_bytes == 0u) {
        return 0;
    }
    stream_index = hda_mod_output_stream_index_local();
    entry_dword = stream_index * HDA_DMA_POS_ENTRY_DWORDS;
    if (entry_dword + 1u >= (HDA_DMA_POS_PAGES * HDA_PAGE_BYTES) / sizeof(uint32_t)) {
        return 0;
    }
    hda_mod_invalidate_range_local((const void *)&g_hda_mod.dma_pos[entry_dword],
                                   HDA_DMA_POS_ENTRY_DWORDS * sizeof(uint32_t));
    position = g_hda_mod.dma_pos[entry_dword];
    hda_mod_delay_local(100u);
    hda_mod_invalidate_range_local((const void *)&g_hda_mod.dma_pos[entry_dword],
                                   HDA_DMA_POS_ENTRY_DWORDS * sizeof(uint32_t));
    verify = g_hda_mod.dma_pos[entry_dword];
    if (verify != position) {
        position = verify;
    }
    *out_position = position % g_hda_mod.pcm_cbl_bytes;
    return 1;
}

static uint32_t hda_mod_pcm_filter_position_local(uint32_t raw_position) {
    uint32_t now;
    uint32_t timer_hz;
    uint32_t elapsed_ticks;
    uint32_t forward_delta;
    uint32_t plausible_delta;
    uint32_t max_delta;
    uint32_t half_ring;

    if (g_hda_mod.pcm_cbl_bytes == 0u) {
        return 0u;
    }
    raw_position %= g_hda_mod.pcm_cbl_bytes;
    now = driver_timer_current_ticks();
    if (g_hda_mod.pcm_position_valid == 0u) {
        g_hda_mod.pcm_position_lpib = raw_position;
        g_hda_mod.pcm_position_tick = now;
        g_hda_mod.pcm_position_valid = 1u;
        return raw_position;
    }
    if (raw_position == g_hda_mod.pcm_position_lpib) {
        return g_hda_mod.pcm_position_lpib;
    }
    if (raw_position >= g_hda_mod.pcm_position_lpib) {
        forward_delta = raw_position - g_hda_mod.pcm_position_lpib;
    } else {
        forward_delta = g_hda_mod.pcm_cbl_bytes - g_hda_mod.pcm_position_lpib +
            raw_position;
    }
    timer_hz = driver_timer_hz();
    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    half_ring = g_hda_mod.pcm_cbl_bytes / 2u;
    elapsed_ticks = now - g_hda_mod.pcm_position_tick;
    plausible_delta = (uint32_t)(((uint64_t)HDA_OUTPUT_BYTES_PER_SECOND *
                                  (uint64_t)elapsed_ticks) / (uint64_t)timer_hz);
    max_delta = plausible_delta + HDA_BUFFER_BYTES;
    if (max_delta < plausible_delta) {
        max_delta = g_hda_mod.pcm_cbl_bytes;
    }
    plausible_delta = max_delta + HDA_BUFFER_BYTES;
    if (plausible_delta < max_delta || plausible_delta > g_hda_mod.pcm_cbl_bytes) {
        plausible_delta = g_hda_mod.pcm_cbl_bytes;
    }
    if (max_delta < HDA_BUFFER_BYTES) {
        max_delta = HDA_BUFFER_BYTES;
    }
    if (max_delta > half_ring) {
        max_delta = half_ring;
    }
    if (forward_delta > half_ring) {
        if (forward_delta <= plausible_delta) {
            g_hda_mod.pcm_position_reject_count++;
            if (g_hda_mod.pcm_position_reject_count <= 8u ||
                (g_hda_mod.pcm_position_reject_count & 31u) == 0u) {
                driver_log("driver: HDAMOD pcm pos long accept count=%u raw=%u last=%u delta=%u max=%u plausible=%u ticks=%u lpib=%u fill=%u play=%u write=%u\n",
                           g_hda_mod.pcm_position_reject_count,
                           raw_position,
                           g_hda_mod.pcm_position_lpib,
                           forward_delta,
                           max_delta,
                           plausible_delta,
                           elapsed_ticks,
                           g_hda_mod.pcm_last_lpib,
                           (uint32_t)g_hda_mod.pcm_fill_count,
                           (uint32_t)g_hda_mod.pcm_play_index,
                           (uint32_t)g_hda_mod.pcm_write_index);
            }
            g_hda_mod.pcm_position_lpib = raw_position;
            g_hda_mod.pcm_position_tick = now;
            return raw_position;
        }
        g_hda_mod.pcm_position_reject_count++;
        if (g_hda_mod.pcm_position_reject_count <= 8u ||
            (g_hda_mod.pcm_position_reject_count & 31u) == 0u) {
            driver_log("driver: HDAMOD pcm pos back ignore count=%u raw=%u last=%u delta=%u max=%u plausible=%u ticks=%u lpib=%u fill=%u play=%u write=%u\n",
                       g_hda_mod.pcm_position_reject_count,
                       raw_position,
                       g_hda_mod.pcm_position_lpib,
                       forward_delta,
                       max_delta,
                       plausible_delta,
                       elapsed_ticks,
                       g_hda_mod.pcm_last_lpib,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       (uint32_t)g_hda_mod.pcm_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index);
        }
        return g_hda_mod.pcm_position_lpib;
    }
    if (forward_delta > max_delta) {
        g_hda_mod.pcm_position_reject_count++;
        if (g_hda_mod.pcm_position_reject_count <= 8u ||
            (g_hda_mod.pcm_position_reject_count & 31u) == 0u) {
            driver_log("driver: HDAMOD pcm pos wide accept count=%u raw=%u last=%u delta=%u max=%u plausible=%u ticks=%u lpib=%u fill=%u play=%u write=%u\n",
                       g_hda_mod.pcm_position_reject_count,
                       raw_position,
                       g_hda_mod.pcm_position_lpib,
                       forward_delta,
                       max_delta,
                       plausible_delta,
                       elapsed_ticks,
                       g_hda_mod.pcm_last_lpib,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       (uint32_t)g_hda_mod.pcm_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index);
        }
    }
    g_hda_mod.pcm_position_lpib = raw_position;
    g_hda_mod.pcm_position_tick = now;
    return raw_position;
}

static uint32_t hda_mod_pcm_read_lpib_local(void) {
    uint32_t position;
    uint32_t lpib;

    if (g_hda_mod.pcm_cbl_bytes == 0u) {
        return 0u;
    }
    lpib = hda_mod_pcm_read_lpib_register_local();
    if (hda_mod_pcm_read_dma_position_local(&position)) {
        if (position != g_hda_mod.pcm_last_dma_pos) {
            if (position != 0u) {
                g_hda_mod.dma_pos_trusted = 1u;
            }
            g_hda_mod.pcm_last_dma_pos = position;
        }
        if (g_hda_mod.dma_pos_trusted != 0u &&
            (position != g_hda_mod.pcm_last_lpib || lpib == position || lpib == 0u)) {
            if (g_hda_mod.dma_pos_logged == 0u) {
                driver_log("driver: HDAMOD dma position active pos=%u lpib=%u\n",
                           position,
                           lpib);
                g_hda_mod.dma_pos_logged = 1u;
            }
            return hda_mod_pcm_filter_position_local(position);
        }
        return hda_mod_pcm_filter_position_local(lpib);
    }
    return hda_mod_pcm_filter_position_local(lpib);
}

static int hda_mod_pcm_index_is_guarded_local(uint8_t index,
                                              uint8_t hw_buffer,
                                              uint32_t guard_count) {
    uint32_t guard;

    if (guard_count == 0u) {
        guard_count = 1u;
    }
    if (guard_count > HDA_BDL_ENTRIES) {
        guard_count = HDA_BDL_ENTRIES;
    }
    for (guard = 0u; guard < guard_count; guard++) {
        if (index == (uint8_t)((hw_buffer + guard) % HDA_BDL_ENTRIES)) {
            return 1;
        }
    }
    return 0;
}

static int hda_mod_pcm_index_is_hw_guarded_local(uint8_t index, uint8_t hw_buffer) {
    return hda_mod_pcm_index_is_guarded_local(index,
                                              hw_buffer,
                                              HDA_PCM_HW_GUARD_DESCRIPTORS);
}

static int hda_mod_pcm_write_is_hw_guarded_local(uint8_t *out_hw_buffer) {
    uint8_t hw_buffer;

    if (g_hda_mod.pcm_started == 0u || g_hda_mod.pcm_cbl_bytes == 0u) {
        return 0;
    }
    hw_buffer = hda_mod_pcm_buffer_from_lpib_local(hda_mod_pcm_read_lpib_local());
    if (g_hda_mod.pcm_fill_count == 0u && g_hda_mod.pcm_partial_frames == 0u) {
        g_hda_mod.pcm_write_index =
            (uint8_t)((hw_buffer + HDA_PCM_RECOVERY_GUARD_DESCRIPTORS) %
                      HDA_BDL_ENTRIES);
    }
    if (out_hw_buffer != NULL) {
        *out_hw_buffer = hw_buffer;
    }
    return hda_mod_pcm_index_is_hw_guarded_local(g_hda_mod.pcm_write_index, hw_buffer);
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

static void hda_mod_pcm_trace_hw_source_local(uint32_t lpib, uint32_t hw_buffer) {
    uint32_t offset_frames;
    uint32_t hw_src;

    if (hw_buffer >= HDA_BDL_ENTRIES ||
        g_hda_mod.pcm_desc_frames[hw_buffer] == 0u) {
        return;
    }
    offset_frames = (lpib % HDA_BUFFER_BYTES) / 4u;
    if (offset_frames >= g_hda_mod.pcm_desc_frames[hw_buffer]) {
        offset_frames = g_hda_mod.pcm_desc_frames[hw_buffer] - 1u;
    }
    hw_src = g_hda_mod.pcm_desc_src_first[hw_buffer] + offset_frames;
    if (g_hda_mod.pcm_last_hw_src_frame != 0u) {
        if (hw_src + HDA_BUFFER_FRAMES < g_hda_mod.pcm_last_hw_src_frame) {
            g_hda_mod.pcm_hw_jump_count++;
            hda_mod_log("driver: HDAMOD pcm hw-back jump=%u lpib=%u hw=%u src=%u last=%u desc=%u..%u sub=%u call=%u fill=%u play=%u write=%u\n",
                        g_hda_mod.pcm_hw_jump_count,
                        lpib,
                        (uint32_t)hw_buffer,
                        hw_src,
                        g_hda_mod.pcm_last_hw_src_frame,
                        g_hda_mod.pcm_desc_src_first[hw_buffer],
                        g_hda_mod.pcm_desc_src_last[hw_buffer],
                        g_hda_mod.pcm_desc_submit[hw_buffer],
                        g_hda_mod.pcm_desc_call[hw_buffer],
                        (uint32_t)g_hda_mod.pcm_fill_count,
                        (uint32_t)g_hda_mod.pcm_play_index,
                        (uint32_t)g_hda_mod.pcm_write_index);
        } else if (hw_src > g_hda_mod.pcm_last_hw_src_frame + HDA_BUFFER_FRAMES * 4u) {
            g_hda_mod.pcm_hw_jump_count++;
            hda_mod_log("driver: HDAMOD pcm hw-gap jump=%u lpib=%u hw=%u src=%u last=%u desc=%u..%u sub=%u call=%u fill=%u play=%u write=%u\n",
                        g_hda_mod.pcm_hw_jump_count,
                        lpib,
                        (uint32_t)hw_buffer,
                        hw_src,
                        g_hda_mod.pcm_last_hw_src_frame,
                        g_hda_mod.pcm_desc_src_first[hw_buffer],
                        g_hda_mod.pcm_desc_src_last[hw_buffer],
                        g_hda_mod.pcm_desc_submit[hw_buffer],
                        g_hda_mod.pcm_desc_call[hw_buffer],
                        (uint32_t)g_hda_mod.pcm_fill_count,
                        (uint32_t)g_hda_mod.pcm_play_index,
                        (uint32_t)g_hda_mod.pcm_write_index);
        }
    }
    g_hda_mod.pcm_last_hw_src_frame = hw_src;
}

static void hda_mod_pcm_drop_stream_local(void) {
    hda_mod_log("driver: HDAMOD pcm drop st=%u act=%u fill=%u play=%u write=%u part=%u lpib=%u rem=%u curcall=%u total=%u\n",
                (uint32_t)g_hda_mod.pcm_started,
                (uint32_t)g_hda_mod.pcm_active,
                (uint32_t)g_hda_mod.pcm_fill_count,
                (uint32_t)g_hda_mod.pcm_play_index,
                (uint32_t)g_hda_mod.pcm_write_index,
                g_hda_mod.pcm_partial_frames,
                g_hda_mod.pcm_last_lpib,
                g_hda_mod.pcm_reclaim_bytes,
                g_hda_mod.pcm_current_call_seq,
                g_hda_mod.pcm_total_input_frames);
    hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
    g_hda_mod.pcm_active = 0u;
    g_hda_mod.pcm_started = 0u;
    g_hda_mod.pcm_write_index = 0u;
    g_hda_mod.pcm_play_index = 0u;
    g_hda_mod.pcm_fill_count = 0u;
    g_hda_mod.pcm_partial_frames = 0u;
    g_hda_mod.pcm_cbl_bytes = 0u;
    g_hda_mod.pcm_last_lpib = 0u;
    g_hda_mod.pcm_last_lpib_tick = 0u;
    g_hda_mod.pcm_last_dma_pos = 0u;
    g_hda_mod.pcm_position_lpib = 0u;
    g_hda_mod.pcm_position_tick = 0u;
    g_hda_mod.pcm_position_reject_count = 0u;
    g_hda_mod.pcm_position_valid = 0u;
    g_hda_mod.pcm_reclaim_tick = 0u;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    g_hda_mod.pcm_last_debug_tick = 0u;
    g_hda_mod.pcm_current_call_seq = 0u;
    g_hda_mod.pcm_current_call_base_frame = 0u;
    g_hda_mod.pcm_last_submit_src_last = 0u;
    g_hda_mod.pcm_last_reclaim_src_last = 0u;
    g_hda_mod.pcm_last_hw_src_frame = 0u;
    g_hda_mod.dma_pos_trusted = 0u;
    g_hda_mod.dma_pos_logged = 0u;
    hda_mod_zero_all_pcm_buffers_local();
}

static int hda_mod_pcm_debug_due_local(void) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t now = driver_timer_current_ticks();
    uint32_t interval;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    interval = timer_hz / 4u;
    if (interval == 0u) {
        interval = 1u;
    }
    if (g_hda_mod.pcm_last_debug_tick != 0u &&
        (uint32_t)(now - g_hda_mod.pcm_last_debug_tick) < interval) {
        return 0;
    }
    g_hda_mod.pcm_last_debug_tick = now;
    return 1;
}

static int hda_mod_pcm_index_in_window_local(uint8_t index,
                                             uint8_t start,
                                             uint32_t count) {
    uint32_t offset;

    if (count > HDA_BDL_ENTRIES) {
        count = HDA_BDL_ENTRIES;
    }
    for (offset = 0u; offset < count; offset++) {
        if (index == (uint8_t)((start + offset) % HDA_BDL_ENTRIES)) {
            return 1;
        }
    }
    return 0;
}

static int hda_mod_pcm_index_is_queued_or_partial_local(uint8_t index) {
    if (hda_mod_pcm_index_in_window_local(index,
                                          g_hda_mod.pcm_play_index,
                                          g_hda_mod.pcm_fill_count)) {
        return 1;
    }
    return g_hda_mod.pcm_partial_frames != 0u &&
           index == g_hda_mod.pcm_write_index;
}

static void hda_mod_pcm_scrub_unqueued_local(void) {
    uint32_t index;

    for (index = 0u; index < HDA_BDL_ENTRIES; index++) {
        uint8_t slot = (uint8_t)index;

        if (hda_mod_pcm_index_is_queued_or_partial_local(slot)) {
            continue;
        }
        if (g_hda_mod.pcm_desc_frames[slot] == 0u) {
            continue;
        }
        hda_mod_pcm_reset_desc_meta_local(slot);
        hda_mod_set_bdl_descriptor_local(slot, HDA_BUFFER_BYTES, 0u);
        hda_mod_flush_descriptor_local(slot);
    }
}

static void hda_mod_pcm_silence_unqueued_ahead_local(uint8_t hw_buffer) {
    uint32_t guard;
    uint32_t guard_count = HDA_PCM_RECOVERY_GUARD_DESCRIPTORS;

    if (guard_count > HDA_BDL_ENTRIES) {
        guard_count = HDA_BDL_ENTRIES;
    }
    for (guard = 0u; guard < guard_count; guard++) {
        uint8_t slot = (uint8_t)((hw_buffer + guard) % HDA_BDL_ENTRIES);

        if (hda_mod_pcm_index_is_queued_or_partial_local(slot) ||
            g_hda_mod.pcm_buffer_silent[slot] != 0u) {
            continue;
        }
        hda_mod_zero_dma_buffer_local(slot);
        hda_mod_pcm_reset_desc_meta_local(slot);
        hda_mod_set_bdl_descriptor_local(slot, HDA_BUFFER_BYTES, 0u);
        hda_mod_flush_descriptor_local(slot);
    }
}

static void hda_mod_pcm_update_reclaim_budget_local(void) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t now = driver_timer_current_ticks();
    uint32_t elapsed_ticks;
    uint32_t max_budget;
    uint64_t budget;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    if (g_hda_mod.pcm_reclaim_tick == 0u) {
        g_hda_mod.pcm_reclaim_tick = now;
        return;
    }
    elapsed_ticks = now - g_hda_mod.pcm_reclaim_tick;
    if (elapsed_ticks == 0u) {
        return;
    }
    budget = (uint64_t)g_hda_mod.pcm_reclaim_budget_bytes +
        ((uint64_t)HDA_OUTPUT_BYTES_PER_SECOND * (uint64_t)elapsed_ticks) /
            (uint64_t)timer_hz;
    max_budget = g_hda_mod.pcm_cbl_bytes != 0u ?
        g_hda_mod.pcm_cbl_bytes : HDA_BUFFER_BYTES * HDA_BDL_ENTRIES;
    if (budget > max_budget) {
        budget = max_budget;
    }
    g_hda_mod.pcm_reclaim_budget_bytes = (uint32_t)budget;
    g_hda_mod.pcm_reclaim_tick = now;
}

static uint32_t hda_mod_pcm_reclaim_budget_descriptors_local(void) {
    uint32_t descriptors = g_hda_mod.pcm_reclaim_budget_bytes / HDA_BUFFER_BYTES;

    if (descriptors == 0u) {
        descriptors = 1u;
    }
    if (descriptors > HDA_BDL_ENTRIES) {
        descriptors = HDA_BDL_ENTRIES;
    }
    return descriptors;
}

static uint32_t hda_mod_pcm_reclaim_ready_descriptors_local(void) {
    uint32_t descriptors = g_hda_mod.pcm_reclaim_budget_bytes / HDA_BUFFER_BYTES;

    if (descriptors > HDA_BDL_ENTRIES) {
        descriptors = HDA_BDL_ENTRIES;
    }
    return descriptors;
}

static void hda_mod_pcm_consume_reclaim_budget_local(uint32_t descriptors) {
    uint32_t bytes;

    if (descriptors == 0u) {
        return;
    }
    bytes = descriptors * HDA_BUFFER_BYTES;
    if (g_hda_mod.pcm_reclaim_budget_bytes > bytes) {
        g_hda_mod.pcm_reclaim_budget_bytes -= bytes;
    } else {
        g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    }
}

static void hda_mod_pcm_consume_position_bytes_local(uint32_t descriptors) {
    uint32_t bytes;

    if (descriptors == 0u) {
        return;
    }
    bytes = descriptors * HDA_BUFFER_BYTES;
    if (g_hda_mod.pcm_reclaim_bytes > bytes) {
        g_hda_mod.pcm_reclaim_bytes -= bytes;
    } else {
        g_hda_mod.pcm_reclaim_bytes = 0u;
    }
}

static void hda_mod_pcm_stop_empty_local(uint32_t sd_off,
                                         uint32_t current_lpib,
                                         uint8_t hw_buffer) {
    driver_log("driver: HDAMOD pcm empty stop lpib=%u hwbuf=%u play=%u write=%u curcall=%u total=%u rem=%u\n",
               current_lpib,
               (uint32_t)hw_buffer,
               (uint32_t)g_hda_mod.pcm_play_index,
               (uint32_t)g_hda_mod.pcm_write_index,
               g_hda_mod.pcm_current_call_seq,
               g_hda_mod.pcm_total_input_frames,
               g_hda_mod.pcm_reclaim_bytes);
    hda_mod_sd_halt_local(sd_off);
    g_hda_mod.pcm_active = 0u;
    g_hda_mod.pcm_started = 0u;
    g_hda_mod.pcm_write_index = 0u;
    g_hda_mod.pcm_play_index = 0u;
    g_hda_mod.pcm_fill_count = 0u;
    g_hda_mod.pcm_partial_frames = 0u;
    g_hda_mod.pcm_cbl_bytes = 0u;
    g_hda_mod.pcm_last_lpib = 0u;
    g_hda_mod.pcm_last_lpib_tick = 0u;
    g_hda_mod.pcm_last_dma_pos = 0u;
    g_hda_mod.pcm_position_lpib = 0u;
    g_hda_mod.pcm_position_tick = 0u;
    g_hda_mod.pcm_position_reject_count = 0u;
    g_hda_mod.pcm_position_valid = 0u;
    g_hda_mod.pcm_reclaim_tick = 0u;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    g_hda_mod.pcm_last_debug_tick = 0u;
    g_hda_mod.pcm_last_submit_src_last = 0u;
    g_hda_mod.pcm_last_reclaim_src_last = 0u;
    g_hda_mod.pcm_last_hw_src_frame = 0u;
    g_hda_mod.dma_pos_trusted = 0u;
    g_hda_mod.dma_pos_logged = 0u;
    hda_mod_zero_all_pcm_buffers_local();
}

static void __attribute__((unused))
hda_mod_pcm_resync_ring_local(uint32_t current_lpib,
                              uint8_t hw_buffer,
                              uint8_t old_play_index,
                              uint32_t completed,
                              const char *reason) {
    uint32_t guard_count = HDA_PCM_RECOVERY_GUARD_DESCRIPTORS;
    uint32_t i;

    if (guard_count == 0u) {
        guard_count = 1u;
    }
    if (guard_count >= HDA_BDL_ENTRIES) {
        guard_count = HDA_BDL_ENTRIES - 1u;
    }

    for (i = 0u; i < guard_count; i++) {
        uint8_t index = (uint8_t)((hw_buffer + i) % HDA_BDL_ENTRIES);

        hda_mod_zero_dma_buffer_local(index);
        hda_mod_set_bdl_descriptor_local(index, HDA_BUFFER_BYTES, 0u);
        hda_mod_pcm_reset_desc_meta_local(index);
        hda_mod_flush_descriptor_local(index);
    }

    g_hda_mod.pcm_play_index = hw_buffer;
    g_hda_mod.pcm_write_index =
        (uint8_t)((hw_buffer + guard_count) % HDA_BDL_ENTRIES);
    g_hda_mod.pcm_fill_count = (uint8_t)guard_count;
    g_hda_mod.pcm_partial_frames = 0u;
    g_hda_mod.pcm_last_lpib = current_lpib;
    g_hda_mod.pcm_last_lpib_tick = driver_timer_current_ticks();
    g_hda_mod.pcm_last_hw_src_frame = 0u;
    g_hda_mod.pcm_last_reclaim_src_last = 0u;
    g_hda_mod.pcm_reclaim_tick = g_hda_mod.pcm_last_lpib_tick;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    hda_mod_pcm_scrub_unqueued_local();

    if (g_hda_mod.pcm_underrun_count <= 8u ||
        (g_hda_mod.pcm_underrun_count & 15u) == 0u) {
        if (reason == NULL) {
            reason = "ring";
        }
        driver_log("driver: HDAMOD pcm %s resync count=%u lpib=%u hwbuf=%u oldplay=%u newplay=%u write=%u guard=%u done=%u\n",
                   reason,
                   g_hda_mod.pcm_underrun_count,
                   current_lpib,
                   (uint32_t)hw_buffer,
                   (uint32_t)old_play_index,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   guard_count,
                   completed);
    }
}

static void hda_mod_pcm_reclaim_local(uint32_t allow_empty_stop) {
    uint32_t sd_off = g_hda_mod.output_stream_offset;
    uint32_t current_lpib;
    uint32_t delta_bytes;
    uint32_t completed;
    uint32_t data_completed;
    uint32_t fp_a;
    uint32_t fp_b;
    uint32_t fp_c;
    uint32_t bcis;
    uint32_t position_delta;
    uint32_t plausible_delta;
    uint32_t elapsed_lpib_ticks;
    uint32_t hw_distance;
    uint32_t budget_completed;
    uint32_t timer_hz;
    uint32_t now;
    uint64_t reclaim_bytes;
    uint8_t hw_buffer;
    uint8_t old_play_index;
    uint8_t i;

    if (g_hda_mod.pcm_started == 0u || g_hda_mod.pcm_cbl_bytes == 0u) {
        return;
    }

    hda_mod_pcm_update_reclaim_budget_local();
    current_lpib = hda_mod_pcm_read_lpib_local();
    now = driver_timer_current_ticks();
    timer_hz = driver_timer_hz();
    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    if (g_hda_mod.pcm_last_lpib_tick == 0u) {
        g_hda_mod.pcm_last_lpib_tick = now;
    }
    elapsed_lpib_ticks = now - g_hda_mod.pcm_last_lpib_tick;
    hw_buffer = hda_mod_pcm_buffer_from_lpib_local(current_lpib);
    old_play_index = g_hda_mod.pcm_play_index;
    if (hw_buffer >= old_play_index) {
        hw_distance = hw_buffer - old_play_index;
    } else {
        hw_distance = HDA_BDL_ENTRIES - old_play_index + hw_buffer;
    }
    if (current_lpib >= g_hda_mod.pcm_last_lpib) {
        position_delta = current_lpib - g_hda_mod.pcm_last_lpib;
    } else {
        position_delta = g_hda_mod.pcm_cbl_bytes - g_hda_mod.pcm_last_lpib +
            current_lpib;
    }
    if (position_delta > g_hda_mod.pcm_cbl_bytes / 2u) {
        plausible_delta =
            (uint32_t)(((uint64_t)HDA_OUTPUT_BYTES_PER_SECOND *
                        (uint64_t)elapsed_lpib_ticks) / (uint64_t)timer_hz);
        plausible_delta += HDA_BUFFER_BYTES * 2u;
        if (plausible_delta < HDA_BUFFER_BYTES * 2u ||
            plausible_delta > g_hda_mod.pcm_cbl_bytes) {
            plausible_delta = g_hda_mod.pcm_cbl_bytes;
        }
        if (position_delta <= plausible_delta) {
            driver_log("driver: HDAMOD pcm reclaim long accept lpib=%u last=%u delta=%u plausible=%u ticks=%u hwbuf=%u play=%u write=%u fill=%u\n",
                       current_lpib,
                       g_hda_mod.pcm_last_lpib,
                       position_delta,
                       plausible_delta,
                       elapsed_lpib_ticks,
                       (uint32_t)hw_buffer,
                       (uint32_t)old_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       (uint32_t)g_hda_mod.pcm_fill_count);
        } else {
            driver_log("driver: HDAMOD pcm reclaim back ignore lpib=%u last=%u delta=%u plausible=%u ticks=%u hwbuf=%u play=%u write=%u fill=%u\n",
                       current_lpib,
                       g_hda_mod.pcm_last_lpib,
                       position_delta,
                       plausible_delta,
                       elapsed_lpib_ticks,
                       (uint32_t)hw_buffer,
                       (uint32_t)old_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       (uint32_t)g_hda_mod.pcm_fill_count);
            position_delta = 0u;
        }
    }
    reclaim_bytes = (uint64_t)g_hda_mod.pcm_reclaim_bytes + position_delta;
    if (reclaim_bytes > g_hda_mod.pcm_cbl_bytes) {
        reclaim_bytes = g_hda_mod.pcm_cbl_bytes;
    }
    g_hda_mod.pcm_reclaim_bytes = (uint32_t)reclaim_bytes;
    completed = g_hda_mod.pcm_reclaim_bytes / HDA_BUFFER_BYTES;
    if (hw_distance != 0u && completed > hw_distance) {
        completed = hw_distance;
    }
    budget_completed = hda_mod_pcm_reclaim_ready_descriptors_local();
    if (completed > budget_completed) {
        if (HDA_PCM_TRACE != 0u && hda_mod_pcm_debug_due_local()) {
            driver_log("driver: HDAMOD pcm reclaim timecap lpib=%u hwbuf=%u play=%u write=%u fill=%u done=%u budget=%u bytes=%u\n",
                       current_lpib,
                       (uint32_t)hw_buffer,
                       (uint32_t)old_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       completed,
                       budget_completed,
                       g_hda_mod.pcm_reclaim_budget_bytes);
        }
        completed = budget_completed;
    }
    if (HDA_PCM_TRACE != 0u) {
        hda_mod_log("DBG reclaim t=%u lpib=%u hwbuf=%u play=%u write=%u fill=%u partial=%u reclaim_bytes=%u\n",
                    driver_timer_current_ticks(),
                    current_lpib,
                    (uint32_t)hw_buffer,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    g_hda_mod.pcm_partial_frames,
                    g_hda_mod.pcm_reclaim_bytes);
    }
    bcis = hda_mod_read8_local(sd_off + HDA_SD_STS) & HDA_SD_STS_BCIS;
    if (bcis != 0u) {
        hda_mod_write8_local(sd_off + HDA_SD_STS, HDA_SD_STS_BCIS);
        if (completed > HDA_PCM_BCIS_CATCHUP_DESCRIPTORS) {
            budget_completed = hda_mod_pcm_reclaim_budget_descriptors_local();
            if (completed > budget_completed) {
                if (HDA_PCM_TRACE != 0u && hda_mod_pcm_debug_due_local()) {
                    driver_log("driver: HDAMOD pcm bcis catchup timecap lpib=%u hwbuf=%u play=%u write=%u fill=%u distance=%u budget=%u bytes=%u\n",
                               current_lpib,
                               (uint32_t)hw_buffer,
                               (uint32_t)old_play_index,
                               (uint32_t)g_hda_mod.pcm_write_index,
                               (uint32_t)g_hda_mod.pcm_fill_count,
                               completed,
                               budget_completed,
                               g_hda_mod.pcm_reclaim_budget_bytes);
                }
                completed = budget_completed;
            }
        }
        if (completed > HDA_PCM_BCIS_CATCHUP_DESCRIPTORS) {
            if (HDA_PCM_TRACE != 0u && hda_mod_pcm_debug_due_local()) {
                driver_log("driver: HDAMOD pcm bcis catchup wide lpib=%u hwbuf=%u play=%u write=%u fill=%u distance=%u trace=%u\n",
                           current_lpib,
                           (uint32_t)hw_buffer,
                           (uint32_t)old_play_index,
                           (uint32_t)g_hda_mod.pcm_write_index,
                           (uint32_t)g_hda_mod.pcm_fill_count,
                           completed,
                           HDA_PCM_BCIS_CATCHUP_DESCRIPTORS);
            }
            completed = HDA_PCM_BCIS_CATCHUP_DESCRIPTORS;
        } else if (HDA_PCM_TRACE != 0u &&
                   completed > 1u &&
                   hda_mod_pcm_debug_due_local()) {
            driver_log("driver: HDAMOD pcm bcis catchup done=%u lpib=%u hwbuf=%u play=%u write=%u fill=%u\n",
                       completed,
                       current_lpib,
                       (uint32_t)hw_buffer,
                       (uint32_t)old_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       (uint32_t)g_hda_mod.pcm_fill_count);
        }
    }
    delta_bytes = completed * HDA_BUFFER_BYTES;
    g_hda_mod.pcm_last_lpib = current_lpib;
    g_hda_mod.pcm_last_lpib_tick = now;
    hda_mod_pcm_trace_hw_source_local(current_lpib, hw_buffer);
    if (HDA_PCM_LOW_TRACE != 0u &&
        g_hda_mod.pcm_fill_count <= 8u &&
        hda_mod_pcm_debug_due_local()) {
        driver_log("driver: HDAMOD LOW fill=%u lpib=%u play=%u write=%u\n",
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   current_lpib,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index);
    }
    if (HDA_PCM_RING_HAZARD_TRACE != 0u &&
        (g_hda_mod.pcm_fill_count <= 4u ||
         hda_mod_pcm_index_is_hw_guarded_local(g_hda_mod.pcm_write_index, hw_buffer)) &&
        hda_mod_pcm_debug_due_local()) {
        driver_log("driver: HDAMOD ring hazard lpib=%u hwbuf=%u next=%u guard2=%u play=%u write=%u fill=%u partial=%u delta=%u done=%u rem=%u\n",
                   current_lpib,
                   (uint32_t)hw_buffer,
                   (uint32_t)((hw_buffer + 1u) % HDA_BDL_ENTRIES),
                   (uint32_t)((hw_buffer + 2u) % HDA_BDL_ENTRIES),
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   g_hda_mod.pcm_partial_frames,
                   delta_bytes,
                   completed,
                   g_hda_mod.pcm_reclaim_bytes);
    }
    if (completed != 0u &&
        g_hda_mod.pcm_fill_count == 0u &&
        allow_empty_stop == 0u) {
        g_hda_mod.pcm_underrun_count++;
        driver_log("driver: HDAMOD pcm empty resync seen lpib=%u hwbuf=%u play=%u write=%u fill=%u done=%u rem=%u delta=%u distance=%u budget=%u ticks=%u\n",
                   current_lpib,
                   (uint32_t)hw_buffer,
                   (uint32_t)old_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   completed,
                   g_hda_mod.pcm_reclaim_bytes,
                   position_delta,
                   hw_distance,
                   budget_completed,
                   elapsed_lpib_ticks);
        hda_mod_pcm_resync_ring_local(current_lpib,
                                      hw_buffer,
                                      old_play_index,
                                      completed,
                                      "empty");
        return;
    }
    if (completed > g_hda_mod.pcm_fill_count) {
        g_hda_mod.pcm_underrun_count++;
        if (HDA_PCM_UNDERRUN_TRACE != 0u) {
            driver_log("driver: HDAMOD underrun count=%u lpib=%u hwbuf=%u play=%u write=%u fill=%u done=%u rem=%u\n",
                       g_hda_mod.pcm_underrun_count,
                       current_lpib,
                       (uint32_t)hw_buffer,
                       (uint32_t)old_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       completed,
                       g_hda_mod.pcm_reclaim_bytes);
        }
        driver_log("driver: HDAMOD pcm underrun seen lpib=%u hwbuf=%u play=%u write=%u fill=%u done=%u rem=%u delta=%u distance=%u budget=%u ticks=%u\n",
                   current_lpib,
                   (uint32_t)hw_buffer,
                   (uint32_t)old_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   completed,
                   g_hda_mod.pcm_reclaim_bytes,
                   position_delta,
                   hw_distance,
                   budget_completed,
                   elapsed_lpib_ticks);
        if (allow_empty_stop == 0u) {
            hda_mod_pcm_resync_ring_local(current_lpib,
                                          hw_buffer,
                                          old_play_index,
                                          completed,
                                          "underrun");
            return;
        }
    }
    data_completed = completed;
    if (data_completed > g_hda_mod.pcm_fill_count) {
        data_completed = g_hda_mod.pcm_fill_count;
    }
    if (data_completed != 0u) {
        for (i = 0u; i < data_completed; i++) {
            uint8_t consumed =
                (uint8_t)((old_play_index + i) % HDA_BDL_ENTRIES);
            uint32_t reclaim_gap =
                g_hda_mod.pcm_last_reclaim_src_last != 0u &&
                g_hda_mod.pcm_desc_src_first[consumed] !=
                g_hda_mod.pcm_last_reclaim_src_last + 1u;
            uint32_t log_reclaim;

            hda_mod_pcm_buffer_fingerprint_local(consumed, &fp_a, &fp_b, &fp_c);
            g_hda_mod.pcm_reclaim_seq++;
            log_reclaim = reclaim_gap ||
                (completed > 1u && (i == 0u || i + 1u == data_completed)) ||
                (g_hda_mod.pcm_reclaim_seq <= 8u) ||
                ((g_hda_mod.pcm_reclaim_seq & 31u) == 0u);
            if (log_reclaim) {
                hda_mod_log("driver: HDAMOD pcm reclaim seq=%u idx=%u sub=%u call=%u src=%u..%u frames=%u fp=%x:%x:%x lpib=%u hw=%u done=%u gap=%u fill=%u play=%u write=%u\n",
                            g_hda_mod.pcm_reclaim_seq,
                            (uint32_t)consumed,
                            g_hda_mod.pcm_desc_submit[consumed],
                            g_hda_mod.pcm_desc_call[consumed],
                            g_hda_mod.pcm_desc_src_first[consumed],
                            g_hda_mod.pcm_desc_src_last[consumed],
                            g_hda_mod.pcm_desc_frames[consumed],
                            fp_a,
                            fp_b,
                            fp_c,
                            current_lpib,
                            (uint32_t)hw_buffer,
                            completed,
                            reclaim_gap,
                            (uint32_t)g_hda_mod.pcm_fill_count,
                            (uint32_t)old_play_index,
                            (uint32_t)g_hda_mod.pcm_write_index);
            }
            if (g_hda_mod.pcm_desc_frames[consumed] != 0u) {
                g_hda_mod.pcm_last_reclaim_src_last =
                    g_hda_mod.pcm_desc_src_last[consumed];
            }
            if (consumed != hw_buffer) {
                hda_mod_pcm_reset_desc_meta_local(consumed);
            }
        }
        g_hda_mod.pcm_fill_count = (uint8_t)(g_hda_mod.pcm_fill_count - data_completed);
        g_hda_mod.pcm_play_index =
            (uint8_t)((old_play_index + data_completed) % HDA_BDL_ENTRIES);
        hda_mod_pcm_consume_position_bytes_local(data_completed);
        hda_mod_pcm_consume_reclaim_budget_local(data_completed);
        hda_mod_pcm_scrub_unqueued_local();
    }
    hda_mod_pcm_silence_unqueued_ahead_local(hw_buffer);
    if (g_hda_mod.pcm_fill_count == 0u && g_hda_mod.pcm_partial_frames == 0u) {
        if (allow_empty_stop == 0u) {
            if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_RUN) == 0u) {
                g_hda_mod.pcm_active = 0u;
            } else if (HDA_PCM_EMPTY_TRACE != 0u && hda_mod_pcm_debug_due_local()) {
                driver_log("driver: HDAMOD pcm empty defer lpib=%u hwbuf=%u play=%u write=%u curcall=%u total=%u\n",
                           current_lpib,
                           (uint32_t)hw_buffer,
                           (uint32_t)g_hda_mod.pcm_play_index,
                           (uint32_t)g_hda_mod.pcm_write_index,
                           g_hda_mod.pcm_current_call_seq,
                           g_hda_mod.pcm_total_input_frames);
            }
            return;
        }
        if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_RUN) != 0u) {
            g_hda_mod.pcm_empty_count++;
            if (HDA_PCM_EMPTY_TRACE != 0u) {
                driver_log("driver: HDAMOD empty count=%u lpib=%u hwbuf=%u play=%u write=%u curcall=%u total=%u rem=%u\n",
                           g_hda_mod.pcm_empty_count,
                           current_lpib,
                           (uint32_t)hw_buffer,
                           (uint32_t)g_hda_mod.pcm_play_index,
                           (uint32_t)g_hda_mod.pcm_write_index,
                           g_hda_mod.pcm_current_call_seq,
                           g_hda_mod.pcm_total_input_frames,
                           g_hda_mod.pcm_reclaim_bytes);
            }
            hda_mod_pcm_stop_empty_local(sd_off, current_lpib, hw_buffer);
            return;
        }
        hda_mod_log("driver: HDAMOD pcm empty stopped lpib=%u hwbuf=%u play=%u write=%u curcall=%u fill=%u part=%u\n",
                    current_lpib,
                    (uint32_t)hw_buffer,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    g_hda_mod.pcm_current_call_seq,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    g_hda_mod.pcm_partial_frames);
        hda_mod_pcm_drop_stream_local();
        return;
    }
    if ((hda_mod_read8_local(sd_off + HDA_SD_CTL0) & HDA_SD_CTL_RUN) == 0u) {
        g_hda_mod.pcm_active = 0u;
    }
}

static int hda_mod_pcm_start_local(uint16_t format) {
    uint32_t sd_off = g_hda_mod.output_stream_offset;
    uint32_t lpib_samples;

    if (g_hda_mod.pcm_started != 0u) {
        return 1;
    }
    if (g_hda_mod.pcm_fill_count == 0u) {
        return 1;
    }

    hda_mod_flush_bdl_local();

    g_hda_mod.pcm_cbl_bytes = HDA_BUFFER_BYTES * HDA_BDL_ENTRIES;
    if (!hda_mod_sd_start_local(sd_off,
                                g_hda_mod.play_stream_id,
                                format,
                                g_hda_mod.pcm_cbl_bytes,
                                (uint8_t)(HDA_BDL_ENTRIES - 1u),
                                g_hda_mod.bdl_phys)) {
        return 0;
    }

    /* Additional delay after DMA start for hardware stability */
    hda_mod_delay_local(10000u);

    /* Read LPIB multiple times to ensure valid value on real hardware */
    lpib_samples = hda_mod_read32_local(sd_off + HDA_SD_LPIB);
    lpib_samples = hda_mod_read32_local(sd_off + HDA_SD_LPIB);
    g_hda_mod.pcm_last_lpib = lpib_samples % g_hda_mod.pcm_cbl_bytes;
    g_hda_mod.pcm_position_lpib = g_hda_mod.pcm_last_lpib;
    g_hda_mod.pcm_position_tick = driver_timer_current_ticks();
    g_hda_mod.pcm_last_lpib_tick = g_hda_mod.pcm_position_tick;
    g_hda_mod.pcm_position_reject_count = 0u;
    g_hda_mod.pcm_position_valid = 1u;
    g_hda_mod.pcm_reclaim_tick = g_hda_mod.pcm_position_tick;
    g_hda_mod.pcm_reclaim_bytes = 0u;
    g_hda_mod.pcm_reclaim_budget_bytes = 0u;
    g_hda_mod.pcm_play_index = 0u;
    g_hda_mod.pcm_active = 1u;
    g_hda_mod.pcm_started = 1u;
    driver_log("driver: HDAMOD pcm start ok sd=%x ctl=%x cbl=%u fill=%u write=%u lpib=%u\n",
               sd_off,
               (uint32_t)hda_mod_read8_local(sd_off + HDA_SD_CTL0),
               g_hda_mod.pcm_cbl_bytes,
               (uint32_t)g_hda_mod.pcm_fill_count,
               (uint32_t)g_hda_mod.pcm_write_index,
               g_hda_mod.pcm_last_lpib);
    return 1;
}

static int hda_mod_pcm_wait_for_space_local(uint16_t format) {
    uint32_t timer_hz = driver_timer_hz();
    uint32_t start_ticks;
    uint32_t timeout_ticks;
    uint8_t hw_buffer = 0u;
    int write_guarded;

    if (timer_hz == 0u) {
        timer_hz = 100u;
    }
    start_ticks = driver_timer_current_ticks();
    timeout_ticks = timer_hz * 5u;
    if (timeout_ticks < 10u) {
        timeout_ticks = 10u;
    }
    while (1) {
        write_guarded = hda_mod_pcm_write_is_hw_guarded_local(&hw_buffer);
        if (g_hda_mod.pcm_fill_count < HDA_PCM_MAX_QUEUED_DESCRIPTORS &&
            write_guarded == 0) {
            return 1;
        }
        if (hda_mod_pcm_cancelled_local()) {
            return 0;
        }
        if (g_hda_mod.pcm_started == 0u && !hda_mod_pcm_start_local(format)) {
            return 0;
        }
        hda_mod_pcm_reclaim_local(0u);
        write_guarded = hda_mod_pcm_write_is_hw_guarded_local(&hw_buffer);
        if (g_hda_mod.pcm_fill_count < HDA_PCM_MAX_QUEUED_DESCRIPTORS &&
            write_guarded == 0) {
            return 1;
        }
        if (HDA_PCM_RING_HAZARD_TRACE != 0u &&
            write_guarded != 0 &&
            hda_mod_pcm_debug_due_local()) {
            driver_log("driver: HDAMOD ring wait hwbuf=%u guard=%u guard1=%u guard2=%u play=%u write=%u fill=%u part=%u\n",
                       (uint32_t)hw_buffer,
                       HDA_PCM_HW_GUARD_DESCRIPTORS,
                       (uint32_t)((hw_buffer + 1u) % HDA_BDL_ENTRIES),
                       (uint32_t)((hw_buffer + 2u) % HDA_BDL_ENTRIES),
                       (uint32_t)g_hda_mod.pcm_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       g_hda_mod.pcm_partial_frames);
        }
        if (g_hda_mod.pcm_started != 0u && g_hda_mod.pcm_active == 0u) {
            hda_mod_log("driver: HDAMOD pcm stopped while waiting lpib=%u fill=%u\n",
                        g_hda_mod.pcm_last_lpib,
                        (uint32_t)g_hda_mod.pcm_fill_count);
            return 0;
        }
        if ((uint32_t)(driver_timer_current_ticks() - start_ticks) > timeout_ticks) {
            hda_mod_log("driver: HDAMOD pcm wait timeout lpib=%u fill=%u\n",
                        g_hda_mod.pcm_last_lpib,
                        (uint32_t)g_hda_mod.pcm_fill_count);
            return 0;
        }
        hda_mod_delay_local(1000u);
    }
}

static int hda_mod_pcm_submit_current_local(uint16_t format) {
    uint8_t submitted_index;
    uint32_t fp_a;
    uint32_t fp_b;
    uint32_t fp_c;
    uint32_t lvi = 0xffffffffu;
    uint32_t lpib = 0xffffffffu;
    uint32_t submit_gap;
    uint32_t log_submit;

    if (g_hda_mod.pcm_partial_frames == 0u) {
        return 1;
    }
    if (g_hda_mod.pcm_write_index >= HDA_BDL_ENTRIES) {
        driver_log("driver: HDAMOD pcm bad write index=%u\n",
                   (uint32_t)g_hda_mod.pcm_write_index);
        return 0;
    }
    if (!hda_mod_pcm_wait_for_space_local(format)) {
        driver_log("driver: HDAMOD pcm submit wait failed idx=%u fill=%u play=%u write=%u part=%u started=%u active=%u lpib=%u\n",
                   (uint32_t)g_hda_mod.pcm_write_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   g_hda_mod.pcm_partial_frames,
                   (uint32_t)g_hda_mod.pcm_started,
                   (uint32_t)g_hda_mod.pcm_active,
                   g_hda_mod.pcm_last_lpib);
        return 0;
    }
    if (g_hda_mod.pcm_partial_frames == 0u) {
        return 1;
    }
    if (g_hda_mod.pcm_write_index >= HDA_BDL_ENTRIES) {
        driver_log("driver: HDAMOD pcm bad write index after wait=%u\n",
                   (uint32_t)g_hda_mod.pcm_write_index);
        return 0;
    }
    if (g_hda_mod.pcm_partial_frames < HDA_BUFFER_FRAMES) {
        hda_mod_zero_pcm_tail_local(g_hda_mod.pcm_write_index,
                                    g_hda_mod.pcm_partial_frames);
    }

    hda_mod_set_bdl_descriptor_local(g_hda_mod.pcm_write_index,
                                     HDA_BUFFER_BYTES,
                                     0u);

    submitted_index = g_hda_mod.pcm_write_index;
    if (HDA_PCM_TRACE != 0u) {
        hda_mod_pcm_buffer_fingerprint_local(submitted_index, &fp_a, &fp_b, &fp_c);
    } else {
        fp_a = 0u;
        fp_b = 0u;
        fp_c = 0u;
    }
    g_hda_mod.pcm_submit_seq++;
    g_hda_mod.pcm_desc_submit[submitted_index] = g_hda_mod.pcm_submit_seq;

    hda_mod_flush_buffer_local(submitted_index);
    hda_mod_flush_descriptor_local(submitted_index);

    g_hda_mod.pcm_partial_frames = 0u;
    g_hda_mod.pcm_write_index =
        (uint8_t)((g_hda_mod.pcm_write_index + 1u) % HDA_BDL_ENTRIES);
    if (g_hda_mod.pcm_fill_count < HDA_PCM_MAX_QUEUED_DESCRIPTORS) {
        g_hda_mod.pcm_fill_count++;
    }
    if (g_hda_mod.pcm_started == 0u &&
        g_hda_mod.pcm_fill_count >= HDA_PCM_PREBUFFER_DESCRIPTORS &&
        !hda_mod_pcm_start_local(format)) {
        driver_log("driver: HDAMOD pcm submit start failed seq=%u idx=%u fill=%u play=%u write=%u\n",
                   g_hda_mod.pcm_submit_seq,
                   (uint32_t)submitted_index,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index);
        return 0;
    }
    if (g_hda_mod.pcm_started != 0u) {
        lvi = hda_mod_read16_local(g_hda_mod.output_stream_offset + HDA_SD_LVI);
        if (g_hda_mod.pcm_cbl_bytes != 0u) {
            lpib = hda_mod_read32_local(g_hda_mod.output_stream_offset + HDA_SD_LPIB) %
                g_hda_mod.pcm_cbl_bytes;
        }
    }
    if (HDA_PCM_TRACE != 0u) {
        hda_mod_log("DBG submit t=%u seq=%u idx=%u lvi=%u lpib=%u fill=%u play=%u write=%u\n",
                    driver_timer_current_ticks(),
                    g_hda_mod.pcm_submit_seq,
                    (uint32_t)submitted_index,
                    lvi,
                    lpib,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index);
    }
    submit_gap = g_hda_mod.pcm_last_submit_src_last != 0u &&
        g_hda_mod.pcm_desc_src_first[submitted_index] !=
        g_hda_mod.pcm_last_submit_src_last + 1u;
    log_submit = submit_gap ||
        g_hda_mod.pcm_submit_seq <= 8u ||
        ((g_hda_mod.pcm_submit_seq & 31u) == 0u) ||
        submitted_index == 0u ||
        g_hda_mod.pcm_desc_frames[submitted_index] != HDA_BUFFER_FRAMES;
    if (log_submit) {
        hda_mod_log("driver: HDAMOD pcm submit seq=%u idx=%u call=%u src=%u..%u frames=%u fp=%x:%x:%x gap=%u fill=%u play=%u next=%u lvi=%u lpib=%u\n",
                    g_hda_mod.pcm_submit_seq,
                    (uint32_t)submitted_index,
                    g_hda_mod.pcm_desc_call[submitted_index],
                    g_hda_mod.pcm_desc_src_first[submitted_index],
                    g_hda_mod.pcm_desc_src_last[submitted_index],
                    g_hda_mod.pcm_desc_frames[submitted_index],
                    fp_a,
                    fp_b,
                    fp_c,
                    submit_gap,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    lvi,
                    lpib);
    }
    if (g_hda_mod.pcm_desc_frames[submitted_index] != 0u) {
        g_hda_mod.pcm_last_submit_src_last =
            g_hda_mod.pcm_desc_src_last[submitted_index];
    }
    return 1;
}

static int hda_mod_pcm_enqueue_local(const uint8_t *src,
                                     uint32_t input_frames,
                                     uint64_t src_step,
                                     uint32_t channels,
                                     uint32_t bits_per_sample,
                                     uint16_t format) {
    uint64_t src_pos = g_hda_mod.pcm_src_remainder;
    uint64_t input_end = (uint64_t)input_frames << 32;

    hda_mod_log("driver: HDAMOD pcm enqueue enter call=%u base=%u frames=%u fill=%u play=%u write=%u part=%u started=%u active=%u\n",
                g_hda_mod.pcm_current_call_seq,
                g_hda_mod.pcm_current_call_base_frame,
                input_frames,
                (uint32_t)g_hda_mod.pcm_fill_count,
                (uint32_t)g_hda_mod.pcm_play_index,
                (uint32_t)g_hda_mod.pcm_write_index,
                g_hda_mod.pcm_partial_frames,
                (uint32_t)g_hda_mod.pcm_started,
                (uint32_t)g_hda_mod.pcm_active);
    while ((uint32_t)(src_pos >> 32) < input_frames) {
        uint64_t src_before;
        uint64_t src_last_pos;
        uint32_t abs_first;
        uint32_t abs_last;
        uint32_t partial_before;
        uint32_t written_frames;
        uint8_t target_index;

        hda_mod_pcm_reclaim_local(0u);
        if (hda_mod_pcm_cancelled_local()) {
            return 0;
        }
        if (g_hda_mod.pcm_current_call_seq == 0u) {
            hda_mod_log("driver: HDAMOD pcm enqueue call lost after reclaim src=%u:%x fill=%u play=%u write=%u part=%u started=%u active=%u\n",
                        (uint32_t)(src_pos >> 32),
                        (uint32_t)src_pos,
                        (uint32_t)g_hda_mod.pcm_fill_count,
                        (uint32_t)g_hda_mod.pcm_play_index,
                        (uint32_t)g_hda_mod.pcm_write_index,
                        g_hda_mod.pcm_partial_frames,
                        (uint32_t)g_hda_mod.pcm_started,
                        (uint32_t)g_hda_mod.pcm_active);
        }
        if (g_hda_mod.pcm_partial_frames == 0u) {
            if (!hda_mod_pcm_wait_for_space_local(format)) {
                driver_log("driver: HDAMOD pcm enqueue wait failed call=%u src=%u:%x fill=%u play=%u write=%u part=%u started=%u active=%u lpib=%u\n",
                           g_hda_mod.pcm_current_call_seq,
                           (uint32_t)(src_pos >> 32),
                           (uint32_t)src_pos,
                           (uint32_t)g_hda_mod.pcm_fill_count,
                           (uint32_t)g_hda_mod.pcm_play_index,
                           (uint32_t)g_hda_mod.pcm_write_index,
                           g_hda_mod.pcm_partial_frames,
                           (uint32_t)g_hda_mod.pcm_started,
                           (uint32_t)g_hda_mod.pcm_active,
                           g_hda_mod.pcm_last_lpib);
                return 0;
            }
            if (g_hda_mod.pcm_current_call_seq == 0u) {
                hda_mod_log("driver: HDAMOD pcm enqueue call lost after wait src=%u:%x fill=%u play=%u write=%u part=%u started=%u active=%u\n",
                            (uint32_t)(src_pos >> 32),
                            (uint32_t)src_pos,
                            (uint32_t)g_hda_mod.pcm_fill_count,
                            (uint32_t)g_hda_mod.pcm_play_index,
                            (uint32_t)g_hda_mod.pcm_write_index,
                            g_hda_mod.pcm_partial_frames,
                            (uint32_t)g_hda_mod.pcm_started,
                            (uint32_t)g_hda_mod.pcm_active);
            }
            hda_mod_zero_buffer_local(g_hda_mod.pcm_write_index);
            hda_mod_pcm_reset_desc_meta_local(g_hda_mod.pcm_write_index);
        }
        target_index = g_hda_mod.pcm_write_index;
        partial_before = g_hda_mod.pcm_partial_frames;
        src_before = src_pos;
        written_frames =
            hda_mod_fill_pcm_frames_local(target_index,
                                          partial_before,
                                          src,
                                          input_frames,
                                          &src_pos,
                                          src_step,
                                          channels,
                                          bits_per_sample);
        if (written_frames == 0u) {
            break;
        }
        src_last_pos = src_pos >= src_step ? src_pos - src_step : src_pos;
        abs_first = g_hda_mod.pcm_current_call_base_frame +
            (uint32_t)(src_before >> 32);
        abs_last = g_hda_mod.pcm_current_call_base_frame +
            (uint32_t)(src_last_pos >> 32);
        if (g_hda_mod.pcm_desc_frames[target_index] == 0u) {
            g_hda_mod.pcm_desc_src_first[target_index] = abs_first;
            g_hda_mod.pcm_desc_call[target_index] =
                g_hda_mod.pcm_current_call_seq;
        }
        g_hda_mod.pcm_desc_src_last[target_index] = abs_last;
        g_hda_mod.pcm_desc_frames[target_index] += written_frames;
        if (partial_before != 0u ||
            written_frames != HDA_BUFFER_FRAMES ||
            g_hda_mod.pcm_current_call_seq == 0u) {
            hda_mod_log("driver: HDAMOD pcm fill call=%u idx=%u part=%u+%u src=%u..%u fill=%u play=%u write=%u rem=%u:%x\n",
                        g_hda_mod.pcm_current_call_seq,
                        (uint32_t)target_index,
                        partial_before,
                        written_frames,
                        abs_first,
                        abs_last,
                        (uint32_t)g_hda_mod.pcm_fill_count,
                        (uint32_t)g_hda_mod.pcm_play_index,
                        (uint32_t)g_hda_mod.pcm_write_index,
                        (uint32_t)(src_pos >> 32),
                        (uint32_t)src_pos);
        }
        g_hda_mod.pcm_partial_frames += written_frames;
        if (g_hda_mod.pcm_partial_frames == HDA_BUFFER_FRAMES &&
            !hda_mod_pcm_submit_current_local(format)) {
            driver_log("driver: HDAMOD pcm enqueue submit failed call=%u idx=%u src=%u:%x fill=%u play=%u write=%u part=%u started=%u active=%u lpib=%u\n",
                       g_hda_mod.pcm_current_call_seq,
                       (uint32_t)target_index,
                       (uint32_t)(src_pos >> 32),
                       (uint32_t)src_pos,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       (uint32_t)g_hda_mod.pcm_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       g_hda_mod.pcm_partial_frames,
                       (uint32_t)g_hda_mod.pcm_started,
                       (uint32_t)g_hda_mod.pcm_active,
                       g_hda_mod.pcm_last_lpib);
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
    queued_bytes = (uint64_t)g_hda_mod.pcm_fill_count * HDA_BUFFER_BYTES;
    if (queued_bytes > g_hda_mod.pcm_reclaim_bytes) {
        queued_bytes -= g_hda_mod.pcm_reclaim_bytes;
    } else {
        queued_bytes = 0u;
    }
    drain_ticks = (queued_bytes * timer_hz + HDA_OUTPUT_BYTES_PER_SECOND - 1u) /
        HDA_OUTPUT_BYTES_PER_SECOND;
    timeout_ticks = (uint32_t)drain_ticks + timer_hz * 5u;
    if (timeout_ticks < timer_hz * 10u) {
        timeout_ticks = timer_hz * 10u;
    }
    while (g_hda_mod.pcm_fill_count != 0u) {
        hda_mod_pcm_reclaim_local(1u);
        if (hda_mod_pcm_cancelled_local()) {
            return 0;
        }
        if (g_hda_mod.pcm_fill_count == 0u) {
            break;
        }
        if (g_hda_mod.pcm_active == 0u) {
            hda_mod_log("driver: HDAMOD pcm drain stopped lpib=%u fill=%u\n",
                        g_hda_mod.pcm_last_lpib,
                        (uint32_t)g_hda_mod.pcm_fill_count);
            return 0;
        }
        if ((uint32_t)(driver_timer_current_ticks() - start_ticks) > timeout_ticks) {
            hda_mod_log("driver: HDAMOD pcm drain timeout lpib=%u fill=%u\n",
                        g_hda_mod.pcm_last_lpib,
                        (uint32_t)g_hda_mod.pcm_fill_count);
            return 0;
        }
        hda_mod_delay_local(1000u);
    }
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
        if (!hda_mod_configure_playback_converter_local(format)) {
            return hda_mod_restore_log_and_return_local(saved_log, 0);
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
    if (async == 0u && !hda_mod_pcm_submit_current_local(format)) {
        hda_mod_log("driver: HDAMOD pcm call final submit failed id=%u fill=%u play=%u write=%u part=%u\n",
                    call_seq,
                    (uint32_t)g_hda_mod.pcm_fill_count,
                    (uint32_t)g_hda_mod.pcm_play_index,
                    (uint32_t)g_hda_mod.pcm_write_index,
                    g_hda_mod.pcm_partial_frames);
        hda_mod_sd_halt_local(g_hda_mod.output_stream_offset);
        hda_mod_reset_pcm_state_local();
        return hda_mod_restore_log_and_return_local(saved_log, 0);
    }
    if (g_hda_mod.pcm_started == 0u &&
        async != 0u &&
        g_hda_mod.pcm_fill_count < HDA_PCM_PREBUFFER_DESCRIPTORS) {
        return hda_mod_restore_log_and_return_local(saved_log, 1);
    }
    if (g_hda_mod.pcm_started == 0u && !hda_mod_pcm_start_local(format)) {
        hda_mod_log("driver: HDAMOD pcm call start failed id=%u fill=%u play=%u write=%u part=%u\n",
                    call_seq,
                    (uint32_t)g_hda_mod.pcm_fill_count,
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
    if (read_capacity > HDA_STREAM_READ_CHUNK_BYTES) {
        read_capacity = HDA_STREAM_READ_CHUNK_BYTES;
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
        got = stream->read(stream->ctx, read_buffer, want);
        read_ticks = (uint32_t)(driver_timer_current_ticks() - read_start);
        if (HDA_STREAM_SLOW_READ_TRACE != 0u && read_ticks >= slow_read_ticks) {
            driver_log("driver: HDAMOD slow read ticks=%u want=%u got=%u remaining=%u fill=%u play=%u write=%u\n",
                       read_ticks,
                       want,
                       got,
                       remaining,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       (uint32_t)g_hda_mod.pcm_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index);
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
            if (!hda_mod_pcm_enqueue_local(read_buffer,
                                           input_frames,
                                           src_step,
                                           stream->channels,
                                           stream->bits_per_sample,
                                           format)) {
                fail_stage = 5u;
                goto done;
            }
        }
        if (read_seq <= 4u ||
            started_before != g_hda_mod.pcm_started ||
            (read_seq & 63u) == 0u) {
            uint32_t lpib = 0u;

            if (g_hda_mod.pcm_cbl_bytes != 0u) {
                lpib = hda_mod_pcm_read_lpib_local();
            }
            driver_log("driver: HDAMOD stream chunk seq=%u got=%u play=%u remain=%u fill=%u playi=%u write=%u part=%u started=%u active=%u lpib=%u\n",
                       read_seq,
                       got,
                       play_bytes,
                       remaining,
                       (uint32_t)g_hda_mod.pcm_fill_count,
                       (uint32_t)g_hda_mod.pcm_play_index,
                       (uint32_t)g_hda_mod.pcm_write_index,
                       g_hda_mod.pcm_partial_frames,
                       (uint32_t)g_hda_mod.pcm_started,
                       (uint32_t)g_hda_mod.pcm_active,
                       lpib);
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
    if (!hda_mod_pcm_submit_current_local(format)) {
        fail_stage = 7u;
        goto done;
    }
    if (hda_mod_pcm_cancelled_local()) {
        fail_stage = 8u;
        goto done;
    }
    if (g_hda_mod.pcm_started == 0u && !hda_mod_pcm_start_local(format)) {
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
        driver_log("driver: HDAMOD stream ok chunks=%u fill=%u play=%u write=%u part=%u started=%u active=%u lpib=%u rem=%u\n",
                   read_seq,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   g_hda_mod.pcm_partial_frames,
                   (uint32_t)g_hda_mod.pcm_started,
                   (uint32_t)g_hda_mod.pcm_active,
                   g_hda_mod.pcm_last_lpib,
                   g_hda_mod.pcm_reclaim_bytes);
    } else {
        driver_log("driver: HDAMOD stream failed stage=%u fill=%u play=%u write=%u part=%u started=%u active=%u lpib=%u rem=%u\n",
                   fail_stage,
                   (uint32_t)g_hda_mod.pcm_fill_count,
                   (uint32_t)g_hda_mod.pcm_play_index,
                   (uint32_t)g_hda_mod.pcm_write_index,
                   g_hda_mod.pcm_partial_frames,
                   (uint32_t)g_hda_mod.pcm_started,
                   (uint32_t)g_hda_mod.pcm_active,
                   g_hda_mod.pcm_last_lpib,
                   g_hda_mod.pcm_reclaim_bytes);
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

    driver_memset(&g_hda_mod, 0, sizeof(g_hda_mod));
    if (!driver_pci_find_by_class(HDA_PCI_CLASS_MULTIMEDIA,
                                  HDA_PCI_SUBCLASS_AUDIO,
                                  0u,
                                  &hda)) {
        return 0;
    }

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

    hda_mod_write32_local(HDA_REG_INTCTL, 0u);
    g_hda_mod.initialized = hda_mod_controller_reset_local() ? 1u : 0u;
    hda_mod_refresh_registers_local();
    hda_mod_probe_codec_vendor_local();
    if (!hda_mod_register_audio_local()) {
        return 0;
    }
    hda_mod_publish_status_local();
    hda_mod_log("driver: HDAMOD build=resync12 hwguard=%u resync_guard=%u prebuf=%u maxq=%u stream_chunk=%u\n",
                HDA_PCM_HW_GUARD_DESCRIPTORS,
                HDA_PCM_RECOVERY_GUARD_DESCRIPTORS,
                HDA_PCM_PREBUFFER_DESCRIPTORS,
                HDA_PCM_MAX_QUEUED_DESCRIPTORS,
                HDA_STREAM_READ_CHUNK_BYTES);
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
