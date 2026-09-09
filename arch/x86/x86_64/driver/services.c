#include "kernel/public/driver/driver.h"
#include "kernel/public/driver/driver_module.h"
#include "arch/x86/common/io.h"
#include "drivers/audio/audio.h"
#include "drivers/audio/ac97.h"
#include "drivers/bus/pci.h"
#include "hal/hal.h"
#include "kernel/internal/driver/driver_loader_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/profile.h"
#include "arch/x86/x86_64/mm/pmm.h"
#include "lib/string.h"

#include <stdarg.h>

#define DRIVER_ELF64_MODULE_ALLOC_MAX_COUNT 64u
#define DRIVER_ELF64_MODULE_ALLOC_MAX_PAGES 256u
#define DRIVER_ELF64_PAGE_SIZE 4096u

struct driver_elf64_kernel_symbol {
    const char *name;
    uintptr_t value;
};

struct driver_elf64_module_allocation {
    void *virt;
    uint64_t phys;
    uint32_t page_count;
};

static struct driver_elf64_module_allocation
    g_driver_elf64_module_allocs[DRIVER_ELF64_MODULE_ALLOC_MAX_COUNT];

static void driver_elf64_copy_text(char *dst, const char *src, uint32_t dst_size) {
    uint32_t i = 0;

    if (dst == NULL || dst_size == 0u) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static struct driver_elf64_module_allocation *driver_elf64_alloc_find(void *virt,
                                                                      uint32_t page_count) {
    if (virt == NULL || page_count == 0u) {
        return NULL;
    }
    for (uint32_t i = 0u; i < DRIVER_ELF64_MODULE_ALLOC_MAX_COUNT; i++) {
        if (g_driver_elf64_module_allocs[i].virt == virt &&
            g_driver_elf64_module_allocs[i].page_count == page_count) {
            return &g_driver_elf64_module_allocs[i];
        }
    }
    return NULL;
}

static struct driver_elf64_module_allocation *driver_elf64_alloc_free_slot(void) {
    for (uint32_t i = 0u; i < DRIVER_ELF64_MODULE_ALLOC_MAX_COUNT; i++) {
        if (g_driver_elf64_module_allocs[i].virt == NULL) {
            return &g_driver_elf64_module_allocs[i];
        }
    }
    return NULL;
}

void *driver_alloc_pages(uint32_t page_count, uint64_t *phys_out) {
    struct driver_elf64_module_allocation *record;
    uint64_t phys;
    void *virt;

    if (phys_out != NULL) {
        *phys_out = 0;
    }
    if (page_count == 0u || page_count > DRIVER_ELF64_MODULE_ALLOC_MAX_PAGES) {
        return NULL;
    }
    record = driver_elf64_alloc_free_slot();
    if (record == NULL) {
        return NULL;
    }
    if (page_count == 1u) {
        phys = pmm_alloc_page();
    } else {
        phys = pmm_alloc_contiguous(page_count);
    }
    if (phys == 0u) {
        return NULL;
    }
    virt = hal_phys_direct_map(phys);
    if (virt == NULL) {
        for (uint32_t i = 0u; i < page_count; i++) {
            (void)pmm_free_page(phys + (uint64_t)i * DRIVER_ELF64_PAGE_SIZE);
        }
        return NULL;
    }
    memset(virt, 0, page_count * DRIVER_ELF64_PAGE_SIZE);
    record->virt = virt;
    record->phys = phys;
    record->page_count = page_count;
    if (phys_out != NULL) {
        *phys_out = phys;
    }
    return virt;
}

void *driver_alloc_pages_below(uint32_t page_count,
                               uint64_t max_phys_exclusive,
                               uint64_t *phys_out) {
    struct driver_elf64_module_allocation *record;
    uint64_t phys;
    void *virt;

    if (phys_out != NULL) {
        *phys_out = 0;
    }
    if (page_count == 0u || page_count > DRIVER_ELF64_MODULE_ALLOC_MAX_PAGES) {
        return NULL;
    }
    record = driver_elf64_alloc_free_slot();
    if (record == NULL) {
        return NULL;
    }
    phys = pmm_alloc_contiguous_below(page_count, max_phys_exclusive);
    if (phys == 0u) {
        return NULL;
    }
    virt = hal_phys_direct_map(phys);
    if (virt == NULL) {
        for (uint32_t i = 0u; i < page_count; i++) {
            (void)pmm_free_page(phys + (uint64_t)i * DRIVER_ELF64_PAGE_SIZE);
        }
        return NULL;
    }
    memset(virt, 0, page_count * DRIVER_ELF64_PAGE_SIZE);
    record->virt = virt;
    record->phys = phys;
    record->page_count = page_count;
    if (phys_out != NULL) {
        *phys_out = phys;
    }
    return virt;
}

void driver_free_pages(void *virt, uint32_t page_count) {
    struct driver_elf64_module_allocation *record;

    record = driver_elf64_alloc_find(virt, page_count);
    if (record == NULL) {
        return;
    }
    for (uint32_t i = 0u; i < record->page_count; i++) {
        (void)pmm_free_page(record->phys + (uint64_t)i * DRIVER_ELF64_PAGE_SIZE);
    }
    record->virt = NULL;
    record->phys = 0;
    record->page_count = 0;
}

void *driver_mmio_map(uint64_t phys) {
    if (phys == 0u) {
        return NULL;
    }
    return hal_phys_direct_map(phys);
}

int driver_audio_register_device(const struct driver_audio_device_info *driver_info,
                                 const struct driver_audio_device_ops *driver_ops,
                                 void *ctx,
                                 uint32_t *index_out) {
    struct audio_device_info info;

    if (driver_info == NULL) {
        return 0;
    }
    info.present = driver_info->present;
    info.initialized = driver_info->initialized;
    info.caps = driver_info->caps;
    info.driver_kind = driver_info->driver_kind;
    info.sample_rate = driver_info->sample_rate;
    info.channels = driver_info->channels;
    info.bits_per_sample = driver_info->bits_per_sample;
    driver_elf64_copy_text(info.name, driver_info->name, sizeof(info.name));
    return audio_register_device(&info,
                                 (const struct audio_device_ops *)driver_ops,
                                 ctx,
                                 index_out);
}

int driver_ac97_publish_device(const struct driver_ac97_device_info *info) {
    struct ac97_status status;

    if (info == NULL) {
        return 0;
    }
    memset(&status, 0, sizeof(status));
    status.present = (uint8_t)info->present;
    status.initialized = (uint8_t)info->initialized;
    status.irq_line = (uint8_t)info->irq_line;
    status.irq_pin = (uint8_t)info->irq_pin;
    status.bus = (uint8_t)info->bus;
    status.slot = (uint8_t)info->slot;
    status.function = (uint8_t)info->function;
    status.prog_if = (uint8_t)info->prog_if;
    status.vendor_id = (uint16_t)info->vendor_id;
    status.device_id = (uint16_t)info->device_id;
    status.nambar = info->nambar;
    status.nabmbar = info->nabmbar;
    status.mixer_reset = info->mixer_reset;
    status.powerdown = info->powerdown;
    status.ext_audio_id = info->ext_audio_id;
    status.ext_audio_ctrl = info->ext_audio_ctrl;
    status.codec_id = info->codec_id;
    status.global_status = info->global_status;
    status.global_control = info->global_control;
    return ac97_publish_status(&status);
}

uint8_t driver_io_in8(uint16_t port) {
    return inb(port);
}

uint16_t driver_io_in16(uint16_t port) {
    return inw(port);
}

uint32_t driver_io_in32(uint16_t port) {
    return inl(port);
}

void driver_io_out8(uint16_t port, uint8_t value) {
    outb(port, value);
}

void driver_io_out16(uint16_t port, uint16_t value) {
    outw(port, value);
}

void driver_io_out32(uint16_t port, uint32_t value) {
    outl(port, value);
}

static void driver_elf64_pci_copy(struct driver_pci_device *out,
                                  const struct pci_device_info *pci) {
    if (out == NULL || pci == NULL) {
        return;
    }
    out->bus = pci->bus;
    out->slot = pci->slot;
    out->function = pci->function;
    out->class_code = pci->class_code;
    out->subclass = pci->subclass;
    out->prog_if = pci->prog_if;
    out->irq_line = pci->irq_line;
    out->irq_pin = pci->irq_pin;
    out->vendor_id = pci->vendor_id;
    out->device_id = pci->device_id;
    out->bar[0] = pci->bar0;
    out->bar[1] = pci->bar1;
    out->bar[2] = pci->bar2;
    out->bar[3] = pci->bar3;
    out->bar[4] = pci->bar4;
    out->bar[5] = pci->bar5;
}

int driver_pci_find_by_class(uint8_t class_code,
                             uint8_t subclass,
                             uint32_t index,
                             struct driver_pci_device *out) {
    struct pci_device_info pci;

    if (!pci_find_device_by_class_at(class_code, subclass, index, &pci)) {
        return 0;
    }
    driver_elf64_pci_copy(out, &pci);
    return 1;
}

int driver_pci_find_by_id(uint16_t vendor_id,
                          uint16_t device_id,
                          uint32_t index,
                          struct driver_pci_device *out) {
    struct pci_device_info pci;

    if (!pci_find_device_at(vendor_id, device_id, index, &pci)) {
        return 0;
    }
    driver_elf64_pci_copy(out, &pci);
    return 1;
}

uint8_t driver_pci_read8(const struct driver_pci_device *dev, uint8_t offset) {
    return dev != NULL ? pci_config_read8(dev->bus, dev->slot, dev->function, offset) : 0xffu;
}

uint16_t driver_pci_read16(const struct driver_pci_device *dev, uint8_t offset) {
    return dev != NULL ? pci_config_read16(dev->bus, dev->slot, dev->function, offset) : 0xffffu;
}

uint32_t driver_pci_read32(const struct driver_pci_device *dev, uint8_t offset) {
    return dev != NULL ? pci_config_read32(dev->bus, dev->slot, dev->function, offset) : 0xffffffffu;
}

void driver_pci_write8(const struct driver_pci_device *dev, uint8_t offset, uint8_t value) {
    if (dev != NULL) {
        pci_config_write8(dev->bus, dev->slot, dev->function, offset, value);
    }
}

void driver_pci_write16(const struct driver_pci_device *dev, uint8_t offset, uint16_t value) {
    if (dev != NULL) {
        pci_config_write16(dev->bus, dev->slot, dev->function, offset, value);
    }
}

void driver_pci_write32(const struct driver_pci_device *dev, uint8_t offset, uint32_t value) {
    if (dev != NULL) {
        pci_config_write32(dev->bus, dev->slot, dev->function, offset, value);
    }
}

uint32_t driver_timer_current_ticks(void) {
    return hal_timer_current_ticks();
}

uint32_t driver_timer_hz(void) {
    return hal_timer_hz();
}

void driver_cpu_wait_for_event(void) {
    hal_cpu_wait_for_event();
}

void driver_cpu_relax(void) {
    hal_cpu_relax();
}

static void driver_elf64_module_log(const char *fmt, ...) {
    va_list ap;

    if (!driver_boot_verbose_enabled()) {
        return;
    }
    va_start(ap, fmt);
    vkprint(fmt, ap);
    va_end(ap);
}

static const struct driver_elf64_kernel_symbol g_driver_elf64_kernel_symbols[] = {
    { "driver_log", (uintptr_t)driver_elf64_module_log },
    { "driver_alloc_pages", (uintptr_t)driver_alloc_pages },
    { "driver_alloc_pages_below", (uintptr_t)driver_alloc_pages_below },
    { "driver_ac97_publish_device", (uintptr_t)driver_ac97_publish_device },
    { "driver_audio_register_device", (uintptr_t)driver_audio_register_device },
    { "driver_free_pages", (uintptr_t)driver_free_pages },
    { "driver_hda_publish_device", (uintptr_t)driver_hda_publish_device },
    { "driver_mmio_map", (uintptr_t)driver_mmio_map },
    { "driver_memcpy", (uintptr_t)memcpy },
    { "driver_memmove", (uintptr_t)memmove },
    { "driver_memset", (uintptr_t)memset },
    { "driver_io_in8", (uintptr_t)driver_io_in8 },
    { "driver_io_in16", (uintptr_t)driver_io_in16 },
    { "driver_io_in32", (uintptr_t)driver_io_in32 },
    { "driver_io_out8", (uintptr_t)driver_io_out8 },
    { "driver_io_out16", (uintptr_t)driver_io_out16 },
    { "driver_io_out32", (uintptr_t)driver_io_out32 },
    { "driver_pci_find_by_class", (uintptr_t)driver_pci_find_by_class },
    { "driver_pci_find_by_id", (uintptr_t)driver_pci_find_by_id },
    { "driver_pci_read8", (uintptr_t)driver_pci_read8 },
    { "driver_pci_read16", (uintptr_t)driver_pci_read16 },
    { "driver_pci_read32", (uintptr_t)driver_pci_read32 },
    { "driver_pci_write8", (uintptr_t)driver_pci_write8 },
    { "driver_pci_write16", (uintptr_t)driver_pci_write16 },
    { "driver_pci_write32", (uintptr_t)driver_pci_write32 },
    { "driver_starts_with", (uintptr_t)starts_with },
    { "driver_streq", (uintptr_t)streq },
    { "driver_str_len", (uintptr_t)str_len },
    { "driver_timer_current_ticks", (uintptr_t)driver_timer_current_ticks },
    { "driver_timer_hz", (uintptr_t)driver_timer_hz },
    { "driver_cpu_wait_for_event", (uintptr_t)driver_cpu_wait_for_event },
    { "driver_cpu_relax", (uintptr_t)driver_cpu_relax },
    { "driver_profile_register", (uintptr_t)kernel_profile_register },
    { "driver_profile_clock", (uintptr_t)kernel_profile_clock },
    { "driver_profile_record", (uintptr_t)kernel_profile_record },
    { NULL, 0 }
};

int driver_elf64_kernel_symbol_resolve(const char *name, uint64_t *value_out) {
    if (name == NULL || value_out == NULL) {
        return 0;
    }
    for (uint32_t i = 0u; g_driver_elf64_kernel_symbols[i].name != NULL; i++) {
        if (streq(g_driver_elf64_kernel_symbols[i].name, name)) {
            *value_out = g_driver_elf64_kernel_symbols[i].value;
            return 1;
        }
    }
    return 0;
}
