#include "kernel/public/driver/driver.h"
#include "kernel/public/driver/driver_module.h"
#include "arch/x86/common/io.h"
#include "arch/x86/i386/paging.h"
#include "arch/x86/i386/pmm.h"
#include "drivers/audio/audio.h"
#include "drivers/audio/hda.h"
#include "drivers/bus/pci.h"
#include "fs/vfs.h"
#include "hal/hal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/profile.h"
#include "lib/string.h"

enum {
    I386_DRIVER_MAX_COUNT = 16u,
    I386_DRIVER_FILE_MAX_COUNT = 16u,
    I386_DRIVER_MODULE_ALLOC_MAX_COUNT = 32u,
    I386_DRIVER_MODULE_ALLOC_MAX_PAGES = 256u,
    I386_DRIVER_MODULE_VIRT_BASE = 0xd1000000u,
    I386_DRIVER_ELF_MAX_SECTIONS = 128u,
    I386_DRIVER_ELF_MAX_FILE_SIZE = 1024u * 1024u,
    I386_DRIVER_ELF_PAGE_SIZE = 4096u,
    I386_DRIVER_ELF_IDENT_SIZE = 16u,
    I386_DRIVER_ELF_HEADER_SIZE = 52u,
    I386_DRIVER_ELF_CLASS_32 = 1u,
    I386_DRIVER_ELF_DATA_LSB = 1u,
    I386_DRIVER_ELF_ET_REL = 1u,
    I386_DRIVER_ELF_EM_386 = 3u,
    I386_DRIVER_ELF_SHT_SYMTAB = 2u,
    I386_DRIVER_ELF_SHT_STRTAB = 3u,
    I386_DRIVER_ELF_SHT_REL = 9u,
    I386_DRIVER_ELF_SHT_NOBITS = 8u,
    I386_DRIVER_ELF_SHF_ALLOC = 0x2u,
    I386_DRIVER_ELF_SHN_UNDEF = 0u,
    I386_DRIVER_ELF_SHN_ABS = 0xfff1u,
    I386_DRIVER_ELF_R_386_32 = 1u,
    I386_DRIVER_ELF_R_386_PC32 = 2u
};

struct driver_i386_elf32_header {
    uint8_t ident[I386_DRIVER_ELF_IDENT_SIZE];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct driver_i386_elf32_section {
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
} __attribute__((packed));

struct driver_i386_elf32_symbol {
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
} __attribute__((packed));

struct driver_i386_elf32_rel {
    uint32_t offset;
    uint32_t info;
} __attribute__((packed));

struct driver_i386_kernel_symbol {
    const char *name;
    uint32_t value;
};

struct driver_i386_module_allocation {
    void *virt;
    uint32_t phys;
    uint32_t phys_pages[I386_DRIVER_MODULE_ALLOC_MAX_PAGES];
    uint32_t page_count;
};

static struct kernel_driver_record g_i386_driver_records[I386_DRIVER_MAX_COUNT];
static struct kernel_driver_file g_i386_driver_files[I386_DRIVER_FILE_MAX_COUNT];
static struct driver_i386_module_allocation g_i386_driver_module_allocs[I386_DRIVER_MODULE_ALLOC_MAX_COUNT];
static uint32_t g_i386_driver_count;
static uint32_t g_i386_driver_file_count;
static uint32_t g_i386_driver_next_virt = I386_DRIVER_MODULE_VIRT_BASE;

int hda_i386_publish_status(const struct hda_status *status);

static const char *driver_i386_elf_symbol_name(const uint8_t *image,
                                               uint32_t file_size,
                                               const struct driver_i386_elf32_section *sections,
                                               const struct driver_i386_elf32_section *sym_section,
                                               const struct driver_i386_elf32_symbol *symbol);
static int driver_i386_kernel_symbol_resolve(const char *name, uint32_t *value_out);
uint64_t __udivdi3(uint64_t num, uint64_t den);
uint64_t __umoddi3(uint64_t num, uint64_t den);

static int driver_i386_name_valid(const char *name) {
    uint32_t len;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    len = str_len(name);
    return len != 0u && len <= KERNEL_DRIVER_NAME_MAX;
}

static struct kernel_driver_record *driver_i386_find_mutable(const char *name) {
    if (!driver_i386_name_valid(name)) {
        return 0;
    }
    for (uint32_t i = 0u; i < g_i386_driver_count; i++) {
        if (g_i386_driver_records[i].driver != 0 &&
            streq(g_i386_driver_records[i].driver->name, name)) {
            return &g_i386_driver_records[i];
        }
    }
    return 0;
}

static void driver_i386_copy_text(char *dst, const char *src, uint32_t dst_size) {
    uint32_t i;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    for (i = 0u; i + 1u < dst_size && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int driver_i386_ascii_lower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static int driver_i386_name_has_drv_suffix(const char *name) {
    uint32_t len;

    if (name == 0) {
        return 0;
    }
    len = str_len(name);
    if (len < 4u) {
        return 0;
    }
    return name[len - 4u] == '.' &&
           driver_i386_ascii_lower(name[len - 3u]) == 'd' &&
           driver_i386_ascii_lower(name[len - 2u]) == 'r' &&
           driver_i386_ascii_lower(name[len - 1u]) == 'v';
}

static int driver_i386_join_path(char *dst,
                                 uint32_t dst_size,
                                 const char *directory,
                                 const char *name) {
    uint32_t out = 0u;
    uint32_t i;

    if (dst == 0 || dst_size == 0u || directory == 0 || name == 0) {
        return 0;
    }
    for (i = 0u; directory[i] != '\0' && out + 1u < dst_size; i++) {
        dst[out++] = directory[i];
    }
    if (out == 0u || out + 1u >= dst_size) {
        dst[0] = '\0';
        return 0;
    }
    if (dst[out - 1u] != '/') {
        dst[out++] = '/';
    }
    for (i = 0u; name[i] != '\0' && out + 1u < dst_size; i++) {
        dst[out++] = name[i];
    }
    if (name[i] != '\0') {
        dst[0] = '\0';
        return 0;
    }
    dst[out] = '\0';
    return 1;
}

static int driver_i386_file_exists(const char *path) {
    for (uint32_t i = 0u; i < g_i386_driver_file_count; i++) {
        if (streq(g_i386_driver_files[i].path, path)) {
            return 1;
        }
    }
    return 0;
}

static uint32_t driver_i386_align_up(uint32_t value, uint32_t align) {
    if (align <= 1u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static int driver_i386_range_valid(uint32_t offset, uint32_t size, uint32_t file_size) {
    return offset <= file_size && size <= file_size - offset;
}

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

static void *driver_i386_alloc_pages(uint32_t page_count,
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

static void driver_i386_free_pages(void *virt, uint32_t page_count) {
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
        driver_free_pages(virt, page_count);
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
    return (void *)(uintptr_t)(uint32_t)phys;
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

static const struct driver_i386_kernel_symbol g_i386_driver_kernel_symbols[] = {
    { "driver_log", (uint32_t)(uintptr_t)kprint },
    { "driver_alloc_pages", (uint32_t)(uintptr_t)driver_alloc_pages },
    { "driver_alloc_pages_below", (uint32_t)(uintptr_t)driver_alloc_pages_below },
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

void driver_manager_init(void) {
    for (uint32_t i = 0u; i < I386_DRIVER_MAX_COUNT; i++) {
        g_i386_driver_records[i].driver = 0;
        g_i386_driver_records[i].state = KERNEL_DRIVER_STATE_EMPTY;
        g_i386_driver_records[i].init_result = 0;
        g_i386_driver_records[i].source = "builtin";
        g_i386_driver_records[i].path = "-";
        g_i386_driver_records[i].reason = "empty";
    }
    for (uint32_t i = 0u; i < I386_DRIVER_FILE_MAX_COUNT; i++) {
        g_i386_driver_files[i].name[0] = '\0';
        g_i386_driver_files[i].path[0] = '\0';
        g_i386_driver_files[i].size = 0u;
        g_i386_driver_files[i].elf_class = 0u;
        g_i386_driver_files[i].elf_data = 0u;
        g_i386_driver_files[i].elf_type = 0u;
        g_i386_driver_files[i].elf_machine = 0u;
        g_i386_driver_files[i].state = KERNEL_DRIVER_FILE_DISCOVERED;
        g_i386_driver_files[i].reason = "empty";
    }
    g_i386_driver_count = 0u;
    g_i386_driver_file_count = 0u;
}

int driver_register(const struct kernel_driver *driver) {
    struct kernel_driver_record *record;

    if (driver == 0 ||
        !driver_i386_name_valid(driver->name) ||
        driver->init == 0 ||
        g_i386_driver_count >= I386_DRIVER_MAX_COUNT ||
        driver_i386_find_mutable(driver->name) != 0) {
        return 0;
    }
    record = &g_i386_driver_records[g_i386_driver_count++];
    record->driver = driver;
    record->state = KERNEL_DRIVER_STATE_REGISTERED;
    record->init_result = 0;
    record->source = "builtin-i386";
    record->path = "-";
    record->reason = "registered";
    return 1;
}

uint32_t driver_init_all(void) {
    uint32_t active_count = 0u;

    for (uint32_t i = 0u; i < g_i386_driver_count; i++) {
        int result;
        struct kernel_driver_record *record = &g_i386_driver_records[i];

        if (record->state != KERNEL_DRIVER_STATE_REGISTERED ||
            record->driver == 0 ||
            record->driver->init == 0) {
            continue;
        }
        result = record->driver->init();
        record->init_result = result;
        if (result > 0) {
            record->state = KERNEL_DRIVER_STATE_ACTIVE;
            record->reason = "init-ok";
            active_count++;
        } else if (result == 0) {
            record->state = KERNEL_DRIVER_STATE_INACTIVE;
            record->reason = "missing-hardware";
        } else {
            record->state = KERNEL_DRIVER_STATE_FAILED;
            record->reason = "init-failed";
        }
    }
    return active_count;
}

const struct kernel_driver_record *driver_find(const char *name) {
    return driver_i386_find_mutable(name);
}

const struct kernel_driver_record *driver_get(uint32_t index) {
    if (index >= g_i386_driver_count) {
        return 0;
    }
    return &g_i386_driver_records[index];
}

uint32_t driver_count(void) {
    return g_i386_driver_count;
}

static enum kernel_driver_file_state driver_i386_probe_elf(struct vfs *vfs,
                                                           struct vfs_node *node,
                                                           uint32_t file_size,
                                                           struct kernel_driver_file *file) {
    struct driver_i386_elf32_header header;
    uint32_t offset = 0u;
    int64_t read_bytes;

    if (file != 0) {
        file->elf_class = 0u;
        file->elf_data = 0u;
        file->elf_type = 0u;
        file->elf_machine = 0u;
    }
    if (vfs == 0 || node == 0 || file_size < sizeof(header)) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    read_bytes = vfs_read(vfs, node, &offset, &header, sizeof(header), VFS_READ_BLOCKING);
    if (read_bytes != (int64_t)sizeof(header)) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    if (header.ident[0] != 0x7fu ||
        header.ident[1] != 'E' ||
        header.ident[2] != 'L' ||
        header.ident[3] != 'F' ||
        header.ident[5] == 0u) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    if (file != 0) {
        file->elf_class = header.ident[4];
        file->elf_data = header.ident[5];
        file->elf_type = header.type;
        file->elf_machine = header.machine;
    }
    if (header.ident[4] != I386_DRIVER_ELF_CLASS_32 ||
        header.ident[5] != I386_DRIVER_ELF_DATA_LSB ||
        header.type != I386_DRIVER_ELF_ET_REL ||
        header.machine != I386_DRIVER_ELF_EM_386 ||
        header.ehsize != I386_DRIVER_ELF_HEADER_SIZE ||
        header.shoff == 0u ||
        header.shnum == 0u) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    return KERNEL_DRIVER_FILE_ELF_RELOC;
}

static int driver_i386_read_file_image(struct vfs *vfs,
                                       const struct kernel_driver_file *file,
                                       uint8_t **image_out,
                                       uint32_t *alloc_size_out,
                                       uint32_t *size_out) {
    struct vfs_node node;
    uint8_t *image;
    uint32_t offset = 0u;
    uint32_t alloc_size = 0u;
    uint32_t pages;
    int64_t read_bytes;

    if (image_out == 0 || alloc_size_out == 0 || size_out == 0) {
        return 0;
    }
    *image_out = 0;
    *alloc_size_out = 0u;
    *size_out = 0u;
    if (vfs == 0 || file == 0 || file->size < I386_DRIVER_ELF_HEADER_SIZE ||
        file->size > I386_DRIVER_ELF_MAX_FILE_SIZE ||
        vfs_open(vfs, file->path, 0u, &node) != 0) {
        return 0;
    }
    pages = (file->size + I386_DRIVER_ELF_PAGE_SIZE - 1u) / I386_DRIVER_ELF_PAGE_SIZE;
    image = driver_i386_alloc_pages(pages, 0, &alloc_size);
    if (image == 0 || alloc_size < file->size) {
        kprint("driver: file image allocation failed %s size=%u\n", file->path, file->size);
        return 0;
    }
    read_bytes = vfs_read(vfs, &node, &offset, image, file->size, VFS_READ_BLOCKING);
    if (read_bytes != (int64_t)file->size) {
        driver_i386_free_pages(image, pages);
        return 0;
    }
    *image_out = image;
    *alloc_size_out = alloc_size;
    *size_out = file->size;
    return 1;
}

static int driver_i386_elf_header_valid(const struct driver_i386_elf32_header *header,
                                        uint32_t file_size) {
    uint32_t section_bytes;

    if (header == 0 || file_size < sizeof(*header)) {
        return 0;
    }
    if (header->ident[0] != 0x7fu ||
        header->ident[1] != 'E' ||
        header->ident[2] != 'L' ||
        header->ident[3] != 'F' ||
        header->ident[4] != I386_DRIVER_ELF_CLASS_32 ||
        header->ident[5] != I386_DRIVER_ELF_DATA_LSB ||
        header->type != I386_DRIVER_ELF_ET_REL ||
        header->machine != I386_DRIVER_ELF_EM_386 ||
        header->ehsize != I386_DRIVER_ELF_HEADER_SIZE ||
        header->shentsize != sizeof(struct driver_i386_elf32_section) ||
        header->shnum == 0u ||
        header->shnum > I386_DRIVER_ELF_MAX_SECTIONS) {
        return 0;
    }
    section_bytes = (uint32_t)header->shentsize * (uint32_t)header->shnum;
    return driver_i386_range_valid(header->shoff, section_bytes, file_size);
}

static int driver_i386_elf_section_valid(const struct driver_i386_elf32_section *section,
                                         uint32_t file_size) {
    if (section == 0) {
        return 0;
    }
    if (section->type == I386_DRIVER_ELF_SHT_NOBITS) {
        return 1;
    }
    return driver_i386_range_valid(section->offset, section->size, file_size);
}

static int driver_i386_layout_sections(const uint8_t *image,
                                       uint32_t file_size,
                                       const struct driver_i386_elf32_header *header,
                                       uint32_t section_addrs[I386_DRIVER_ELF_MAX_SECTIONS],
                                       uint32_t *load_size_out) {
    const struct driver_i386_elf32_section *sections;
    uint32_t load_size = 1u;

    if (image == 0 || header == 0 || section_addrs == 0 || load_size_out == 0) {
        return 0;
    }
    sections = (const struct driver_i386_elf32_section *)(image + header->shoff);
    for (uint32_t i = 0u; i < header->shnum; i++) {
        uint32_t align;

        section_addrs[i] = 0u;
        if (!driver_i386_elf_section_valid(&sections[i], file_size)) {
            return 0;
        }
        if ((sections[i].flags & I386_DRIVER_ELF_SHF_ALLOC) == 0u || sections[i].size == 0u) {
            continue;
        }
        align = sections[i].addralign > 1u && sections[i].addralign <= I386_DRIVER_ELF_PAGE_SIZE
                    ? sections[i].addralign
                    : 1u;
        load_size = driver_i386_align_up(load_size, align);
        section_addrs[i] = load_size;
        load_size += sections[i].size;
    }
    *load_size_out = load_size;
    return load_size != 0u;
}

static void driver_i386_copy_sections(uint8_t *load_base,
                                      const uint8_t *image,
                                      const struct driver_i386_elf32_header *header,
                                      const uint32_t section_addrs[I386_DRIVER_ELF_MAX_SECTIONS]) {
    const struct driver_i386_elf32_section *sections =
        (const struct driver_i386_elf32_section *)(image + header->shoff);

    for (uint32_t i = 0u; i < header->shnum; i++) {
        uint8_t *dest;

        if (section_addrs[i] == 0u ||
            (sections[i].flags & I386_DRIVER_ELF_SHF_ALLOC) == 0u ||
            sections[i].size == 0u) {
            continue;
        }
        dest = load_base + section_addrs[i];
        if (sections[i].type == I386_DRIVER_ELF_SHT_NOBITS) {
            memset(dest, 0, sections[i].size);
        } else {
            memcpy(dest, image + sections[i].offset, sections[i].size);
        }
    }
}

static uint32_t driver_i386_symbol_value(const struct driver_i386_elf32_symbol *symbol,
                                         const uint8_t *image,
                                         uint32_t file_size,
                                         const struct driver_i386_elf32_section *sections,
                                         const struct driver_i386_elf32_section *sym_section,
                                         const uint32_t section_addrs[I386_DRIVER_ELF_MAX_SECTIONS],
                                         uint8_t *load_base,
                                         uint16_t section_count,
                                         int *ok_out) {
    if (ok_out != 0) {
        *ok_out = 0;
    }
    if (symbol == 0) {
        return 0u;
    }
    if (symbol->shndx == I386_DRIVER_ELF_SHN_ABS) {
        if (ok_out != 0) {
            *ok_out = 1;
        }
        return symbol->value;
    }
    if (symbol->shndx == I386_DRIVER_ELF_SHN_UNDEF) {
        uint32_t value = 0u;
        const char *name = driver_i386_elf_symbol_name(image,
                                                       file_size,
                                                       sections,
                                                       sym_section,
                                                       symbol);

        if (driver_i386_kernel_symbol_resolve(name, &value)) {
            if (ok_out != 0) {
                *ok_out = 1;
            }
            return value;
        }
        if (name != 0) {
            kprint("driver: unresolved symbol %s\n", name);
        }
        return 0u;
    }
    if (symbol->shndx >= section_count || section_addrs[symbol->shndx] == 0u) {
        return 0u;
    }
    if (ok_out != 0) {
        *ok_out = 1;
    }
    return (uint32_t)(uintptr_t)load_base + section_addrs[symbol->shndx] + symbol->value;
}

static int driver_i386_apply_relocations(uint8_t *load_base,
                                         const uint8_t *image,
                                         uint32_t file_size,
                                         const struct driver_i386_elf32_header *header,
                                         const uint32_t section_addrs[I386_DRIVER_ELF_MAX_SECTIONS]) {
    const struct driver_i386_elf32_section *sections =
        (const struct driver_i386_elf32_section *)(image + header->shoff);

    for (uint32_t i = 0u; i < header->shnum; i++) {
        const struct driver_i386_elf32_section *rel_section = &sections[i];
        const struct driver_i386_elf32_section *sym_section;
        const struct driver_i386_elf32_rel *relocs;
        const struct driver_i386_elf32_symbol *symbols;
        uint32_t reloc_count;
        uint32_t symbol_count;
        uint32_t target_index;

        if (rel_section->type != I386_DRIVER_ELF_SHT_REL) {
            continue;
        }
        if (rel_section->entsize != sizeof(struct driver_i386_elf32_rel) ||
            rel_section->link >= header->shnum ||
            rel_section->info >= header->shnum ||
            !driver_i386_elf_section_valid(rel_section, file_size)) {
            return 0;
        }
        sym_section = &sections[rel_section->link];
        if (sym_section->type != I386_DRIVER_ELF_SHT_SYMTAB ||
            sym_section->entsize != sizeof(struct driver_i386_elf32_symbol) ||
            !driver_i386_elf_section_valid(sym_section, file_size)) {
            return 0;
        }
        target_index = rel_section->info;
        if (section_addrs[target_index] == 0u) {
            continue;
        }
        relocs = (const struct driver_i386_elf32_rel *)(image + rel_section->offset);
        symbols = (const struct driver_i386_elf32_symbol *)(image + sym_section->offset);
        reloc_count = rel_section->size / rel_section->entsize;
        symbol_count = sym_section->size / sym_section->entsize;

        for (uint32_t r = 0u; r < reloc_count; r++) {
            uint32_t symbol_index = relocs[r].info >> 8;
            uint32_t type = relocs[r].info & 0xffu;
            uint8_t *place;
            uint32_t value;
            uint32_t addend;
            int symbol_ok = 0;

            if (symbol_index >= symbol_count ||
                relocs[r].offset > sections[target_index].size ||
                4u > sections[target_index].size - relocs[r].offset) {
                kprint("driver: bad i386 reloc sec=%u index=%u type=%u sym=%u\n",
                       i,
                       r,
                       type,
                       symbol_index);
                return 0;
            }
            place = load_base + section_addrs[target_index] + relocs[r].offset;
            addend = *((uint32_t *)place);
            value = driver_i386_symbol_value(&symbols[symbol_index],
                                             image,
                                             file_size,
                                             sections,
                                             sym_section,
                                             section_addrs,
                                             load_base,
                                             header->shnum,
                                             &symbol_ok);
            if (!symbol_ok) {
                return 0;
            }
            if (type == I386_DRIVER_ELF_R_386_32) {
                *((uint32_t *)place) = value + addend;
            } else if (type == I386_DRIVER_ELF_R_386_PC32) {
                *((uint32_t *)place) = value + addend - (uint32_t)(uintptr_t)place;
            } else {
                kprint("driver: unsupported i386 reloc type=%u\n", type);
                return 0;
            }
        }
    }
    return 1;
}

static const char *driver_i386_elf_symbol_name(const uint8_t *image,
                                               uint32_t file_size,
                                               const struct driver_i386_elf32_section *sections,
                                               const struct driver_i386_elf32_section *sym_section,
                                               const struct driver_i386_elf32_symbol *symbol) {
    const struct driver_i386_elf32_section *str_section;
    const char *name;
    uint32_t remaining;

    if (image == 0 || sections == 0 || sym_section == 0 || symbol == 0 ||
        sym_section->link >= I386_DRIVER_ELF_MAX_SECTIONS) {
        return 0;
    }
    str_section = &sections[sym_section->link];
    if (str_section->type != I386_DRIVER_ELF_SHT_STRTAB ||
        !driver_i386_elf_section_valid(str_section, file_size) ||
        symbol->name >= str_section->size) {
        return 0;
    }
    name = (const char *)(image + str_section->offset + symbol->name);
    remaining = str_section->size - symbol->name;
    for (uint32_t i = 0u; i < remaining; i++) {
        if (name[i] == '\0') {
            return name;
        }
    }
    return 0;
}

static int driver_i386_kernel_symbol_resolve(const char *name, uint32_t *value_out) {
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

static const struct kernel_driver *driver_i386_find_driver_symbol(
    uint8_t *load_base,
    const uint8_t *image,
    uint32_t file_size,
    const struct driver_i386_elf32_header *header,
    const uint32_t section_addrs[I386_DRIVER_ELF_MAX_SECTIONS]) {
    const struct driver_i386_elf32_section *sections =
        (const struct driver_i386_elf32_section *)(image + header->shoff);

    for (uint32_t i = 0u; i < header->shnum; i++) {
        const struct driver_i386_elf32_section *sym_section = &sections[i];
        const struct driver_i386_elf32_symbol *symbols;
        uint32_t symbol_count;

        if (sym_section->type != I386_DRIVER_ELF_SHT_SYMTAB ||
            sym_section->entsize != sizeof(struct driver_i386_elf32_symbol) ||
            sym_section->link >= header->shnum ||
            !driver_i386_elf_section_valid(sym_section, file_size)) {
            continue;
        }
        symbols = (const struct driver_i386_elf32_symbol *)(image + sym_section->offset);
        symbol_count = sym_section->size / sym_section->entsize;
        for (uint32_t s = 0u; s < symbol_count; s++) {
            const char *name = driver_i386_elf_symbol_name(image,
                                                           file_size,
                                                           sections,
                                                           sym_section,
                                                           &symbols[s]);
            int symbol_ok = 0;
            uint32_t symbol_value;

            if (name == 0 || !streq(name, "kernel_driver")) {
                continue;
            }
            symbol_value = driver_i386_symbol_value(&symbols[s],
                                                    image,
                                                    file_size,
                                                    sections,
                                                    sym_section,
                                                    section_addrs,
                                                    load_base,
                                                    header->shnum,
                                                    &symbol_ok);
            if (!symbol_ok) {
                return 0;
            }
            return (const struct kernel_driver *)(uintptr_t)symbol_value;
        }
    }
    return 0;
}

static int driver_i386_register_source(const struct kernel_driver *driver,
                                       const char *source,
                                       const char *path) {
    struct kernel_driver_record *record;

    if (driver == 0 ||
        !driver_i386_name_valid(driver->name) ||
        driver->init == 0 ||
        g_i386_driver_count >= I386_DRIVER_MAX_COUNT ||
        driver_i386_find_mutable(driver->name) != 0) {
        return 0;
    }
    record = &g_i386_driver_records[g_i386_driver_count++];
    record->driver = driver;
    record->state = KERNEL_DRIVER_STATE_REGISTERED;
    record->init_result = 0;
    record->source = source != 0 ? source : "ramdisk-i386";
    record->path = path != 0 ? path : "-";
    record->reason = "registered";
    return 1;
}

static int driver_i386_load_file(struct vfs *vfs, struct kernel_driver_file *file) {
    uint8_t *image = 0;
    uint8_t *load_base = 0;
    uint32_t file_size = 0u;
    uint32_t image_alloc_size = 0u;
    uint32_t image_pages;
    uint32_t load_size = 0u;
    uint32_t load_alloc_size = 0u;
    uint32_t load_pages;
    struct driver_i386_elf32_header *header;
    uint32_t section_addrs[I386_DRIVER_ELF_MAX_SECTIONS];
    const struct kernel_driver *driver;

    if (file == 0 || file->state != KERNEL_DRIVER_FILE_ELF_RELOC) {
        return 0;
    }
    if (!driver_i386_read_file_image(vfs, file, &image, &image_alloc_size, &file_size)) {
        return 0;
    }
    image_pages = image_alloc_size / I386_DRIVER_ELF_PAGE_SIZE;
    header = (struct driver_i386_elf32_header *)image;
    if (!driver_i386_elf_header_valid(header, file_size) ||
        !driver_i386_layout_sections(image, file_size, header, section_addrs, &load_size)) {
        kprint("driver: ELF32 layout failed %s\n", file->path);
        driver_i386_free_pages(image, image_pages);
        return 0;
    }
    load_pages = (load_size + I386_DRIVER_ELF_PAGE_SIZE - 1u) / I386_DRIVER_ELF_PAGE_SIZE;
    load_base = driver_i386_alloc_pages(load_pages, 0, &load_alloc_size);
    if (load_base == 0 || load_alloc_size < load_size) {
        kprint("driver: i386 load memory failed %s size=%u\n", file->path, load_size);
        driver_i386_free_pages(image, image_pages);
        return 0;
    }
    driver_i386_copy_sections(load_base, image, header, section_addrs);
    if (!driver_i386_apply_relocations(load_base, image, file_size, header, section_addrs)) {
        driver_i386_free_pages(image, image_pages);
        driver_i386_free_pages(load_base, load_pages);
        return 0;
    }
    driver = driver_i386_find_driver_symbol(load_base, image, file_size, header, section_addrs);
    if (driver == 0 || !driver_i386_register_source(driver, "ramdisk-i386", file->path)) {
        kprint("driver: register failed %s\n", file->path);
        driver_i386_free_pages(image, image_pages);
        driver_i386_free_pages(load_base, load_pages);
        return 0;
    }
    driver_i386_free_pages(image, image_pages);
    kprint("driver: loaded %s as %s\n", file->path, driver->name);
    return 1;
}

uint32_t driver_discover_root(struct vfs *vfs, const char *directory) {
    struct vfs_node dir_node;
    struct vfs_node file_node;
    struct vfs_dirent entry;
    uint32_t index = 0u;
    uint32_t found = 0u;
    char path[NOS_PATH_BUFFER_SIZE];

    if (vfs == 0 || directory == 0 || directory[0] == '\0') {
        return 0u;
    }
    if (vfs_opendir(vfs, directory, &dir_node) != 0) {
        kprint("driver: directory not found %s\n", directory);
        return 0u;
    }
    while (vfs_readdir(vfs, &dir_node, &index, &entry) == 1) {
        if (!driver_i386_name_has_drv_suffix(entry.name)) {
            continue;
        }
        if (!driver_i386_join_path(path, sizeof(path), directory, entry.name) ||
            driver_i386_file_exists(path)) {
            continue;
        }
        if (vfs_open(vfs, path, 0u, &file_node) != 0 || file_node.kind != VFS_NODE_FILE) {
            continue;
        }
        if (g_i386_driver_file_count >= I386_DRIVER_FILE_MAX_COUNT) {
            kprint("driver: .DRV table full while scanning %s\n", directory);
            break;
        }
        driver_i386_copy_text(g_i386_driver_files[g_i386_driver_file_count].name,
                              entry.name,
                              sizeof(g_i386_driver_files[g_i386_driver_file_count].name));
        driver_i386_copy_text(g_i386_driver_files[g_i386_driver_file_count].path,
                              path,
                              sizeof(g_i386_driver_files[g_i386_driver_file_count].path));
        g_i386_driver_files[g_i386_driver_file_count].size = entry.size;
        g_i386_driver_files[g_i386_driver_file_count].state =
            driver_i386_probe_elf(vfs,
                                  &file_node,
                                  entry.size,
                                  &g_i386_driver_files[g_i386_driver_file_count]);
        if (g_i386_driver_files[g_i386_driver_file_count].state ==
            KERNEL_DRIVER_FILE_ELF_RELOC) {
            g_i386_driver_files[g_i386_driver_file_count].reason = "probe-ok";
        } else {
            g_i386_driver_files[g_i386_driver_file_count].reason = "unsupported-elf";
        }
        kprint("driver: discovered %s size=%u state=%u\n",
               g_i386_driver_files[g_i386_driver_file_count].path,
               g_i386_driver_files[g_i386_driver_file_count].size,
               (uint32_t)g_i386_driver_files[g_i386_driver_file_count].state);
        g_i386_driver_file_count++;
        found++;
    }
    if (found == 0u) {
        kprint("driver: no .DRV files in %s\n", directory);
    }
    return found;
}

uint32_t driver_load_all(struct vfs *vfs) {
    uint32_t loaded = 0u;

    if (vfs == 0) {
        return 0u;
    }
    for (uint32_t i = 0u; i < g_i386_driver_file_count; i++) {
        if (g_i386_driver_files[i].state != KERNEL_DRIVER_FILE_ELF_RELOC) {
            continue;
        }
        if (driver_i386_load_file(vfs, &g_i386_driver_files[i])) {
            g_i386_driver_files[i].state = KERNEL_DRIVER_FILE_LOADED;
            g_i386_driver_files[i].reason = "loaded";
            loaded++;
        } else {
            g_i386_driver_files[i].state = KERNEL_DRIVER_FILE_LOAD_FAILED;
            g_i386_driver_files[i].reason = "load-failed";
            kprint("driver: load failed %s\n", g_i386_driver_files[i].path);
        }
    }
    return loaded;
}

const struct kernel_driver_file *driver_get_file(uint32_t index) {
    if (index >= g_i386_driver_file_count) {
        return 0;
    }
    return &g_i386_driver_files[index];
}

uint32_t driver_file_count(void) {
    return g_i386_driver_file_count;
}
