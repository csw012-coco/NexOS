#include "drivers/audio/hda.h"

#include "drivers/audio/audio.h"
#include "drivers/bus/pci.h"
#include "hal/hal.h"
#include "kernel/public/driver/driver_module.h"

enum {
    PCI_COMMAND_MEMORY = 1u << 1,
    PCI_COMMAND_BUS_MASTER = 1u << 2,
    HDA_REG_GCAP = 0x00,
    HDA_REG_VMIN = 0x02,
    HDA_REG_VMAJ = 0x03,
    HDA_REG_OUTPAY = 0x04,
    HDA_REG_INPAY = 0x06,
    HDA_REG_GCTL = 0x08,
    HDA_REG_WAKEEN = 0x0c,
    HDA_REG_STATESTS = 0x0e,
    HDA_REG_CORBSIZE = 0x4e,
    HDA_REG_RIRBSIZE = 0x5e,
    HDA_GCTL_CRST = 1u,
    HDA_SAMPLE_RATE = 48000u,
    HDA_MMIO_MAP_SIZE = 0x4000u,

    HDA_STATE_ABSENT = 0u,
    HDA_STATE_PCI_FOUND = 1u,
    HDA_STATE_MAPPED = 2u,
    HDA_STATE_RESETTING = 3u,
    HDA_STATE_CODEC_DETECT = 4u,
    HDA_STATE_READY = 5u,
    HDA_STATE_FAILED = 6u,

    HDA_ERROR_NONE = 0u,
    HDA_ERROR_BAD_BAR = 1u,
    HDA_ERROR_MMIO_MAP = 2u,
    HDA_ERROR_CRST_CLEAR_TIMEOUT = 3u,
    HDA_ERROR_CRST_SET_TIMEOUT = 4u,
    HDA_ERROR_NO_CODEC = 5u
};

static struct hda_status g_hda_status;
static volatile uint8_t *g_hda_mmio;
static uint8_t g_hda_audio_registered;

static void hda_set_state(uint32_t state) {
    g_hda_status.state = state;
}

static void hda_note_error(uint32_t error) {
    g_hda_status.last_error = error;
    if (error != HDA_ERROR_NONE) {
        g_hda_status.error_count++;
        hda_set_state(HDA_STATE_FAILED);
    }
}

const struct kernel_driver hda_kernel_driver = {
    .name = "HDA",
    .kind = KERNEL_DRIVER_KIND_AUDIO,
    .init = hda_init,
    .exit = NULL,
};

static uint64_t hda_mmio_base_from_bar(uint32_t bar_lo, uint32_t bar_hi) {
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

static uint8_t hda_read8(uint32_t offset) {
    return *(volatile uint8_t *)(g_hda_mmio + offset);
}

static uint16_t hda_read16(uint32_t offset) {
    return *(volatile uint16_t *)(g_hda_mmio + offset);
}

static uint32_t hda_read32(uint32_t offset) {
    return *(volatile uint32_t *)(g_hda_mmio + offset);
}

static void hda_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(g_hda_mmio + offset) = value;
}

static void hda_write16(uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(g_hda_mmio + offset) = value;
}

static void hda_delay_local(uint32_t spins) {
    volatile uint32_t i;

    for (i = 0; i < spins; i++) {
        __asm__ __volatile__("" ::: "memory");
    }
}

static void hda_fill_name_local(char dst[32], const char *src) {
    uint32_t i = 0;

    while (src != 0 && src[i] != '\0' && i + 1u < 32u) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void hda_refresh_registers(void) {
    if (g_hda_mmio == 0) {
        return;
    }
    g_hda_status.gcap = hda_read16(HDA_REG_GCAP);
    g_hda_status.vmin = hda_read8(HDA_REG_VMIN);
    g_hda_status.vmaj = hda_read8(HDA_REG_VMAJ);
    g_hda_status.outpay = hda_read16(HDA_REG_OUTPAY);
    g_hda_status.inpay = hda_read16(HDA_REG_INPAY);
    g_hda_status.gctl = hda_read32(HDA_REG_GCTL);
    g_hda_status.statests = hda_read16(HDA_REG_STATESTS);
    g_hda_status.wakeen = hda_read16(HDA_REG_WAKEEN);
    g_hda_status.corb_size = hda_read8(HDA_REG_CORBSIZE);
    g_hda_status.rirb_size = hda_read8(HDA_REG_RIRBSIZE);
    g_hda_status.codec_mask = g_hda_status.statests & 0x7fffu;
}

static int hda_wait_gctl_bits(uint32_t mask, int set) {
    uint32_t spins;

    for (spins = 0; spins < 200000u; spins++) {
        uint32_t value = hda_read32(HDA_REG_GCTL) & mask;

        if ((set && value == mask) || (!set && value == 0u)) {
            return 1;
        }
    }
    return 0;
}

static int hda_wait_codec_mask(uint32_t *mask_out) {
    uint32_t stable_mask = 0u;
    uint32_t stable_count = 0u;

    hda_set_state(HDA_STATE_CODEC_DETECT);
    for (uint32_t attempt = 0u; attempt < 1000u; attempt++) {
        uint32_t mask;

        g_hda_status.codec_detect_attempts++;
        mask = hda_read16(HDA_REG_STATESTS) & 0x7fffu;
        if (mask != 0u && mask == stable_mask) {
            stable_count++;
            if (stable_count >= 3u) {
                if (mask_out != 0) {
                    *mask_out = mask;
                }
                return 1;
            }
        } else {
            stable_mask = mask;
            stable_count = mask != 0u ? 1u : 0u;
        }
        hda_delay_local(1000u);
    }
    if (mask_out != 0) {
        *mask_out = stable_mask;
    }
    return stable_mask != 0u;
}

static int hda_reset_controller(void) {
    uint32_t codec_mask = 0u;

    if (g_hda_mmio == 0) {
        hda_note_error(HDA_ERROR_MMIO_MAP);
        return 0;
    }
    g_hda_status.reset_attempts++;
    hda_set_state(HDA_STATE_RESETTING);
    hda_write32(HDA_REG_GCTL, hda_read32(HDA_REG_GCTL) & ~HDA_GCTL_CRST);
    if (!hda_wait_gctl_bits(HDA_GCTL_CRST, 0)) {
        hda_refresh_registers();
        hda_note_error(HDA_ERROR_CRST_CLEAR_TIMEOUT);
        return 0;
    }
    hda_delay_local(1000u);
    hda_write16(HDA_REG_STATESTS, 0x7fffu);
    hda_write32(HDA_REG_GCTL, hda_read32(HDA_REG_GCTL) | HDA_GCTL_CRST);
    if (!hda_wait_gctl_bits(HDA_GCTL_CRST, 1)) {
        hda_refresh_registers();
        hda_note_error(HDA_ERROR_CRST_SET_TIMEOUT);
        return 0;
    }
    hda_delay_local(1000u);
    (void)hda_wait_codec_mask(&codec_mask);
    hda_refresh_registers();
    if (codec_mask == 0u && g_hda_status.codec_mask == 0u) {
        hda_note_error(HDA_ERROR_NO_CODEC);
        return 0;
    }
    hda_note_error(HDA_ERROR_NONE);
    hda_set_state(HDA_STATE_READY);
    return 1;
}

static void hda_register_audio_device(void) {
    struct audio_device_info info;

    if (g_hda_audio_registered || !g_hda_status.present) {
        return;
    }

    info.present = 1u;
    info.initialized = g_hda_status.initialized;
    info.caps = 0u;
    info.driver_kind = AUDIO_DRIVER_HDA;
    info.sample_rate = HDA_SAMPLE_RATE;
    info.channels = 2u;
    info.bits_per_sample = 16u;
    hda_fill_name_local(info.name, "Intel HD Audio");

    if (audio_register_device(&info, 0, 0, 0)) {
        g_hda_audio_registered = 1u;
    }
}

int hda_init(void) {
    struct pci_hda_controller hda;
    uint64_t mmio_base;
    uint16_t command;

    g_hda_status.present = 0u;
    g_hda_status.initialized = 0u;
    hda_set_state(HDA_STATE_ABSENT);

    if (!pci_find_hda_controller(&hda)) {
        return 0;
    }

    g_hda_status.present = 1u;
    g_hda_status.bus = hda.bus;
    g_hda_status.slot = hda.slot;
    g_hda_status.function = hda.function;
    g_hda_status.prog_if = hda.prog_if;
    g_hda_status.irq_line = hda.irq_line;
    g_hda_status.irq_pin = hda.irq_pin;
    g_hda_status.vendor_id = hda.vendor_id;
    g_hda_status.device_id = hda.device_id;
    g_hda_status.mmio_base_lo = hda.mmio_base_lo;
    g_hda_status.mmio_base_hi = hda.mmio_base_hi;
    hda_set_state(HDA_STATE_PCI_FOUND);

    mmio_base = hda_mmio_base_from_bar(hda.mmio_base_lo, hda.mmio_base_hi);
    if (mmio_base == 0u) {
        hda_note_error(HDA_ERROR_BAD_BAR);
        hda_register_audio_device();
        return 0;
    }

    command = pci_config_read16(hda.bus, hda.slot, hda.function, 0x04);
    command = (uint16_t)(command | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER);
    pci_config_write16(hda.bus, hda.slot, hda.function, 0x04, command);
    g_hda_status.pci_command = pci_config_read16(hda.bus, hda.slot, hda.function, 0x04);

    g_hda_mmio = (volatile uint8_t *)hal_mmio_map(mmio_base, HDA_MMIO_MAP_SIZE);
    if (g_hda_mmio == 0) {
        hda_note_error(HDA_ERROR_MMIO_MAP);
        hda_register_audio_device();
        return 0;
    }
    hda_set_state(HDA_STATE_MAPPED);

    g_hda_status.initialized = hda_reset_controller() ? 1u : 0u;
    hda_refresh_registers();
    hda_register_audio_device();
    return g_hda_status.initialized != 0u;
}

int hda_query_status(struct hda_status *out) {
    if (out == 0 || !g_hda_status.present) {
        return 0;
    }
    hda_refresh_registers();
    *out = g_hda_status;
    return 1;
}

int driver_hda_publish_device(const struct driver_hda_device_info *info) {
    if (info == NULL) {
        return 0;
    }
    g_hda_status.present = (uint8_t)info->present;
    g_hda_status.initialized = (uint8_t)info->initialized;
    g_hda_status.irq_line = (uint8_t)info->irq_line;
    g_hda_status.irq_pin = (uint8_t)info->irq_pin;
    g_hda_status.bus = (uint8_t)info->bus;
    g_hda_status.slot = (uint8_t)info->slot;
    g_hda_status.function = (uint8_t)info->function;
    g_hda_status.prog_if = (uint8_t)info->prog_if;
    g_hda_status.vendor_id = (uint16_t)info->vendor_id;
    g_hda_status.device_id = (uint16_t)info->device_id;
    g_hda_status.mmio_base_lo = info->mmio_base_lo;
    g_hda_status.mmio_base_hi = info->mmio_base_hi;
    g_hda_status.pci_command = info->pci_command;
    g_hda_status.gcap = info->gcap;
    g_hda_status.vmaj = info->vmaj;
    g_hda_status.vmin = info->vmin;
    g_hda_status.outpay = info->outpay;
    g_hda_status.inpay = info->inpay;
    g_hda_status.gctl = info->gctl;
    g_hda_status.statests = info->statests;
    g_hda_status.wakeen = info->wakeen;
    g_hda_status.corb_size = info->corb_size;
    g_hda_status.rirb_size = info->rirb_size;
    g_hda_status.codec_mask = info->codec_mask;
    hda_set_state(info->initialized != 0u ? HDA_STATE_READY : HDA_STATE_PCI_FOUND);
    hda_note_error(HDA_ERROR_NONE);
    return 1;
}
