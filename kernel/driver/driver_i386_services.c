#include "kernel/public/driver/driver.h"
#include "kernel/public/driver/driver_module.h"
#include "arch/x86/common/io.h"
#include "arch/x86/i386/paging.h"
#include "arch/x86/i386/pmm.h"
#include "drivers/audio/audio.h"
#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "drivers/bus/pci.h"
#include "drivers/net/rtl8139.h"
#include "hal/hal.h"
#include "kernel/internal/driver/driver_i386_legacy_internal.h"
#include "kernel/internal/driver/driver_loader_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/profile.h"
#include "lib/string.h"

/*
 * i386 .DRV compatibility bridge.
 *
 * The common driver manager is now built for i386 too. The service-symbol side
 * exports the kernel helper ABI used by ELF32 .DRV modules; the loader side
 * keeps only i386 ELF32 relocatable probing/loading. Keep new driver model
 * policy in the common driver manager.
 */

#define driver_alloc_pages driver_i386_legacy_alloc_pages
#define driver_alloc_pages_below driver_i386_legacy_alloc_pages_below
#define driver_free_pages driver_i386_legacy_free_pages
#define driver_mmio_map driver_i386_legacy_mmio_map
#define driver_str_len driver_i386_legacy_str_len
#define driver_streq driver_i386_legacy_streq
#define driver_starts_with driver_i386_legacy_starts_with
#define driver_memcpy driver_i386_legacy_memcpy
#define driver_memmove driver_i386_legacy_memmove
#define driver_memset driver_i386_legacy_memset
#define driver_audio_register_device driver_i386_legacy_audio_register_device
#define driver_ac97_publish_device driver_i386_legacy_ac97_publish_device
#define driver_hda_publish_device driver_i386_legacy_hda_publish_device
#define driver_pci_find_by_class driver_i386_legacy_pci_find_by_class
#define driver_pci_find_by_id driver_i386_legacy_pci_find_by_id
#define driver_pci_read8 driver_i386_legacy_pci_read8
#define driver_pci_read16 driver_i386_legacy_pci_read16
#define driver_pci_read32 driver_i386_legacy_pci_read32
#define driver_pci_write8 driver_i386_legacy_pci_write8
#define driver_pci_write16 driver_i386_legacy_pci_write16
#define driver_pci_write32 driver_i386_legacy_pci_write32
#define driver_io_in8 driver_i386_legacy_io_in8
#define driver_io_in16 driver_i386_legacy_io_in16
#define driver_io_in32 driver_i386_legacy_io_in32
#define driver_io_out8 driver_i386_legacy_io_out8
#define driver_io_out16 driver_i386_legacy_io_out16
#define driver_io_out32 driver_i386_legacy_io_out32
#define driver_timer_current_ticks driver_i386_legacy_timer_current_ticks
#define driver_timer_hz driver_i386_legacy_timer_hz
#define driver_cpu_wait_for_event driver_i386_legacy_cpu_wait_for_event
#define driver_cpu_relax driver_i386_legacy_cpu_relax

static struct driver_i386_module_allocation g_i386_driver_module_allocs[I386_DRIVER_MODULE_ALLOC_MAX_COUNT];
static uint32_t g_i386_driver_next_virt = I386_DRIVER_MODULE_VIRT_BASE;

uint64_t __udivdi3(uint64_t num, uint64_t den);
uint64_t __umoddi3(uint64_t num, uint64_t den);
int hda_i386_publish_status(const struct hda_status *status);

static struct driver_i386_module_allocation *driver_i386_alloc_slot(void) {
    for (uint32_t i = 0u; i < I386_DRIVER_MODULE_ALLOC_MAX_COUNT; i++) {
        if (g_i386_driver_module_allocs[i].virt == 0) {
            return &g_i386_driver_module_allocs[i];
        }
    }
    return 0;
}

static struct driver_i386_module_allocation *driver_i386_alloc_find(void *virt,
                                                                    uint32_t page_count) {
    if (virt == 0 || page_count == 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i < I386_DRIVER_MODULE_ALLOC_MAX_COUNT; i++) {
        if (g_i386_driver_module_allocs[i].virt == virt &&
            g_i386_driver_module_allocs[i].page_count == page_count) {
            return &g_i386_driver_module_allocs[i];
        }
    }
    return 0;
}

void *driver_i386_alloc_pages(uint32_t page_count,
                                     uint32_t *phys_out,
                                     uint32_t *alloc_size_out) {
    struct driver_i386_module_allocation *record;
    uint32_t virt;

    if (phys_out != 0) {
        *phys_out = 0u;
    }
    if (alloc_size_out != 0) {
        *alloc_size_out = 0u;
    }
    if (page_count == 0u || page_count > I386_DRIVER_MODULE_ALLOC_MAX_PAGES) {
        return 0;
    }
    record = driver_i386_alloc_slot();
    if (record == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < I386_DRIVER_MODULE_ALLOC_MAX_PAGES; i++) {
        record->phys_pages[i] = I386_PMM_INVALID_PAGE;
    }
    virt = g_i386_driver_next_virt;
    if (virt + page_count * I386_DRIVER_ELF_PAGE_SIZE < virt) {
        return 0;
    }
    for (uint32_t i = 0u; i < page_count; i++) {
        uint32_t phys = i386_pmm_alloc_page();

        if (phys == I386_PMM_INVALID_PAGE ||
            !i386_paging_map_page(virt + i * I386_DRIVER_ELF_PAGE_SIZE,
                                  phys,
                                  1,
                                  0)) {
            if (phys != I386_PMM_INVALID_PAGE) {
                (void)i386_pmm_free_page(phys);
            }
            for (uint32_t u = 0u; u < i; u++) {
                uint32_t ignored;

                (void)i386_paging_unmap_page(virt + u * I386_DRIVER_ELF_PAGE_SIZE,
                                             &ignored);
            }
            for (uint32_t r = 0u; r < i; r++) {
                if (record->phys_pages[r] != I386_PMM_INVALID_PAGE) {
                    (void)i386_pmm_free_page(record->phys_pages[r]);
                    record->phys_pages[r] = I386_PMM_INVALID_PAGE;
                }
            }
            return 0;
        }
        if (i == 0u) {
            record->phys = phys;
        }
        record->phys_pages[i] = phys;
    }
    memset((void *)(uintptr_t)virt, 0, page_count * I386_DRIVER_ELF_PAGE_SIZE);
    record->virt = (void *)(uintptr_t)virt;
    record->page_count = page_count;
    g_i386_driver_next_virt += page_count * I386_DRIVER_ELF_PAGE_SIZE;
    if (phys_out != 0) {
        *phys_out = record->phys;
    }
    if (alloc_size_out != 0) {
        *alloc_size_out = page_count * I386_DRIVER_ELF_PAGE_SIZE;
    }
    return record->virt;
}

void driver_i386_free_pages(void *virt, uint32_t page_count) {
    struct driver_i386_module_allocation *record;

    record = driver_i386_alloc_find(virt, page_count);
    if (record == 0) {
        return;
    }
    for (uint32_t i = 0u; i < record->page_count; i++) {
        uint32_t ignored;

        (void)i386_paging_unmap_page((uint32_t)(uintptr_t)record->virt +
                                         i * I386_DRIVER_ELF_PAGE_SIZE,
                                     &ignored);
        if (record->phys_pages[i] != I386_PMM_INVALID_PAGE) {
            (void)i386_pmm_free_page(record->phys_pages[i]);
            record->phys_pages[i] = I386_PMM_INVALID_PAGE;
        }
    }
    record->virt = 0;
    record->phys = 0u;
    record->page_count = 0u;
}

void *driver_alloc_pages(uint32_t page_count, uint64_t *phys_out) {
    uint32_t phys = 0u;

    void *virt = driver_i386_alloc_pages(page_count, &phys, 0);

    if (phys_out != 0) {
        *phys_out = phys;
    }
    return virt;
}

void *driver_alloc_pages_below(uint32_t page_count,
                               uint64_t max_phys_exclusive,
                               uint64_t *phys_out) {
    void *virt = driver_alloc_pages(page_count, phys_out);

    if (virt != 0 && phys_out != 0 && *phys_out >= max_phys_exclusive) {
        driver_i386_free_pages(virt, page_count);
        *phys_out = 0u;
        return 0;
    }
    return virt;
}

void driver_free_pages(void *virt, uint32_t page_count) {
    driver_i386_free_pages(virt, page_count);
}

void *driver_mmio_map(uint64_t phys) {
    if (phys > 0xffffffffull) {
        return 0;
    }
    return hal_mmio_map(phys, 4096u);
}

uint32_t driver_str_len(const char *text) {
    return str_len(text);
}

int driver_streq(const char *lhs, const char *rhs) {
    return streq(lhs, rhs);
}

int driver_starts_with(const char *text, const char *prefix) {
    return starts_with(text, prefix);
}

void *driver_memcpy(void *dst, const void *src, uint32_t size) {
    return memcpy(dst, src, size);
}

void *driver_memmove(void *dst, const void *src, uint32_t size) {
    return memmove(dst, src, size);
}

void *driver_memset(void *dst, int value, uint32_t size) {
    return memset(dst, value, size);
}

int driver_audio_register_device(const struct driver_audio_device_info *info,
                                 const struct driver_audio_device_ops *ops,
                                 void *ctx,
                                 uint32_t *index_out) {
    return audio_register_device((const struct audio_device_info *)info,
                                 (const struct audio_device_ops *)ops,
                                 ctx,
                                 index_out);
}

int driver_ac97_publish_device(const struct driver_ac97_device_info *info) {
    struct ac97_status status;

    if (info == 0) {
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

int driver_hda_publish_device(const struct driver_hda_device_info *info) {
    struct hda_status status;

    if (info == 0) {
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
    status.mmio_base_lo = info->mmio_base_lo;
    status.mmio_base_hi = info->mmio_base_hi;
    status.pci_command = info->pci_command;
    status.gcap = info->gcap;
    status.vmaj = info->vmaj;
    status.vmin = info->vmin;
    status.outpay = info->outpay;
    status.inpay = info->inpay;
    status.gctl = info->gctl;
    status.statests = info->statests;
    status.wakeen = info->wakeen;
    status.corb_size = info->corb_size;
    status.rirb_size = info->rirb_size;
    status.codec_mask = info->codec_mask;
    return hda_i386_publish_status(&status);
}

static void driver_i386_pci_copy(struct driver_pci_device *out,
                                 const struct pci_device_info *pci) {
    if (out == 0 || pci == 0) {
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
    driver_i386_pci_copy(out, &pci);
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
    driver_i386_pci_copy(out, &pci);
    return 1;
}

uint8_t driver_pci_read8(const struct driver_pci_device *dev, uint8_t offset) {
    return dev != 0 ? pci_config_read8(dev->bus, dev->slot, dev->function, offset) : 0xffu;
}

uint16_t driver_pci_read16(const struct driver_pci_device *dev, uint8_t offset) {
    return dev != 0 ? pci_config_read16(dev->bus, dev->slot, dev->function, offset) : 0xffffu;
}

uint32_t driver_pci_read32(const struct driver_pci_device *dev, uint8_t offset) {
    return dev != 0 ? pci_config_read32(dev->bus, dev->slot, dev->function, offset) : 0xffffffffu;
}

void driver_pci_write8(const struct driver_pci_device *dev, uint8_t offset, uint8_t value) {
    if (dev != 0) {
        pci_config_write8(dev->bus, dev->slot, dev->function, offset, value);
    }
}

void driver_pci_write16(const struct driver_pci_device *dev, uint8_t offset, uint16_t value) {
    if (dev != 0) {
        pci_config_write16(dev->bus, dev->slot, dev->function, offset, value);
    }
}

void driver_pci_write32(const struct driver_pci_device *dev, uint8_t offset, uint32_t value) {
    if (dev != 0) {
        pci_config_write32(dev->bus, dev->slot, dev->function, offset, value);
    }
}

uint8_t driver_io_in8(uint16_t port) { return inb(port); }
uint16_t driver_io_in16(uint16_t port) { return inw(port); }
uint32_t driver_io_in32(uint16_t port) { return inl(port); }
void driver_io_out8(uint16_t port, uint8_t value) { outb(port, value); }
void driver_io_out16(uint16_t port, uint16_t value) { outw(port, value); }
void driver_io_out32(uint16_t port, uint32_t value) { outl(port, value); }
uint32_t driver_timer_current_ticks(void) { return hal_timer_current_ticks(); }
uint32_t driver_timer_hz(void) { return hal_timer_hz(); }
void driver_cpu_wait_for_event(void) { hal_cpu_wait_for_event(); }
void driver_cpu_relax(void) { hal_cpu_relax(); }

static void driver_i386_module_log(const char *fmt, ...) {
    va_list ap;

    if (!driver_boot_verbose_enabled()) {
        return;
    }
    va_start(ap, fmt);
    vkprint(fmt, ap);
    va_end(ap);
}

static const struct driver_i386_kernel_symbol g_i386_driver_kernel_symbols[] = {
    { "driver_log", (uint32_t)(uintptr_t)driver_i386_module_log },
    { "driver_alloc_pages", (uint32_t)(uintptr_t)driver_alloc_pages },
    { "driver_alloc_pages_below", (uint32_t)(uintptr_t)driver_alloc_pages_below },
    { "driver_ac97_publish_device", (uint32_t)(uintptr_t)driver_ac97_publish_device },
    { "driver_audio_register_device", (uint32_t)(uintptr_t)driver_audio_register_device },
    { "driver_free_pages", (uint32_t)(uintptr_t)driver_free_pages },
    { "driver_hda_publish_device", (uint32_t)(uintptr_t)driver_hda_publish_device },
    { "driver_mmio_map", (uint32_t)(uintptr_t)driver_mmio_map },
    { "driver_memcpy", (uint32_t)(uintptr_t)memcpy },
    { "driver_memmove", (uint32_t)(uintptr_t)memmove },
    { "driver_memset", (uint32_t)(uintptr_t)memset },
    { "driver_io_in8", (uint32_t)(uintptr_t)driver_io_in8 },
    { "driver_io_in16", (uint32_t)(uintptr_t)driver_io_in16 },
    { "driver_io_in32", (uint32_t)(uintptr_t)driver_io_in32 },
    { "driver_io_out8", (uint32_t)(uintptr_t)driver_io_out8 },
    { "driver_io_out16", (uint32_t)(uintptr_t)driver_io_out16 },
    { "driver_io_out32", (uint32_t)(uintptr_t)driver_io_out32 },
    { "driver_pci_find_by_class", (uint32_t)(uintptr_t)driver_pci_find_by_class },
    { "driver_pci_find_by_id", (uint32_t)(uintptr_t)driver_pci_find_by_id },
    { "driver_pci_read8", (uint32_t)(uintptr_t)driver_pci_read8 },
    { "driver_pci_read16", (uint32_t)(uintptr_t)driver_pci_read16 },
    { "driver_pci_read32", (uint32_t)(uintptr_t)driver_pci_read32 },
    { "driver_pci_write8", (uint32_t)(uintptr_t)driver_pci_write8 },
    { "driver_pci_write16", (uint32_t)(uintptr_t)driver_pci_write16 },
    { "driver_pci_write32", (uint32_t)(uintptr_t)driver_pci_write32 },
    { "driver_starts_with", (uint32_t)(uintptr_t)starts_with },
    { "driver_streq", (uint32_t)(uintptr_t)streq },
    { "driver_str_len", (uint32_t)(uintptr_t)str_len },
    { "driver_timer_current_ticks", (uint32_t)(uintptr_t)driver_timer_current_ticks },
    { "driver_timer_hz", (uint32_t)(uintptr_t)driver_timer_hz },
    { "driver_cpu_wait_for_event", (uint32_t)(uintptr_t)hal_cpu_wait_for_event },
    { "driver_cpu_relax", (uint32_t)(uintptr_t)hal_cpu_relax },
    { "driver_profile_register", (uint32_t)(uintptr_t)kernel_profile_register },
    { "driver_profile_clock", (uint32_t)(uintptr_t)kernel_profile_clock },
    { "driver_profile_record", (uint32_t)(uintptr_t)kernel_profile_record },
    { "__udivdi3", (uint32_t)(uintptr_t)__udivdi3 },
    { "__umoddi3", (uint32_t)(uintptr_t)__umoddi3 },
    { 0, 0u }
};

int driver_i386_kernel_symbol_resolve(const char *name, uint32_t *value_out) {
    if (name == 0 || value_out == 0) {
        return 0;
    }
    for (uint32_t i = 0u; g_i386_driver_kernel_symbols[i].name != 0; i++) {
        if (streq(g_i386_driver_kernel_symbols[i].name, name)) {
            *value_out = g_i386_driver_kernel_symbols[i].value;
            return 1;
        }
    }
    return 0;
}
