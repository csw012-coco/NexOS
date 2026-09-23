#include "kernel/public/driver/driver.h"
#include "kernel/public/driver/driver_module.h"
#include "fs/vfs.h"
#include "arch/x86/i386/driver/internal.h"
#include "kernel/internal/driver/driver_loader_internal.h"
#include "kernel/public/core/kprint.h"
#include "lib/string.h"

/*
 * i386 ELF32 .DRV loader.
 *
 * The common driver manager is now built for i386 too. This file keeps only
 * i386 ELF32 relocatable probing/loading; service symbols are exported from
 * driver/services.c. Keep driver model policy in the common manager.
 */

static void driver_elf32_copy_text(char *dst, const char *src, uint32_t dst_size) {
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

static uint32_t driver_elf32_align_up(uint32_t value, uint32_t align) {
    if (align <= 1u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static int driver_elf32_range_valid(uint32_t offset, uint32_t size, uint32_t file_size) {
    return offset <= file_size && size <= file_size - offset;
}

static const char *driver_elf32_elf_symbol_name(const uint8_t *image,
                                               uint32_t file_size,
                                               const struct driver_elf32_section *sections,
                                               const struct driver_elf32_section *sym_section,
                                               const struct driver_elf32_symbol *symbol);
enum kernel_driver_file_state driver_arch_probe_file(struct vfs *vfs,
                                                     struct vfs_node *node,
                                                     struct kernel_driver_file *file) {
    struct driver_elf32_header header;
    uint32_t file_size;
    uint32_t offset = 0u;
    int64_t read_bytes;

    if (file != 0) {
        file->elf_class = 0u;
        file->elf_data = 0u;
        file->elf_type = 0u;
        file->elf_machine = 0u;
    }
    if (vfs == 0 || node == 0) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    file_size = vfs_node_file_size(node);
    if (file != 0) {
        file->size = file_size;
    }
    if (file_size < sizeof(header)) {
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
    if (header.ident[4] != DRIVER_ELF32_CLASS_32 ||
        header.ident[5] != DRIVER_ELF32_DATA_LSB ||
        header.type != DRIVER_ELF32_ET_REL ||
        header.machine != DRIVER_ELF32_EM_386 ||
        header.ehsize != DRIVER_ELF32_HEADER_SIZE ||
        header.shoff == 0u ||
        header.shnum == 0u) {
        return KERNEL_DRIVER_FILE_ELF_INVALID;
    }
    return KERNEL_DRIVER_FILE_ELF_RELOC;
}

static int driver_elf32_read_file_image(struct vfs *vfs,
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
    if (vfs == 0 || file == 0 || file->size < DRIVER_ELF32_HEADER_SIZE ||
        file->size > DRIVER_ELF32_MAX_FILE_SIZE ||
        vfs_open(vfs, file->path, 0u, &node) != 0) {
        return 0;
    }
    pages = (file->size + DRIVER_ELF32_PAGE_SIZE - 1u) / DRIVER_ELF32_PAGE_SIZE;
    image = driver_elf32_alloc_pages(pages, 0, &alloc_size);
    if (image == 0 || alloc_size < file->size) {
        kprint("driver: file image allocation failed %s size=%u\n", file->path, file->size);
        return 0;
    }
    read_bytes = vfs_read(vfs, &node, &offset, image, file->size, VFS_READ_BLOCKING);
    if (read_bytes != (int64_t)file->size) {
        driver_elf32_free_pages(image, pages);
        return 0;
    }
    *image_out = image;
    *alloc_size_out = alloc_size;
    *size_out = file->size;
    return 1;
}

static int driver_elf32_elf_header_valid(const struct driver_elf32_header *header,
                                        uint32_t file_size) {
    uint32_t section_bytes;

    if (header == 0 || file_size < sizeof(*header)) {
        return 0;
    }
    if (header->ident[0] != 0x7fu ||
        header->ident[1] != 'E' ||
        header->ident[2] != 'L' ||
        header->ident[3] != 'F' ||
        header->ident[4] != DRIVER_ELF32_CLASS_32 ||
        header->ident[5] != DRIVER_ELF32_DATA_LSB ||
        header->type != DRIVER_ELF32_ET_REL ||
        header->machine != DRIVER_ELF32_EM_386 ||
        header->ehsize != DRIVER_ELF32_HEADER_SIZE ||
        header->shentsize != sizeof(struct driver_elf32_section) ||
        header->shnum == 0u ||
        header->shnum > DRIVER_ELF32_MAX_SECTIONS) {
        return 0;
    }
    section_bytes = (uint32_t)header->shentsize * (uint32_t)header->shnum;
    return driver_elf32_range_valid(header->shoff, section_bytes, file_size);
}

static int driver_elf32_elf_section_valid(const struct driver_elf32_section *section,
                                         uint32_t file_size) {
    if (section == 0) {
        return 0;
    }
    if (section->type == DRIVER_ELF32_SHT_NOBITS) {
        return 1;
    }
    return driver_elf32_range_valid(section->offset, section->size, file_size);
}

static int driver_elf32_layout_sections(const uint8_t *image,
                                       uint32_t file_size,
                                       const struct driver_elf32_header *header,
                                       uint32_t section_addrs[DRIVER_ELF32_MAX_SECTIONS],
                                       uint32_t *load_size_out) {
    const struct driver_elf32_section *sections;
    uint32_t load_size = 1u;

    if (image == 0 || header == 0 || section_addrs == 0 || load_size_out == 0) {
        return 0;
    }
    sections = (const struct driver_elf32_section *)(image + header->shoff);
    for (uint32_t i = 0u; i < header->shnum; i++) {
        uint32_t align;

        section_addrs[i] = 0u;
        if (!driver_elf32_elf_section_valid(&sections[i], file_size)) {
            return 0;
        }
        if ((sections[i].flags & DRIVER_ELF32_SHF_ALLOC) == 0u || sections[i].size == 0u) {
            continue;
        }
        align = sections[i].addralign > 1u && sections[i].addralign <= DRIVER_ELF32_PAGE_SIZE
                    ? sections[i].addralign
                    : 1u;
        load_size = driver_elf32_align_up(load_size, align);
        section_addrs[i] = load_size;
        load_size += sections[i].size;
    }
    *load_size_out = load_size;
    return load_size != 0u;
}

static void driver_elf32_copy_sections(uint8_t *load_base,
                                      const uint8_t *image,
                                      const struct driver_elf32_header *header,
                                      const uint32_t section_addrs[DRIVER_ELF32_MAX_SECTIONS]) {
    const struct driver_elf32_section *sections =
        (const struct driver_elf32_section *)(image + header->shoff);

    for (uint32_t i = 0u; i < header->shnum; i++) {
        uint8_t *dest;

        if (section_addrs[i] == 0u ||
            (sections[i].flags & DRIVER_ELF32_SHF_ALLOC) == 0u ||
            sections[i].size == 0u) {
            continue;
        }
        dest = load_base + section_addrs[i];
        if (sections[i].type == DRIVER_ELF32_SHT_NOBITS) {
            memset(dest, 0, sections[i].size);
        } else {
            memcpy(dest, image + sections[i].offset, sections[i].size);
        }
    }
}

static uint32_t driver_elf32_symbol_value(const struct driver_elf32_symbol *symbol,
                                         const uint8_t *image,
                                         uint32_t file_size,
                                         const struct driver_elf32_section *sections,
                                         const struct driver_elf32_section *sym_section,
                                         const uint32_t section_addrs[DRIVER_ELF32_MAX_SECTIONS],
                                         uint8_t *load_base,
                                         uint16_t section_count,
                                         int *ok_out) {
    if (ok_out != 0) {
        *ok_out = 0;
    }
    if (symbol == 0) {
        return 0u;
    }
    if (symbol->shndx == DRIVER_ELF32_SHN_ABS) {
        if (ok_out != 0) {
            *ok_out = 1;
        }
        return symbol->value;
    }
    if (symbol->shndx == DRIVER_ELF32_SHN_UNDEF) {
        uint32_t value = 0u;
        const char *name = driver_elf32_elf_symbol_name(image,
                                                       file_size,
                                                       sections,
                                                       sym_section,
                                                       symbol);

        if (driver_elf32_kernel_symbol_resolve(name, &value)) {
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

static int driver_elf32_apply_relocations(uint8_t *load_base,
                                         const uint8_t *image,
                                         uint32_t file_size,
                                         const struct driver_elf32_header *header,
                                         const uint32_t section_addrs[DRIVER_ELF32_MAX_SECTIONS]) {
    const struct driver_elf32_section *sections =
        (const struct driver_elf32_section *)(image + header->shoff);

    for (uint32_t i = 0u; i < header->shnum; i++) {
        const struct driver_elf32_section *rel_section = &sections[i];
        const struct driver_elf32_section *sym_section;
        const struct driver_elf32_rel *relocs;
        const struct driver_elf32_symbol *symbols;
        uint32_t reloc_count;
        uint32_t symbol_count;
        uint32_t target_index;

        if (rel_section->type != DRIVER_ELF32_SHT_REL) {
            continue;
        }
        if (rel_section->entsize != sizeof(struct driver_elf32_rel) ||
            rel_section->link >= header->shnum ||
            rel_section->info >= header->shnum ||
            !driver_elf32_elf_section_valid(rel_section, file_size)) {
            return 0;
        }
        sym_section = &sections[rel_section->link];
        if (sym_section->type != DRIVER_ELF32_SHT_SYMTAB ||
            sym_section->entsize != sizeof(struct driver_elf32_symbol) ||
            !driver_elf32_elf_section_valid(sym_section, file_size)) {
            return 0;
        }
        target_index = rel_section->info;
        if (section_addrs[target_index] == 0u) {
            continue;
        }
        relocs = (const struct driver_elf32_rel *)(image + rel_section->offset);
        symbols = (const struct driver_elf32_symbol *)(image + sym_section->offset);
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
                kprint("driver: bad reloc sec=%u index=%u type=%u sym=%u\n",
                       i,
                       r,
                       type,
                       symbol_index);
                return 0;
            }
            place = load_base + section_addrs[target_index] + relocs[r].offset;
            addend = *((uint32_t *)place);
            value = driver_elf32_symbol_value(&symbols[symbol_index],
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
            if (type == DRIVER_ELF32_R_386_32) {
                *((uint32_t *)place) = value + addend;
            } else if (type == DRIVER_ELF32_R_386_PC32) {
                *((uint32_t *)place) = value + addend - (uint32_t)(uintptr_t)place;
            } else {
                kprint("driver: unsupported reloc type=%u\n", type);
                return 0;
            }
        }
    }
    return 1;
}

static const char *driver_elf32_elf_symbol_name(const uint8_t *image,
                                               uint32_t file_size,
                                               const struct driver_elf32_section *sections,
                                               const struct driver_elf32_section *sym_section,
                                               const struct driver_elf32_symbol *symbol) {
    const struct driver_elf32_section *str_section;
    const char *name;
    uint32_t remaining;

    if (image == 0 || sections == 0 || sym_section == 0 || symbol == 0 ||
        sym_section->link >= DRIVER_ELF32_MAX_SECTIONS) {
        return 0;
    }
    str_section = &sections[sym_section->link];
    if (str_section->type != DRIVER_ELF32_SHT_STRTAB ||
        !driver_elf32_elf_section_valid(str_section, file_size) ||
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

static const struct kernel_driver *driver_elf32_find_driver_symbol(
    uint8_t *load_base,
    const uint8_t *image,
    uint32_t file_size,
    const struct driver_elf32_header *header,
    const uint32_t section_addrs[DRIVER_ELF32_MAX_SECTIONS]) {
    const struct driver_elf32_section *sections =
        (const struct driver_elf32_section *)(image + header->shoff);

    for (uint32_t i = 0u; i < header->shnum; i++) {
        const struct driver_elf32_section *sym_section = &sections[i];
        const struct driver_elf32_symbol *symbols;
        uint32_t symbol_count;

        if (sym_section->type != DRIVER_ELF32_SHT_SYMTAB ||
            sym_section->entsize != sizeof(struct driver_elf32_symbol) ||
            sym_section->link >= header->shnum ||
            !driver_elf32_elf_section_valid(sym_section, file_size)) {
            continue;
        }
        symbols = (const struct driver_elf32_symbol *)(image + sym_section->offset);
        symbol_count = sym_section->size / sym_section->entsize;
        for (uint32_t s = 0u; s < symbol_count; s++) {
            const char *name = driver_elf32_elf_symbol_name(image,
                                                           file_size,
                                                           sections,
                                                           sym_section,
                                                           &symbols[s]);
            int symbol_ok = 0;
            uint32_t symbol_value;

            if (name == 0 || !streq(name, "kernel_driver")) {
                continue;
            }
            symbol_value = driver_elf32_symbol_value(&symbols[s],
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

int driver_arch_load_file(struct vfs *vfs, struct kernel_driver_file *file) {
    uint8_t *image = 0;
    uint8_t *load_base = 0;
    uint32_t file_size = 0u;
    uint32_t image_alloc_size = 0u;
    uint32_t image_pages;
    uint32_t load_size = 0u;
    uint32_t load_alloc_size = 0u;
    uint32_t load_pages;
    struct driver_elf32_header *header;
    uint32_t section_addrs[DRIVER_ELF32_MAX_SECTIONS];
    const struct kernel_driver *driver;

    if (file == 0 || file->state != KERNEL_DRIVER_FILE_ELF_RELOC) {
        return 0;
    }
    if (!driver_elf32_read_file_image(vfs, file, &image, &image_alloc_size, &file_size)) {
        file->reason_code = KERNEL_DRIVER_REASON_LOAD_FAILED;
        return 0;
    }
    image_pages = image_alloc_size / DRIVER_ELF32_PAGE_SIZE;
    header = (struct driver_elf32_header *)image;
    if (!driver_elf32_elf_header_valid(header, file_size) ||
        !driver_elf32_layout_sections(image, file_size, header, section_addrs, &load_size)) {
        kprint("driver: ELF layout failed %s\n", file->path);
        file->reason_code = KERNEL_DRIVER_REASON_LAYOUT_FAILED;
        driver_elf32_free_pages(image, image_pages);
        return 0;
    }
    load_pages = (load_size + DRIVER_ELF32_PAGE_SIZE - 1u) / DRIVER_ELF32_PAGE_SIZE;
    load_base = driver_elf32_alloc_pages(load_pages, 0, &load_alloc_size);
    if (load_base == 0 || load_alloc_size < load_size) {
        kprint("driver: load memory failed %s size=%u\n", file->path, load_size);
        file->reason_code = KERNEL_DRIVER_REASON_NO_MEMORY;
        driver_elf32_free_pages(image, image_pages);
        return 0;
    }
    driver_elf32_copy_sections(load_base, image, header, section_addrs);
    if (!driver_elf32_apply_relocations(load_base, image, file_size, header, section_addrs)) {
        file->reason_code = KERNEL_DRIVER_REASON_RELOC_FAILED;
        driver_elf32_free_pages(image, image_pages);
        driver_elf32_free_pages(load_base, load_pages);
        return 0;
    }
    driver = driver_elf32_find_driver_symbol(load_base, image, file_size, header, section_addrs);
    if (driver != 0) {
        driver_elf32_copy_text(file->driver_name,
                              driver->name,
                              sizeof(file->driver_name));
    }
    if (driver == 0 || !driver_register_source(driver, "ramdisk-i386", file->path)) {
        kprint("driver: register failed %s\n", file->path);
        file->reason_code = driver == 0
            ? KERNEL_DRIVER_REASON_SYMBOL_MISSING
            : KERNEL_DRIVER_REASON_REGISTER_FAILED;
        driver_elf32_free_pages(image, image_pages);
        driver_elf32_free_pages(load_base, load_pages);
        return 0;
    }
    driver_elf32_free_pages(image, image_pages);
    if (driver_boot_verbose_enabled()) {
        kprint("driver: loaded %s as %s\n", file->path, driver->name);
    }
    return 1;
}
