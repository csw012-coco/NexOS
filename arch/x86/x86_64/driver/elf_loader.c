#include "kernel/public/driver/driver.h"
#include "kernel/public/driver/driver_module.h"
#include "fs/vfs.h"
#include "hal/hal.h"
#include "kernel/internal/driver/driver_loader_internal.h"
#include "kernel/public/core/kprint.h"
#include "arch/x86/x86_64/mm/pmm.h"
#include "lib/string.h"

#define DRIVER_ELF_MAX_SECTIONS 256u
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

int driver_elf64_kernel_symbol_resolve(const char *name, uint64_t *value_out);

static const char *driver_elf_symbol_name_local(const uint8_t *image,
                                                uint32_t file_size,
                                                const struct driver_elf64_section *sections,
                                                const struct driver_elf64_section *sym_section,
                                                const struct driver_elf64_symbol *symbol);

static void driver_elf64_copy_text_local(char *dst, const char *src, uint32_t dst_size) {
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
    ptr = hal_phys_direct_map(phys);
    if (ptr == NULL) {
        for (uint32_t i = 0; i < page_count; i++) {
            (void)pmm_free_page(phys + (uint64_t)i * DRIVER_ELF_PAGE_SIZE);
        }
        return NULL;
    }
    memset(ptr, 0, page_count * DRIVER_ELF_PAGE_SIZE);
    if (alloc_size_out != NULL) {
        *alloc_size_out = page_count * DRIVER_ELF_PAGE_SIZE;
    }
    return ptr;
}

static void driver_free_pages_local(void *ptr, uint32_t alloc_size) {
    uint64_t phys;
    uint32_t page_count;

    if (ptr == NULL || alloc_size == 0u) {
        return;
    }
    if (!hal_paging_get_mapping((uint64_t)(uintptr_t)ptr, &phys)) {
        return;
    }
    page_count = driver_align_up_local(alloc_size, DRIVER_ELF_PAGE_SIZE) / DRIVER_ELF_PAGE_SIZE;
    for (uint32_t i = 0; i < page_count; i++) {
        (void)pmm_free_page(phys + (uint64_t)i * DRIVER_ELF_PAGE_SIZE);
    }
}

static enum kernel_driver_file_state driver_probe_elf_local(struct vfs *vfs,
                                                           struct vfs_node *node,
                                                           struct kernel_driver_file *file) {
    struct driver_elf64_header header;
    uint32_t offset = 0;
    int64_t read_bytes;

    if (file != NULL) {
        file->elf_class = 0;
        file->elf_data = 0;
        file->elf_type = 0;
        file->elf_machine = 0;
    }
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
    if (file != NULL) {
        file->elf_class = header.ident[4];
        file->elf_data = header.ident[5];
        file->elf_type = header.type;
        file->elf_machine = header.machine;
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

enum kernel_driver_file_state driver_arch_probe_file(struct vfs *vfs,
                                                     struct vfs_node *node,
                                                     struct kernel_driver_file *file)
    __attribute__((weak));
enum kernel_driver_file_state driver_arch_probe_file(struct vfs *vfs,
                                                     struct vfs_node *node,
                                                     struct kernel_driver_file *file) {
    return driver_probe_elf_local(vfs, node, file);
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

        if (driver_elf64_kernel_symbol_resolve(name, &value)) {
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
        file->reason_code = KERNEL_DRIVER_REASON_LOAD_FAILED;
        return 0;
    }
    header = (struct driver_elf64_header *)image;
    if (!driver_elf_header_valid_local(header, file_size) ||
        !driver_elf_layout_sections_local(image, file_size, header, section_addrs, &load_size)) {
        kprint("driver: ELF layout failed %s\n", file->path);
        file->reason_code = KERNEL_DRIVER_REASON_LAYOUT_FAILED;
        driver_free_pages_local(image, image_alloc_size);
        return 0;
    }
    load_base = driver_alloc_pages_local(load_size, &load_alloc_size);
    if (load_base == NULL || load_alloc_size < load_size) {
        kprint("driver: load memory failed %s size=%u\n", file->path, load_size);
        file->reason_code = KERNEL_DRIVER_REASON_NO_MEMORY;
        driver_free_pages_local(image, image_alloc_size);
        return 0;
    }
    driver_elf_copy_sections_local(load_base, image, header, section_addrs);
    if (!driver_elf_apply_relocations_local(load_base,
                                            image,
                                            file_size,
                                            header,
                                            section_addrs)) {
        file->reason_code = KERNEL_DRIVER_REASON_RELOC_FAILED;
        driver_free_pages_local(image, image_alloc_size);
        driver_free_pages_local(load_base, load_alloc_size);
        return 0;
    }
    driver = driver_elf_find_driver_symbol_local(load_base,
                                                 image,
                                                 file_size,
                                                 header,
                                                 section_addrs);
    if (driver != NULL) {
        driver_elf64_copy_text_local(file->driver_name,
                                     driver->name,
                                     sizeof(file->driver_name));
    }
    if (driver == NULL || !driver_register_source(driver, "ramdisk", file->path)) {
        kprint("driver: register failed %s\n", file->path);
        file->reason_code = driver == NULL
                                ? KERNEL_DRIVER_REASON_SYMBOL_MISSING
                                : KERNEL_DRIVER_REASON_REGISTER_FAILED;
        driver_free_pages_local(image, image_alloc_size);
        driver_free_pages_local(load_base, load_alloc_size);
        return 0;
    }
    driver_free_pages_local(image, image_alloc_size);
    if (driver_boot_verbose_enabled()) {
        kprint("driver: loaded %s as %s\n", file->path, driver->name);
    }
    return 1;
}

int driver_arch_load_file(struct vfs *vfs, struct kernel_driver_file *file)
    __attribute__((weak));
int driver_arch_load_file(struct vfs *vfs, struct kernel_driver_file *file) {
    return driver_load_file_local(vfs, file);
}
