#include "kernel/public/driver/driver.h"
#include "kernel/public/driver/driver_module.h"
#include "arch/x86/io.h"
#include "drivers/bus/pci.h"
#include "fs/vfs.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/pmm.h"
#include "lib/string.h"

#define DRIVER_MAX_COUNT 32u
#define DRIVER_FILE_MAX_COUNT 32u
#define DRIVER_ELF_MAX_SECTIONS 64u
#define DRIVER_ELF_MAX_FILE_SIZE (1024u * 1024u)
#define DRIVER_ELF_PAGE_SIZE 4096u

enum {
    DRIVER_ELF_IDENT_SIZE = 16u,
    DRIVER_ELF_HEADER_SIZE = 64u,
    DRIVER_ELF_CLASS_64 = 2u,
    DRIVER_ELF_DATA_LSB = 1u,
    DRIVER_ELF_ET_REL = 1u,
    DRIVER_ELF_EM_X86_64 = 62u,
    DRIVER_ELF_SHT_SYMTAB = 2u,
    DRIVER_ELF_SHT_STRTAB = 3u,
    DRIVER_ELF_SHT_RELA = 4u,
    DRIVER_ELF_SHT_NOBITS = 8u,
    DRIVER_ELF_SHF_ALLOC = 0x2u,
    DRIVER_ELF_SHN_UNDEF = 0u,
    DRIVER_ELF_SHN_ABS = 0xfff1u,
    DRIVER_ELF_STB_LOCAL = 0u,
    DRIVER_ELF_R_X86_64_64 = 1u,
    DRIVER_ELF_R_X86_64_PC32 = 2u,
    DRIVER_ELF_R_X86_64_32 = 10u,
    DRIVER_ELF_R_X86_64_32S = 11u,
    DRIVER_ELF_R_X86_64_PLT32 = 4u
};

struct driver_elf64_header {
    uint8_t ident[DRIVER_ELF_IDENT_SIZE];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct driver_elf64_section {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
} __attribute__((packed));

struct driver_elf64_symbol {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
    uint64_t value;
    uint64_t size;
} __attribute__((packed));

struct driver_elf64_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} __attribute__((packed));

struct driver_kernel_symbol {
    const char *name;
    uint64_t value;
};

static const struct driver_kernel_symbol g_driver_kernel_symbols[] = {
    { "driver_log", (uint64_t)(uintptr_t)kprint },
    { "driver_memcpy", (uint64_t)(uintptr_t)memcpy },
    { "driver_memmove", (uint64_t)(uintptr_t)memmove },
    { "driver_memset", (uint64_t)(uintptr_t)memset },
    { "driver_io_in8", (uint64_t)(uintptr_t)driver_io_in8 },
    { "driver_io_in16", (uint64_t)(uintptr_t)driver_io_in16 },
    { "driver_io_in32", (uint64_t)(uintptr_t)driver_io_in32 },
    { "driver_io_out8", (uint64_t)(uintptr_t)driver_io_out8 },
    { "driver_io_out16", (uint64_t)(uintptr_t)driver_io_out16 },
    { "driver_io_out32", (uint64_t)(uintptr_t)driver_io_out32 },
    { "driver_pci_find_by_class", (uint64_t)(uintptr_t)driver_pci_find_by_class },
    { "driver_pci_find_by_id", (uint64_t)(uintptr_t)driver_pci_find_by_id },
    { "driver_pci_read8", (uint64_t)(uintptr_t)driver_pci_read8 },
    { "driver_pci_read16", (uint64_t)(uintptr_t)driver_pci_read16 },
    { "driver_pci_read32", (uint64_t)(uintptr_t)driver_pci_read32 },
    { "driver_pci_write8", (uint64_t)(uintptr_t)driver_pci_write8 },
    { "driver_pci_write16", (uint64_t)(uintptr_t)driver_pci_write16 },
    { "driver_pci_write32", (uint64_t)(uintptr_t)driver_pci_write32 },
    { "driver_starts_with", (uint64_t)(uintptr_t)starts_with },
    { "driver_streq", (uint64_t)(uintptr_t)streq },
    { "driver_str_len", (uint64_t)(uintptr_t)str_len },
    { NULL, 0 }
};

static struct kernel_driver_record g_driver_records[DRIVER_MAX_COUNT];
static struct kernel_driver_file g_driver_files[DRIVER_FILE_MAX_COUNT];
static uint32_t g_driver_count;
static uint32_t g_driver_file_count;

static const char *driver_elf_symbol_name_local(const uint8_t *image,
                                                uint32_t file_size,
                                                const struct driver_elf64_section *sections,
                                                const struct driver_elf64_section *sym_section,
                                                const struct driver_elf64_symbol *symbol);
static int driver_kernel_symbol_resolve_local(const char *name, uint64_t *value_out);

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

static void driver_pci_copy_device_local(struct driver_pci_device *out,
                                         const struct pci_device_info *pci) {
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

    if (out == NULL ||
        !pci_find_device_by_class_at(class_code, subclass, index, &pci)) {
        return 0;
    }
    driver_pci_copy_device_local(out, &pci);
    return 1;
}

int driver_pci_find_by_id(uint16_t vendor_id,
                          uint16_t device_id,
                          uint32_t index,
                          struct driver_pci_device *out) {
    struct pci_device_info pci;

    if (out == NULL ||
        !pci_find_device_at(vendor_id, device_id, index, &pci)) {
        return 0;
    }
    driver_pci_copy_device_local(out, &pci);
    return 1;
}

uint8_t driver_pci_read8(const struct driver_pci_device *dev, uint8_t offset) {
    if (dev == NULL) {
        return 0xffu;
    }
    return pci_config_read8(dev->bus, dev->slot, dev->function, offset);
}

uint16_t driver_pci_read16(const struct driver_pci_device *dev, uint8_t offset) {
    if (dev == NULL) {
        return 0xffffu;
    }
    return pci_config_read16(dev->bus, dev->slot, dev->function, offset);
}

uint32_t driver_pci_read32(const struct driver_pci_device *dev, uint8_t offset) {
    if (dev == NULL) {
        return 0xffffffffu;
    }
    return pci_config_read32(dev->bus, dev->slot, dev->function, offset);
}

void driver_pci_write8(const struct driver_pci_device *dev, uint8_t offset, uint8_t value) {
    if (dev == NULL) {
        return;
    }
    pci_config_write8(dev->bus, dev->slot, dev->function, offset, value);
}

void driver_pci_write16(const struct driver_pci_device *dev, uint8_t offset, uint16_t value) {
    if (dev == NULL) {
        return;
    }
    pci_config_write16(dev->bus, dev->slot, dev->function, offset, value);
}

void driver_pci_write32(const struct driver_pci_device *dev, uint8_t offset, uint32_t value) {
    if (dev == NULL) {
        return;
    }
    pci_config_write32(dev->bus, dev->slot, dev->function, offset, value);
}

static char driver_ascii_upper_local(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static int driver_name_has_drv_suffix_local(const char *name) {
    uint32_t len;

    if (name == NULL) {
        return 0;
    }
    len = str_len(name);
    if (len < 5u) {
        return 0;
    }
    return driver_ascii_upper_local(name[len - 4u]) == '.' &&
           driver_ascii_upper_local(name[len - 3u]) == 'D' &&
           driver_ascii_upper_local(name[len - 2u]) == 'R' &&
           driver_ascii_upper_local(name[len - 1u]) == 'V';
}

static void driver_copy_text_local(char *dst, const char *src, uint32_t dst_size) {
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

static int driver_join_path_local(char *out,
                                  uint32_t out_size,
                                  const char *directory,
                                  const char *name) {
    uint32_t pos = 0;
    uint32_t i = 0;

    if (out == NULL || out_size < 2u || directory == NULL ||
        name == NULL || name[0] == '\0') {
        return 0;
    }
    while (directory[i] != '\0') {
        if (pos + 1u >= out_size) {
            return 0;
        }
        out[pos++] = directory[i++];
    }
    if (pos != 0u && out[pos - 1u] != '/') {
        if (pos + 1u >= out_size) {
            return 0;
        }
        out[pos++] = '/';
    }
    i = 0;
    while (name[i] != '\0') {
        if (pos + 1u >= out_size) {
            return 0;
        }
        out[pos++] = name[i++];
    }
    out[pos] = '\0';
    return 1;
}

static int driver_file_exists_local(const char *path) {
    uint32_t i;

    if (path == NULL) {
        return 0;
    }
    for (i = 0; i < g_driver_file_count; i++) {
        if (streq(g_driver_files[i].path, path)) {
            return 1;
        }
    }
    return 0;
}

static int driver_name_valid_local(const char *name) {
    uint32_t len = 0;

    if (name == NULL) {
        return 0;
    }
    while (name[len] != '\0') {
        len++;
        if (len > KERNEL_DRIVER_NAME_MAX) {
            return 0;
        }
    }
    return len != 0;
}

static uint32_t driver_align_up_local(uint32_t value, uint32_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static int driver_range_valid_local(uint32_t offset, uint32_t size, uint32_t limit) {
    if (offset > limit) {
        return 0;
    }
    if (size > limit - offset) {
        return 0;
    }
    return 1;
}

static void *driver_alloc_pages_local(uint32_t size, uint32_t *alloc_size_out) {
    uint32_t page_count;
    uint64_t phys;
    void *ptr;

    if (alloc_size_out != NULL) {
        *alloc_size_out = 0;
    }
    if (size == 0u) {
        return NULL;
    }
    page_count = driver_align_up_local(size, DRIVER_ELF_PAGE_SIZE) / DRIVER_ELF_PAGE_SIZE;
    if (page_count == 1u) {
        phys = pmm_alloc_page();
    } else {
        phys = pmm_alloc_contiguous(page_count);
    }
    if (phys == 0u) {
        return NULL;
    }
    ptr = (void *)(uintptr_t)phys;
    memset(ptr, 0, page_count * DRIVER_ELF_PAGE_SIZE);
    if (alloc_size_out != NULL) {
        *alloc_size_out = page_count * DRIVER_ELF_PAGE_SIZE;
    }
    return ptr;
}

static void driver_free_pages_local(void *ptr, uint32_t alloc_size) {
    uint64_t phys = (uint64_t)(uintptr_t)ptr;
    uint32_t page_count;

    if (ptr == NULL || alloc_size == 0u) {
        return;
    }
    page_count = driver_align_up_local(alloc_size, DRIVER_ELF_PAGE_SIZE) / DRIVER_ELF_PAGE_SIZE;
    for (uint32_t i = 0; i < page_count; i++) {
        (void)pmm_free_page(phys + (uint64_t)i * DRIVER_ELF_PAGE_SIZE);
    }
}

static struct kernel_driver_record *driver_find_mutable_local(const char *name) {
    uint32_t i;

    if (!driver_name_valid_local(name)) {
        return NULL;
    }
    for (i = 0; i < g_driver_count; i++) {
        if (g_driver_records[i].driver != NULL &&
            streq(g_driver_records[i].driver->name, name)) {
            return &g_driver_records[i];
        }
    }
    return NULL;
}

void driver_manager_init(void) {
    uint32_t i;

    for (i = 0; i < DRIVER_MAX_COUNT; i++) {
        g_driver_records[i].driver = NULL;
        g_driver_records[i].state = KERNEL_DRIVER_STATE_EMPTY;
        g_driver_records[i].init_result = 0;
        g_driver_records[i].source = "builtin";
        g_driver_records[i].path = "-";
    }
    for (i = 0; i < DRIVER_FILE_MAX_COUNT; i++) {
        g_driver_files[i].name[0] = '\0';
        g_driver_files[i].path[0] = '\0';
        g_driver_files[i].size = 0;
        g_driver_files[i].state = KERNEL_DRIVER_FILE_DISCOVERED;
    }
    g_driver_count = 0;
    g_driver_file_count = 0;
}

static int driver_register_source_local(const struct kernel_driver *driver,
                                        const char *source,
                                        const char *path) {
    struct kernel_driver_record *record;

    if (driver == NULL || !driver_name_valid_local(driver->name) || driver->init == NULL) {
        return 0;
    }
    if (g_driver_count >= DRIVER_MAX_COUNT) {
        return 0;
    }
    record = driver_find_mutable_local(driver->name);
    if (record != NULL) {
        return 0;
    }

    g_driver_records[g_driver_count].driver = driver;
    g_driver_records[g_driver_count].state = KERNEL_DRIVER_STATE_REGISTERED;
    g_driver_records[g_driver_count].init_result = 0;
    g_driver_records[g_driver_count].source = source != NULL ? source : "builtin";
    g_driver_records[g_driver_count].path = path != NULL ? path : "-";
    g_driver_count++;
    return 1;
}

int driver_register(const struct kernel_driver *driver) {
    return driver_register_source_local(driver, "builtin", "-");
}

uint32_t driver_init_all(void) {
    uint32_t active_count = 0;
    uint32_t i;
    int result;

    for (i = 0; i < g_driver_count; i++) {
        if (g_driver_records[i].state != KERNEL_DRIVER_STATE_REGISTERED ||
            g_driver_records[i].driver == NULL ||
            g_driver_records[i].driver->init == NULL) {
            continue;
        }

        result = g_driver_records[i].driver->init();
        g_driver_records[i].init_result = result;
        if (result > 0) {
            g_driver_records[i].state = KERNEL_DRIVER_STATE_ACTIVE;
            active_count++;
        } else if (result == 0) {
            g_driver_records[i].state = KERNEL_DRIVER_STATE_INACTIVE;
        } else {
            g_driver_records[i].state = KERNEL_DRIVER_STATE_FAILED;
        }
    }
    return active_count;
}

const struct kernel_driver_record *driver_find(const char *name) {
    return driver_find_mutable_local(name);
}

const struct kernel_driver_record *driver_get(uint32_t index) {
    if (index >= g_driver_count) {
        return NULL;
    }
    return &g_driver_records[index];
}

uint32_t driver_count(void) {
    return g_driver_count;
}

static enum kernel_driver_file_state driver_probe_elf_local(struct vfs *vfs,
                                                            struct vfs_node *node) {
    struct driver_elf64_header header;
    uint32_t offset = 0;
    int64_t read_bytes;

    if (vfs == NULL || node == NULL || vfs_node_file_size(node) < DRIVER_ELF_HEADER_SIZE) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    read_bytes = vfs_read(vfs,
                          node,
                          &offset,
                          &header,
                          sizeof(header),
                          VFS_READ_BLOCKING);
    if (read_bytes != (int64_t)sizeof(header)) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    if (header.ident[0] != 0x7fu ||
        header.ident[1] != 'E' ||
        header.ident[2] != 'L' ||
        header.ident[3] != 'F') {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    if (header.ident[4] != DRIVER_ELF_CLASS_64 ||
        header.ident[5] != DRIVER_ELF_DATA_LSB) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    if (header.type != DRIVER_ELF_ET_REL || header.machine != DRIVER_ELF_EM_X86_64) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    if (header.ehsize != DRIVER_ELF_HEADER_SIZE || header.shoff == 0u || header.shnum == 0u) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    return KERNEL_DRIVER_FILE_ELF_RELOC;
}

static int driver_read_file_image_local(struct vfs *vfs,
                                        const char *path,
                                        uint8_t **image_out,
                                        uint32_t *alloc_size_out,
                                        uint32_t *size_out) {
    struct vfs_node node;
    uint8_t *image;
    uint32_t image_alloc_size;
    uint32_t offset = 0;
    uint32_t size;
    int64_t read_bytes;

    if (image_out == NULL || alloc_size_out == NULL || size_out == NULL) {
        return 0;
    }
    *image_out = NULL;
    *alloc_size_out = 0;
    *size_out = 0;
    if (vfs == NULL || path == NULL || vfs_open(vfs, path, 0, &node) != 0) {
        return 0;
    }
    size = vfs_node_file_size(&node);
    if (size < DRIVER_ELF_HEADER_SIZE || size > DRIVER_ELF_MAX_FILE_SIZE) {
        return 0;
    }
    image = driver_alloc_pages_local(size, &image_alloc_size);
    if (image == NULL || image_alloc_size < size) {
        kprint("driver: file image allocation failed %s size=%u\n", path, size);
        return 0;
    }
    read_bytes = vfs_read(vfs, &node, &offset, image, size, VFS_READ_BLOCKING);
    if (read_bytes != (int64_t)size) {
        driver_free_pages_local(image, image_alloc_size);
        return 0;
    }
    *image_out = image;
    *alloc_size_out = image_alloc_size;
    *size_out = size;
    return 1;
}

static int driver_elf_header_valid_local(const struct driver_elf64_header *header,
                                         uint32_t file_size) {
    uint32_t section_bytes;

    if (header == NULL || file_size < sizeof(*header)) {
        return 0;
    }
    if (header->ident[0] != 0x7fu ||
        header->ident[1] != 'E' ||
        header->ident[2] != 'L' ||
        header->ident[3] != 'F') {
        return 0;
    }
    if (header->ident[4] != DRIVER_ELF_CLASS_64 ||
        header->ident[5] != DRIVER_ELF_DATA_LSB ||
        header->type != DRIVER_ELF_ET_REL ||
        header->machine != DRIVER_ELF_EM_X86_64) {
        return 0;
    }
    if (header->ehsize != DRIVER_ELF_HEADER_SIZE ||
        header->shentsize != sizeof(struct driver_elf64_section) ||
        header->shnum == 0u ||
        header->shnum > DRIVER_ELF_MAX_SECTIONS ||
        header->shoff > 0xffffffffu) {
        return 0;
    }
    section_bytes = (uint32_t)header->shentsize * (uint32_t)header->shnum;
    return driver_range_valid_local((uint32_t)header->shoff, section_bytes, file_size);
}

static int driver_elf_section_valid_local(const struct driver_elf64_section *section,
                                          uint32_t file_size) {
    if (section == NULL || section->offset > 0xffffffffu || section->size > 0xffffffffu) {
        return 0;
    }
    if (section->type == DRIVER_ELF_SHT_NOBITS) {
        return 1;
    }
    return driver_range_valid_local((uint32_t)section->offset,
                                    (uint32_t)section->size,
                                    file_size);
}

static int driver_elf_layout_sections_local(const uint8_t *image,
                                            uint32_t file_size,
                                            const struct driver_elf64_header *header,
                                            uint64_t section_addrs[DRIVER_ELF_MAX_SECTIONS],
                                            uint32_t *load_size_out) {
    const struct driver_elf64_section *sections;
    uint32_t load_size = 1;

    if (image == NULL || header == NULL || section_addrs == NULL || load_size_out == NULL) {
        return 0;
    }
    sections = (const struct driver_elf64_section *)(image + (uint32_t)header->shoff);
    for (uint32_t i = 0; i < header->shnum; i++) {
        uint32_t align;

        section_addrs[i] = 0;
        if (!driver_elf_section_valid_local(&sections[i], file_size)) {
            return 0;
        }
        if ((sections[i].flags & DRIVER_ELF_SHF_ALLOC) == 0u || sections[i].size == 0u) {
            continue;
        }
        if (sections[i].size > 0xffffffffu) {
            return 0;
        }
        align = sections[i].addralign > 1u && sections[i].addralign <= 4096u
                    ? (uint32_t)sections[i].addralign
                    : 1u;
        load_size = driver_align_up_local(load_size, align);
        section_addrs[i] = load_size;
        load_size += (uint32_t)sections[i].size;
    }
    *load_size_out = load_size;
    return load_size != 0u;
}

static void driver_elf_copy_sections_local(uint8_t *load_base,
                                           const uint8_t *image,
                                           const struct driver_elf64_header *header,
                                           const uint64_t section_addrs[DRIVER_ELF_MAX_SECTIONS]) {
    const struct driver_elf64_section *sections =
        (const struct driver_elf64_section *)(image + (uint32_t)header->shoff);

    for (uint32_t i = 0; i < header->shnum; i++) {
        uint8_t *dest;

        if (section_addrs[i] == 0u ||
            (sections[i].flags & DRIVER_ELF_SHF_ALLOC) == 0u ||
            sections[i].size == 0u) {
            continue;
        }
        dest = load_base + section_addrs[i];
        if (sections[i].type == DRIVER_ELF_SHT_NOBITS) {
            memset(dest, 0, (uint32_t)sections[i].size);
        } else {
            memcpy(dest, image + (uint32_t)sections[i].offset, (uint32_t)sections[i].size);
        }
    }
}

static uint64_t driver_elf_symbol_value_local(const struct driver_elf64_symbol *symbol,
                                              const uint8_t *image,
                                              uint32_t file_size,
                                              const struct driver_elf64_section *sections,
                                              const struct driver_elf64_section *sym_section,
                                              const uint64_t section_addrs[DRIVER_ELF_MAX_SECTIONS],
                                              uint8_t *load_base,
                                              uint16_t section_count,
                                              int *ok_out) {
    if (ok_out != NULL) {
        *ok_out = 0;
    }
    if (symbol == NULL) {
        return 0;
    }
    if (symbol->shndx == DRIVER_ELF_SHN_ABS) {
        if (ok_out != NULL) {
            *ok_out = 1;
        }
        return symbol->value;
    }
    if (symbol->shndx == DRIVER_ELF_SHN_UNDEF) {
        uint64_t value = 0;
        const char *name = driver_elf_symbol_name_local(image,
                                                        file_size,
                                                        sections,
                                                        sym_section,
                                                        symbol);

        if (driver_kernel_symbol_resolve_local(name, &value)) {
            if (ok_out != NULL) {
                *ok_out = 1;
            }
            return value;
        }
        if (name != NULL) {
            kprint("driver: unresolved symbol %s\n", name);
        }
        return 0;
    }
    if (symbol->shndx >= section_count) {
        return 0;
    }
    if (section_addrs[symbol->shndx] == 0u) {
        return 0;
    }
    if (ok_out != NULL) {
        *ok_out = 1;
    }
    return (uint64_t)(uintptr_t)load_base + section_addrs[symbol->shndx] + symbol->value;
}

static int driver_elf_write_reloc_local(uint8_t *place,
                                        uint32_t type,
                                        uint64_t symbol_value,
                                        int64_t addend) {
    int64_t value = (int64_t)symbol_value + addend;
    int64_t pc_value = value - (int64_t)(uintptr_t)place;

    switch (type) {
        case DRIVER_ELF_R_X86_64_64:
            *((uint64_t *)place) = (uint64_t)value;
            return 1;
        case DRIVER_ELF_R_X86_64_PC32:
        case DRIVER_ELF_R_X86_64_PLT32:
            if (pc_value < -2147483648ll || pc_value > 2147483647ll) {
                kprint("driver: reloc pc32 out of range type=%u place=%lx target=%lx\n",
                       type,
                       (uint64_t)(uintptr_t)place,
                       (uint64_t)value);
                return 0;
            }
            *((uint32_t *)place) = (uint32_t)pc_value;
            return 1;
        case DRIVER_ELF_R_X86_64_32:
            if (value < 0 || value > 0xffffffffll) {
                kprint("driver: reloc 32 out of range target=%lx\n", (uint64_t)value);
                return 0;
            }
            *((uint32_t *)place) = (uint32_t)value;
            return 1;
        case DRIVER_ELF_R_X86_64_32S:
            if (value < -2147483648ll || value > 2147483647ll) {
                kprint("driver: reloc 32s out of range target=%lx\n", (uint64_t)value);
                return 0;
            }
            *((uint32_t *)place) = (uint32_t)value;
            return 1;
        default:
            kprint("driver: unsupported reloc type=%u\n", type);
            return 0;
    }
}

static uint32_t driver_elf_reloc_width_local(uint32_t type) {
    switch (type) {
        case DRIVER_ELF_R_X86_64_64:
            return 8u;
        case DRIVER_ELF_R_X86_64_PC32:
        case DRIVER_ELF_R_X86_64_PLT32:
        case DRIVER_ELF_R_X86_64_32:
        case DRIVER_ELF_R_X86_64_32S:
            return 4u;
        default:
            return 0;
    }
}

static int driver_elf_apply_relocations_local(uint8_t *load_base,
                                              const uint8_t *image,
                                              uint32_t file_size,
                                              const struct driver_elf64_header *header,
                                              const uint64_t section_addrs[DRIVER_ELF_MAX_SECTIONS]) {
    const struct driver_elf64_section *sections =
        (const struct driver_elf64_section *)(image + (uint32_t)header->shoff);

    for (uint32_t i = 0; i < header->shnum; i++) {
        const struct driver_elf64_section *rela_section = &sections[i];
        const struct driver_elf64_section *sym_section;
        const struct driver_elf64_rela *relocs;
        const struct driver_elf64_symbol *symbols;
        uint32_t reloc_count;
        uint32_t symbol_count;
        uint32_t target_index;

        if (rela_section->type != DRIVER_ELF_SHT_RELA) {
            continue;
        }
        if (rela_section->entsize != sizeof(struct driver_elf64_rela) ||
            rela_section->link >= header->shnum ||
            rela_section->info >= header->shnum ||
            !driver_elf_section_valid_local(rela_section, file_size)) {
            return 0;
        }
        sym_section = &sections[rela_section->link];
        if (sym_section->type != DRIVER_ELF_SHT_SYMTAB ||
            sym_section->entsize != sizeof(struct driver_elf64_symbol) ||
            !driver_elf_section_valid_local(sym_section, file_size)) {
            return 0;
        }
        target_index = rela_section->info;
        if (section_addrs[target_index] == 0u) {
            continue;
        }
        relocs = (const struct driver_elf64_rela *)(image + (uint32_t)rela_section->offset);
        symbols = (const struct driver_elf64_symbol *)(image + (uint32_t)sym_section->offset);
        reloc_count = (uint32_t)(rela_section->size / rela_section->entsize);
        symbol_count = (uint32_t)(sym_section->size / sym_section->entsize);

        for (uint32_t r = 0; r < reloc_count; r++) {
            uint32_t symbol_index = (uint32_t)(relocs[r].info >> 32);
            uint32_t type = (uint32_t)relocs[r].info;
            uint32_t width = driver_elf_reloc_width_local(type);
            uint64_t symbol_value;
            uint8_t *place;
            int symbol_ok = 0;

            if (symbol_index >= symbol_count ||
                width == 0u ||
                relocs[r].offset > sections[target_index].size ||
                width > sections[target_index].size - relocs[r].offset) {
                kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n",
                       i,
                       r,
                       type,
                       symbol_index);
                return 0;
            }
            symbol_value = driver_elf_symbol_value_local(&symbols[symbol_index],
                                                         image,
                                                         file_size,
                                                         sections,
                                                         sym_section,
                                                         section_addrs,
                                                         load_base,
                                                         header->shnum,
                                                         &symbol_ok);
            if (!symbol_ok) {
                kprint("driver: reloc symbol failed sec=%u index=%u sym=%u\n",
                       i,
                       r,
                       symbol_index);
                return 0;
            }
            place = load_base + section_addrs[target_index] + (uint32_t)relocs[r].offset;
            if (!driver_elf_write_reloc_local(place, type, symbol_value, relocs[r].addend)) {
                kprint("driver: reloc write failed sec=%u index=%u type=%u\n", i, r, type);
                return 0;
            }
        }
    }
    return 1;
}

static const char *driver_elf_symbol_name_local(const uint8_t *image,
                                                uint32_t file_size,
                                                const struct driver_elf64_section *sections,
                                                const struct driver_elf64_section *sym_section,
                                                const struct driver_elf64_symbol *symbol) {
    const struct driver_elf64_section *str_section;
    const char *name;
    uint32_t remaining;

    if (sym_section->link >= DRIVER_ELF_MAX_SECTIONS || symbol == NULL) {
        return NULL;
    }
    str_section = &sections[sym_section->link];
    if (str_section->type != DRIVER_ELF_SHT_STRTAB ||
        !driver_elf_section_valid_local(str_section, file_size) ||
        symbol->name >= str_section->size) {
        return NULL;
    }
    name = (const char *)(image + (uint32_t)str_section->offset + symbol->name);
    remaining = (uint32_t)str_section->size - symbol->name;
    for (uint32_t i = 0; i < remaining; i++) {
        if (name[i] == '\0') {
            return name;
        }
    }
    return NULL;
}

static int driver_kernel_symbol_resolve_local(const char *name, uint64_t *value_out) {
    if (name == NULL || value_out == NULL) {
        return 0;
    }
    for (uint32_t i = 0; g_driver_kernel_symbols[i].name != NULL; i++) {
        if (streq(g_driver_kernel_symbols[i].name, name)) {
            *value_out = g_driver_kernel_symbols[i].value;
            return 1;
        }
    }
    return 0;
}

static const struct kernel_driver *driver_elf_find_driver_symbol_local(
    uint8_t *load_base,
    const uint8_t *image,
    uint32_t file_size,
    const struct driver_elf64_header *header,
    const uint64_t section_addrs[DRIVER_ELF_MAX_SECTIONS]) {
    const struct driver_elf64_section *sections =
        (const struct driver_elf64_section *)(image + (uint32_t)header->shoff);

    for (uint32_t i = 0; i < header->shnum; i++) {
        const struct driver_elf64_section *sym_section = &sections[i];
        const struct driver_elf64_symbol *symbols;
        uint32_t symbol_count;

        if (sym_section->type != DRIVER_ELF_SHT_SYMTAB ||
            sym_section->entsize != sizeof(struct driver_elf64_symbol) ||
            sym_section->link >= header->shnum ||
            !driver_elf_section_valid_local(sym_section, file_size)) {
            continue;
        }
        symbols = (const struct driver_elf64_symbol *)(image + (uint32_t)sym_section->offset);
        symbol_count = (uint32_t)(sym_section->size / sym_section->entsize);
        for (uint32_t s = 0; s < symbol_count; s++) {
            const char *name = driver_elf_symbol_name_local(image,
                                                            file_size,
                                                            sections,
                                                            sym_section,
                                                            &symbols[s]);
            int symbol_ok = 0;
            uint64_t symbol_value;

            if (name == NULL || !streq(name, "kernel_driver")) {
                continue;
            }
            symbol_value = driver_elf_symbol_value_local(&symbols[s],
                                                         image,
                                                         file_size,
                                                         sections,
                                                         sym_section,
                                                         section_addrs,
                                                         load_base,
                                                         header->shnum,
                                                         &symbol_ok);
            if (!symbol_ok) {
                return NULL;
            }
            return (const struct kernel_driver *)(uintptr_t)symbol_value;
        }
    }
    return NULL;
}

static int driver_load_file_local(struct vfs *vfs, struct kernel_driver_file *file) {
    uint8_t *image = NULL;
    uint8_t *load_base = NULL;
    uint32_t file_size = 0;
    uint32_t image_alloc_size = 0;
    uint32_t load_size = 0;
    uint32_t load_alloc_size = 0;
    struct driver_elf64_header *header;
    uint64_t section_addrs[DRIVER_ELF_MAX_SECTIONS];
    const struct kernel_driver *driver;

    if (file == NULL || file->state != KERNEL_DRIVER_FILE_ELF_RELOC) {
        return 0;
    }
    if (!driver_read_file_image_local(vfs,
                                      file->path,
                                      &image,
                                      &image_alloc_size,
                                      &file_size)) {
        return 0;
    }
    header = (struct driver_elf64_header *)image;
    if (!driver_elf_header_valid_local(header, file_size) ||
        !driver_elf_layout_sections_local(image, file_size, header, section_addrs, &load_size)) {
        kprint("driver: ELF layout failed %s\n", file->path);
        driver_free_pages_local(image, image_alloc_size);
        return 0;
    }
    load_base = driver_alloc_pages_local(load_size, &load_alloc_size);
    if (load_base == NULL || load_alloc_size < load_size) {
        kprint("driver: load memory failed %s size=%u\n", file->path, load_size);
        driver_free_pages_local(image, image_alloc_size);
        return 0;
    }
    driver_elf_copy_sections_local(load_base, image, header, section_addrs);
    if (!driver_elf_apply_relocations_local(load_base,
                                            image,
                                            file_size,
                                            header,
                                            section_addrs)) {
        driver_free_pages_local(image, image_alloc_size);
        driver_free_pages_local(load_base, load_alloc_size);
        return 0;
    }
    driver = driver_elf_find_driver_symbol_local(load_base,
                                                 image,
                                                 file_size,
                                                 header,
                                                 section_addrs);
    if (driver == NULL || !driver_register_source_local(driver, "ramdisk", file->path)) {
        kprint("driver: register failed %s\n", file->path);
        driver_free_pages_local(image, image_alloc_size);
        driver_free_pages_local(load_base, load_alloc_size);
        return 0;
    }
    driver_free_pages_local(image, image_alloc_size);
    kprint("driver: loaded %s as %s\n", file->path, driver->name);
    return 1;
}

uint32_t driver_discover_root(struct vfs *vfs, const char *directory) {
    struct vfs_node dir_node;
    struct vfs_node file_node;
    struct vfs_dirent entry;
    uint32_t index = 0;
    uint32_t found = 0;
    char path[NOS_PATH_BUFFER_SIZE];

    if (vfs == NULL || directory == NULL || directory[0] == '\0') {
        return 0;
    }
    if (vfs_opendir(vfs, directory, &dir_node) != 0) {
        kprint("driver: directory not found %s\n", directory);
        return 0;
    }

    while (vfs_readdir(vfs, &dir_node, &index, &entry) == 1) {
        if (!driver_name_has_drv_suffix_local(entry.name)) {
            continue;
        }
        if (!driver_join_path_local(path, sizeof(path), directory, entry.name)) {
            continue;
        }
        if (driver_file_exists_local(path)) {
            continue;
        }
        if (vfs_open(vfs, path, 0, &file_node) != 0 || file_node.kind != VFS_NODE_FILE) {
            continue;
        }
        if (g_driver_file_count >= DRIVER_FILE_MAX_COUNT) {
            kprint("driver: .DRV table full while scanning %s\n", directory);
            break;
        }
        driver_copy_text_local(g_driver_files[g_driver_file_count].name,
                               entry.name,
                               sizeof(g_driver_files[g_driver_file_count].name));
        driver_copy_text_local(g_driver_files[g_driver_file_count].path,
                               path,
                               sizeof(g_driver_files[g_driver_file_count].path));
        g_driver_files[g_driver_file_count].size = vfs_node_file_size(&file_node);
        g_driver_files[g_driver_file_count].state = driver_probe_elf_local(vfs, &file_node);
        kprint("driver: discovered %s size=%u state=%u\n",
               g_driver_files[g_driver_file_count].path,
               g_driver_files[g_driver_file_count].size,
               (uint32_t)g_driver_files[g_driver_file_count].state);
        g_driver_file_count++;
        found++;
    }
    if (found == 0u) {
        kprint("driver: no .DRV files in %s\n", directory);
    }
    return found;
}

uint32_t driver_load_all(struct vfs *vfs) {
    uint32_t loaded = 0;

    if (vfs == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < g_driver_file_count; i++) {
        if (g_driver_files[i].state != KERNEL_DRIVER_FILE_ELF_RELOC) {
            continue;
        }
        if (driver_load_file_local(vfs, &g_driver_files[i])) {
            g_driver_files[i].state = KERNEL_DRIVER_FILE_LOADED;
            loaded++;
        } else {
            g_driver_files[i].state = KERNEL_DRIVER_FILE_LOAD_FAILED;
            kprint("driver: load failed %s\n", g_driver_files[i].path);
        }
    }
    return loaded;
}

const struct kernel_driver_file *driver_get_file(uint32_t index) {
    if (index >= g_driver_file_count) {
        return NULL;
    }
    return &g_driver_files[index];
}

uint32_t driver_file_count(void) {
    return g_driver_file_count;
}
