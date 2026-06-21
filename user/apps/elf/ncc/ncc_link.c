#include "user/apps/elf/ncc/ncc.h"

enum {
    NCC_ELF_ET_REL = 1,
    NCC_ELF_ET_EXEC = 2,
    NCC_ELF_EM_X86_64 = 62,
    NCC_ELF_PT_LOAD = 1,
    NCC_ELF_SHT_PROGBITS = 1,
    NCC_ELF_SHT_SYMTAB = 2,
    NCC_ELF_SHT_STRTAB = 3,
    NCC_ELF_SHT_RELA = 4,
    NCC_ELF_SHT_NOBITS = 8,
    NCC_ELF_SHF_WRITE = 1,
    NCC_ELF_SHF_ALLOC = 2,
    NCC_ELF_SHF_EXEC = 4,
    NCC_ELF_SHN_UNDEF = 0,
    NCC_ELF_SHN_ABS = 0xfff1,
    NCC_ELF_SHN_COMMON = 0xfff2,
    NCC_ELF_R_X86_64_64 = 1,
    NCC_ELF_R_X86_64_PC32 = 2,
    NCC_ELF_R_X86_64_PLT32 = 4
};

struct ncc_elf64_ehdr {
    uint8_t ident[16];
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

struct ncc_elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

struct ncc_elf64_shdr {
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

struct ncc_elf64_sym {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
    uint64_t value;
    uint64_t size;
} __attribute__((packed));

struct ncc_elf64_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} __attribute__((packed));

struct ncc_link_global {
    char name[NCC_NAME_MAX + 1];
    uint64_t address;
    uint8_t weak;
};

struct ncc_link_layout {
    struct ncc_buffer text;
    struct ncc_buffer data;
    uint64_t text_vaddr;
    uint64_t data_vaddr;
    uint64_t data_file_size;
    uint64_t bss_base;
    uint64_t bss_size;
};

static int ncc_range_valid(uint32_t image_size, uint64_t offset, uint64_t size) {
    return offset <= image_size && size <= image_size - offset;
}

static int ncc_link_buffer_size(struct ncc_buffer *buffer, uint32_t size) {
    uint8_t *data;

    if (size <= buffer->capacity) {
        if (size > buffer->size) {
            memset(buffer->data + buffer->size, 0, size - buffer->size);
        }
        buffer->size = size;
        return 1;
    }
    data = realloc(buffer->data, size);
    if (data == NULL) {
        return 0;
    }
    memset(data + buffer->size, 0, size - buffer->size);
    buffer->data = data;
    buffer->size = size;
    buffer->capacity = size;
    return 1;
}

static enum ncc_section_kind ncc_classify_section(const struct ncc_elf64_shdr *section) {
    if (section->type == NCC_ELF_SHT_NOBITS) {
        return NCC_SEC_BSS;
    }
    if ((section->flags & NCC_ELF_SHF_WRITE) != 0u) {
        return NCC_SEC_DATA;
    }
    if ((section->flags & NCC_ELF_SHF_EXEC) != 0u) {
        return NCC_SEC_TEXT;
    }
    return NCC_SEC_RODATA;
}

static int ncc_parse_elf_image(uint8_t *image,
                               uint32_t image_size,
                               const char *name,
                               struct ncc_object *object,
                               char *error,
                               uint32_t error_size) {
    struct ncc_elf64_ehdr *header;
    struct ncc_elf64_shdr *sections;
    struct ncc_elf64_shdr *symtab = NULL;
    const char *section_names;
    int32_t *section_map;
    uint32_t alloc_count = 0u;
    uint32_t relocation_count = 0u;

    memset(object, 0, sizeof(*object));
    if (image_size < sizeof(*header)) {
        ncc_copy_text(error, error_size, "ELF object is too small");
        return 0;
    }
    header = (struct ncc_elf64_ehdr *)image;
    if (header->ident[0] != 0x7f ||
        header->ident[1] != 'E' ||
        header->ident[2] != 'L' ||
        header->ident[3] != 'F' ||
        header->ident[4] != 2u ||
        header->ident[5] != 1u ||
        header->type != NCC_ELF_ET_REL ||
        header->machine != NCC_ELF_EM_X86_64 ||
        header->shentsize != sizeof(struct ncc_elf64_shdr) ||
        header->shnum == 0u ||
        !ncc_range_valid(image_size,
                         header->shoff,
                         (uint64_t)header->shnum * sizeof(struct ncc_elf64_shdr))) {
        ncc_copy_text(error, error_size, "unsupported ELF relocatable object");
        return 0;
    }
    sections = (struct ncc_elf64_shdr *)(image + header->shoff);
    if (header->shstrndx >= header->shnum ||
        !ncc_range_valid(image_size,
                         sections[header->shstrndx].offset,
                         sections[header->shstrndx].size)) {
        ncc_copy_text(error, error_size, "invalid ELF section names");
        return 0;
    }
    section_names = (const char *)(image + sections[header->shstrndx].offset);
    section_map = malloc((uint32_t)header->shnum * sizeof(*section_map));
    if (section_map == NULL) {
        ncc_copy_text(error, error_size, "out of memory");
        return 0;
    }
    for (uint32_t i = 0u; i < header->shnum; i++) {
        const char *section_name = sections[i].name < sections[header->shstrndx].size
                                       ? section_names + sections[i].name
                                       : "";

        section_map[i] = -1;
        if (sections[i].type == NCC_ELF_SHT_SYMTAB) {
            symtab = &sections[i];
        }
        if (sections[i].type == NCC_ELF_SHT_RELA) {
            relocation_count += sections[i].entsize != 0u
                                    ? (uint32_t)(sections[i].size / sections[i].entsize)
                                    : 0u;
        }
        if ((sections[i].flags & NCC_ELF_SHF_ALLOC) != 0u &&
            strcmp(section_name, ".eh_frame") != 0 &&
            (sections[i].type == NCC_ELF_SHT_PROGBITS ||
             sections[i].type == NCC_ELF_SHT_NOBITS)) {
            section_map[i] = (int32_t)alloc_count++;
        }
    }
    if (symtab == NULL || symtab->entsize != sizeof(struct ncc_elf64_sym) ||
        symtab->link >= header->shnum ||
        !ncc_range_valid(image_size, symtab->offset, symtab->size) ||
        !ncc_range_valid(image_size,
                         sections[symtab->link].offset,
                         sections[symtab->link].size)) {
        free(section_map);
        ncc_copy_text(error, error_size, "ELF object has no valid symbol table");
        return 0;
    }
    object->sections = calloc(alloc_count != 0u ? alloc_count : 1u,
                              sizeof(*object->sections));
    object->symbol_count = (uint32_t)(symtab->size / symtab->entsize);
    object->symbols = calloc(object->symbol_count != 0u ? object->symbol_count : 1u,
                             sizeof(*object->symbols));
    object->relocs = calloc(relocation_count != 0u ? relocation_count : 1u,
                            sizeof(*object->relocs));
    if (object->sections == NULL || object->symbols == NULL || object->relocs == NULL) {
        free(section_map);
        ncc_object_destroy(object);
        ncc_copy_text(error, error_size, "out of memory");
        return 0;
    }
    object->section_count = alloc_count;
    object->owned_image = image;
    ncc_copy_text(object->name, sizeof(object->name), name);
    for (uint32_t i = 0u; i < header->shnum; i++) {
        struct ncc_section *output;

        if (section_map[i] < 0) {
            continue;
        }
        output = &object->sections[section_map[i]];
        output->kind = ncc_classify_section(&sections[i]);
        output->size = sections[i].size;
        output->align = sections[i].addralign != 0u ? sections[i].addralign : 1u;
        if (sections[i].type != NCC_ELF_SHT_NOBITS) {
            if (!ncc_range_valid(image_size, sections[i].offset, sections[i].size)) {
                free(section_map);
                ncc_object_destroy(object);
                ncc_copy_text(error, error_size, "ELF section outside object");
                return 0;
            }
            output->bytes.data = image + sections[i].offset;
            output->bytes.size = (uint32_t)sections[i].size;
            output->bytes.capacity = output->bytes.size;
        }
    }
    {
        struct ncc_elf64_sym *symbols = (struct ncc_elf64_sym *)(image + symtab->offset);
        const char *strings = (const char *)(image + sections[symtab->link].offset);
        uint64_t strings_size = sections[symtab->link].size;

        for (uint32_t i = 0u; i < object->symbol_count; i++) {
            struct ncc_symbol *symbol = &object->symbols[i];
            uint8_t bind = symbols[i].info >> 4;

            if (symbols[i].name < strings_size) {
                ncc_copy_text(symbol->name,
                              sizeof(symbol->name),
                              strings + symbols[i].name);
            }
            symbol->value = symbols[i].value;
            symbol->size = symbols[i].size;
            symbol->global = bind == 1u || bind == 2u;
            symbol->weak = bind == 2u;
            if (symbols[i].shndx == NCC_ELF_SHN_UNDEF) {
                symbol->section = -1;
            } else if (symbols[i].shndx == NCC_ELF_SHN_ABS) {
                symbol->section = -2;
            } else if (symbols[i].shndx == NCC_ELF_SHN_COMMON) {
                symbol->section = -3;
            } else if (symbols[i].shndx < header->shnum) {
                symbol->section = section_map[symbols[i].shndx];
            } else {
                symbol->section = -5;
            }
        }
    }
    for (uint32_t i = 0u; i < header->shnum; i++) {
        struct ncc_elf64_shdr *relocation_section = &sections[i];
        struct ncc_elf64_rela *relocations;
        uint32_t count;
        int32_t target;

        if (relocation_section->type != NCC_ELF_SHT_RELA ||
            relocation_section->info >= header->shnum ||
            relocation_section->entsize != sizeof(struct ncc_elf64_rela)) {
            continue;
        }
        target = section_map[relocation_section->info];
        if (target < 0) {
            continue;
        }
        if (!ncc_range_valid(image_size,
                             relocation_section->offset,
                             relocation_section->size)) {
            free(section_map);
            ncc_object_destroy(object);
            ncc_copy_text(error, error_size, "ELF relocations outside object");
            return 0;
        }
        relocations = (struct ncc_elf64_rela *)(image + relocation_section->offset);
        count = (uint32_t)(relocation_section->size / relocation_section->entsize);
        for (uint32_t j = 0u; j < count; j++) {
            struct ncc_reloc *reloc = &object->relocs[object->reloc_count++];

            reloc->section = target;
            reloc->offset = relocations[j].offset;
            reloc->type = (uint32_t)relocations[j].info;
            reloc->symbol = (uint32_t)(relocations[j].info >> 32);
            reloc->addend = relocations[j].addend;
            if (reloc->symbol >= object->symbol_count) {
                free(section_map);
                ncc_object_destroy(object);
                ncc_copy_text(error, error_size, "ELF relocation has invalid symbol");
                return 0;
            }
        }
    }
    free(section_map);
    return 1;
}

int ncc_load_elf_object(const char *path,
                        struct ncc_object *object,
                        char *error,
                        uint32_t error_size) {
    uint8_t *image;
    uint32_t size;

    if (!ncc_read_file(path, &image, &size)) {
        snprintf(error, error_size, "could not read object: %s", path);
        return 0;
    }
    if (!ncc_parse_elf_image(image, size, path, object, error, error_size)) {
        free(image);
        return 0;
    }
    return 1;
}

static uint32_t ncc_ar_decimal(const uint8_t *text, uint32_t length) {
    uint32_t value = 0u;

    for (uint32_t i = 0u; i < length; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            value = value * 10u + (uint32_t)(text[i] - '0');
        }
    }
    return value;
}

int ncc_load_archive(const char *path,
                     struct ncc_object *objects,
                     uint32_t object_capacity,
                     uint32_t *object_count,
                     char *error,
                     uint32_t error_size) {
    uint8_t *archive;
    uint32_t archive_size;
    uint32_t offset = 8u;

    if (!ncc_read_file(path, &archive, &archive_size) ||
        archive_size < 8u ||
        memcmp(archive, "!<arch>\n", 8u) != 0) {
        free(archive);
        snprintf(error, error_size, "could not read archive: %s", path);
        return 0;
    }
    while (offset + 60u <= archive_size) {
        const uint8_t *header = archive + offset;
        uint32_t member_size = ncc_ar_decimal(header + 48u, 10u);
        uint32_t data_offset = offset + 60u;
        char name[NCC_NAME_MAX + 1];
        uint32_t name_len = 0u;

        if (header[58] != '`' || header[59] != '\n' ||
            member_size > archive_size - data_offset) {
            free(archive);
            ncc_copy_text(error, error_size, "corrupt static archive");
            return 0;
        }
        while (name_len < 16u &&
               header[name_len] != '/' &&
               header[name_len] != ' ') {
            if (name_len + 1u < sizeof(name)) {
                name[name_len] = (char)header[name_len];
            }
            name_len++;
        }
        if (name_len >= sizeof(name)) name_len = sizeof(name) - 1u;
        name[name_len] = '\0';
        if (header[0] != '/' && member_size >= 4u &&
            archive[data_offset] == 0x7f &&
            archive[data_offset + 1u] == 'E' &&
            archive[data_offset + 2u] == 'L' &&
            archive[data_offset + 3u] == 'F') {
            uint8_t *member;

            if (*object_count >= object_capacity) {
                free(archive);
                ncc_copy_text(error, error_size, "too many archive members");
                return 0;
            }
            member = malloc(member_size);
            if (member == NULL) {
                free(archive);
                ncc_copy_text(error, error_size, "out of memory");
                return 0;
            }
            memcpy(member, archive + data_offset, member_size);
            if (!ncc_parse_elf_image(member,
                                     member_size,
                                     name,
                                     &objects[*object_count],
                                     error,
                                     error_size)) {
                free(member);
                free(archive);
                return 0;
            }
            (*object_count)++;
        }
        offset = data_offset + member_size;
        if ((offset & 1u) != 0u) offset++;
    }
    free(archive);
    return 1;
}

static uint64_t ncc_section_address(const struct ncc_link_layout *layout,
                                    const struct ncc_section *section) {
    if (section->kind == NCC_SEC_TEXT || section->kind == NCC_SEC_RODATA) {
        return layout->text_vaddr + section->output_offset;
    }
    if (section->kind == NCC_SEC_DATA) {
        return layout->data_vaddr + section->output_offset;
    }
    return layout->data_vaddr + layout->bss_base + section->output_offset;
}

static uint8_t *ncc_section_output(struct ncc_link_layout *layout,
                                   const struct ncc_section *section) {
    if (section->kind == NCC_SEC_TEXT || section->kind == NCC_SEC_RODATA) {
        return layout->text.data + section->output_offset;
    }
    if (section->kind == NCC_SEC_DATA) {
        return layout->data.data + section->output_offset;
    }
    return NULL;
}

static int ncc_find_global(const struct ncc_link_global *globals,
                           uint32_t count,
                           const char *name) {
    for (uint32_t i = 0u; i < count; i++) {
        if (strcmp(globals[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint64_t ncc_symbol_address(struct ncc_object *object,
                                   struct ncc_symbol *symbol,
                                   struct ncc_link_layout *layout,
                                   struct ncc_link_global *globals,
                                   uint32_t global_count,
                                   int *ok) {
    if (symbol->section >= 0) {
        *ok = 1;
        return ncc_section_address(layout, &object->sections[symbol->section]) + symbol->value;
    }
    if (symbol->section == -2) {
        *ok = 1;
        return symbol->value;
    }
    if (symbol->section == -4) {
        *ok = 1;
        return layout->data_vaddr + layout->bss_base + symbol->value;
    }
    if (symbol->section == -1 && symbol->name[0] != '\0') {
        int global = ncc_find_global(globals, global_count, symbol->name);

        if (global >= 0) {
            *ok = 1;
            return globals[global].address;
        }
        if (symbol->weak) {
            *ok = 1;
            return 0u;
        }
    }
    *ok = 0;
    return 0u;
}

static int ncc_write_zeros(int fd, uint64_t count) {
    uint8_t zeros[256];

    memset(zeros, 0, sizeof(zeros));
    while (count != 0u) {
        uint32_t chunk = count > sizeof(zeros) ? sizeof(zeros) : (uint32_t)count;
        if (!ncc_write_all(fd, zeros, chunk)) return 0;
        count -= chunk;
    }
    return 1;
}

int ncc_link_executable(const char *output_path,
                        struct ncc_object *objects,
                        uint32_t object_count,
                        char *error,
                        uint32_t error_size) {
    struct ncc_link_layout layout;
    struct ncc_link_global *globals;
    uint32_t global_count = 0u;
    uint64_t text_size = 0u;
    uint64_t data_size = 0u;
    uint64_t bss_size = 0u;
    uint64_t bss_align = 1u;
    uint64_t text_file_offset = 0x1000u;
    uint64_t data_file_offset;
    uint64_t entry = 0u;
    int fd;

    memset(&layout, 0, sizeof(layout));
    globals = calloc(NCC_GLOBAL_MAX, sizeof(*globals));
    if (globals == NULL) {
        ncc_copy_text(error, error_size, "out of memory for linker symbols");
        return 0;
    }
    for (uint32_t i = 0u; i < object_count; i++) {
        for (uint32_t j = 0u; j < objects[i].section_count; j++) {
            struct ncc_section *section = &objects[i].sections[j];
            uint64_t *cursor;

            if (section->kind == NCC_SEC_TEXT || section->kind == NCC_SEC_RODATA) {
                cursor = &text_size;
            } else if (section->kind == NCC_SEC_DATA) {
                cursor = &data_size;
            } else {
                cursor = &bss_size;
                if (section->align > bss_align) bss_align = section->align;
            }
            *cursor = ncc_align_up(*cursor, section->align);
            section->output_offset = *cursor;
            *cursor += section->size;
        }
    }
    for (uint32_t i = 0u; i < object_count; i++) {
        for (uint32_t j = 0u; j < objects[i].symbol_count; j++) {
            struct ncc_symbol *symbol = &objects[i].symbols[j];

            if (symbol->section == -3) {
                uint64_t align = symbol->value != 0u ? symbol->value : 1u;
                bss_size = ncc_align_up(bss_size, align);
                symbol->section = -4;
                symbol->value = bss_size;
                bss_size += symbol->size;
                if (align > bss_align) bss_align = align;
            }
        }
    }
    layout.text_vaddr = NCC_ELF_BASE;
    layout.data_vaddr = NCC_ELF_BASE + ncc_align_up(text_size, 0x1000u);
    layout.data_file_size = data_size;
    layout.bss_base = ncc_align_up(data_size, bss_align);
    layout.bss_size = layout.bss_base + bss_size;
    if (!ncc_link_buffer_size(&layout.text, (uint32_t)text_size) ||
        !ncc_link_buffer_size(&layout.data, (uint32_t)data_size)) {
        ncc_copy_text(error, error_size, "out of memory while laying out executable");
        goto fail;
    }
    for (uint32_t i = 0u; i < object_count; i++) {
        for (uint32_t j = 0u; j < objects[i].section_count; j++) {
            struct ncc_section *section = &objects[i].sections[j];
            uint8_t *target = ncc_section_output(&layout, section);

            if (target != NULL && section->bytes.data != NULL && section->bytes.size != 0u) {
                memcpy(target, section->bytes.data, section->bytes.size);
            }
        }
    }
    for (uint32_t i = 0u; i < object_count; i++) {
        for (uint32_t j = 0u; j < objects[i].symbol_count; j++) {
            struct ncc_symbol *symbol = &objects[i].symbols[j];
            int existing;
            int ok;
            uint64_t address;

            if (!symbol->global || symbol->name[0] == '\0' ||
                symbol->section == -1 || symbol->section == -5) {
                continue;
            }
            address = ncc_symbol_address(&objects[i],
                                         symbol,
                                         &layout,
                                         globals,
                                         global_count,
                                         &ok);
            if (!ok) continue;
            existing = ncc_find_global(globals, global_count, symbol->name);
            if (existing >= 0) {
                if (!globals[existing].weak && !symbol->weak) {
                    snprintf(error, error_size, "duplicate symbol: %s", symbol->name);
                    goto fail;
                }
                if (globals[existing].weak && !symbol->weak) {
                    globals[existing].address = address;
                    globals[existing].weak = 0u;
                }
                continue;
            }
            if (global_count >= NCC_GLOBAL_MAX) {
                ncc_copy_text(error, error_size, "too many global symbols");
                goto fail;
            }
            ncc_copy_text(globals[global_count].name,
                          sizeof(globals[global_count].name),
                          symbol->name);
            globals[global_count].address = address;
            globals[global_count].weak = symbol->weak;
            global_count++;
        }
    }
    {
        int start = ncc_find_global(globals, global_count, "_start");
        if (start < 0) {
            ncc_copy_text(error, error_size, "missing _start");
            goto fail;
        }
        entry = globals[start].address;
    }
    for (uint32_t i = 0u; i < object_count; i++) {
        for (uint32_t j = 0u; j < objects[i].reloc_count; j++) {
            struct ncc_reloc *reloc = &objects[i].relocs[j];
            struct ncc_section *section;
            struct ncc_symbol *symbol;
            uint8_t *location;
            uint64_t place;
            uint64_t value;
            int ok;

            if (reloc->section < 0 ||
                (uint32_t)reloc->section >= objects[i].section_count ||
                reloc->symbol >= objects[i].symbol_count) {
                ncc_copy_text(error, error_size, "invalid relocation");
                goto fail;
            }
            section = &objects[i].sections[reloc->section];
            location = ncc_section_output(&layout, section);
            if (location == NULL ||
                reloc->offset + (reloc->type == NCC_ELF_R_X86_64_64 ? 8u : 4u) > section->size) {
                ncc_copy_text(error, error_size, "relocation targets unsupported section");
                goto fail;
            }
            location += reloc->offset;
            place = ncc_section_address(&layout, section) + reloc->offset;
            symbol = &objects[i].symbols[reloc->symbol];
            value = ncc_symbol_address(&objects[i],
                                       symbol,
                                       &layout,
                                       globals,
                                       global_count,
                                       &ok);
            if (!ok) {
                snprintf(error,
                         error_size,
                         "undefined symbol: %s",
                         symbol->name[0] != '\0' ? symbol->name : "<local>");
                goto fail;
            }
            if (reloc->type == NCC_ELF_R_X86_64_64) {
                uint64_t result = value + (uint64_t)reloc->addend;
                memcpy(location, &result, sizeof(result));
            } else if (reloc->type == NCC_ELF_R_X86_64_PC32 ||
                       reloc->type == NCC_ELF_R_X86_64_PLT32) {
                int64_t result = (int64_t)value + reloc->addend - (int64_t)place;
                int32_t result32;
                if (result < -2147483648ll || result > 2147483647ll) {
                    ncc_copy_text(error, error_size, "PC-relative relocation overflow");
                    goto fail;
                }
                result32 = (int32_t)result;
                memcpy(location, &result32, sizeof(result32));
            } else {
                snprintf(error, error_size, "unsupported relocation type %u", reloc->type);
                goto fail;
            }
        }
    }
    data_file_offset = ncc_align_up(text_file_offset + text_size, 0x1000u);
    fd = open(output_path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) {
        snprintf(error, error_size, "could not create output: %s", output_path);
        goto fail;
    }
    {
        struct ncc_elf64_ehdr header;
        struct ncc_elf64_phdr phdr[2];

        memset(&header, 0, sizeof(header));
        memset(phdr, 0, sizeof(phdr));
        header.ident[0] = 0x7f;
        header.ident[1] = 'E';
        header.ident[2] = 'L';
        header.ident[3] = 'F';
        header.ident[4] = 2u;
        header.ident[5] = 1u;
        header.ident[6] = 1u;
        header.type = NCC_ELF_ET_EXEC;
        header.machine = NCC_ELF_EM_X86_64;
        header.version = 1u;
        header.entry = entry;
        header.phoff = sizeof(header);
        header.ehsize = sizeof(header);
        header.phentsize = sizeof(phdr[0]);
        header.phnum = 2u;
        phdr[0].type = NCC_ELF_PT_LOAD;
        phdr[0].flags = 5u;
        phdr[0].offset = text_file_offset;
        phdr[0].vaddr = layout.text_vaddr;
        phdr[0].paddr = layout.text_vaddr;
        phdr[0].filesz = text_size;
        phdr[0].memsz = text_size;
        phdr[0].align = 0x1000u;
        phdr[1].type = NCC_ELF_PT_LOAD;
        phdr[1].flags = 6u;
        phdr[1].offset = data_file_offset;
        phdr[1].vaddr = layout.data_vaddr;
        phdr[1].paddr = layout.data_vaddr;
        phdr[1].filesz = data_size;
        phdr[1].memsz = layout.bss_size;
        phdr[1].align = 0x1000u;
        if (!ncc_write_all(fd, &header, sizeof(header))) {
            close(fd);
            ncc_copy_text(error, error_size, "output ELF header write failed");
            goto fail;
        }
        if (!ncc_write_all(fd, phdr, sizeof(phdr))) {
            close(fd);
            ncc_copy_text(error, error_size, "output program headers write failed");
            goto fail;
        }
        if (!ncc_write_zeros(fd,
                             text_file_offset - sizeof(header) - sizeof(phdr))) {
            close(fd);
            ncc_copy_text(error, error_size, "output text padding write failed");
            goto fail;
        }
        if (!ncc_write_all(fd, layout.text.data, layout.text.size)) {
            close(fd);
            ncc_copy_text(error, error_size, "output text write failed");
            goto fail;
        }
        if (!ncc_write_zeros(fd,
                             data_file_offset - text_file_offset - layout.text.size)) {
            close(fd);
            ncc_copy_text(error, error_size, "output data padding write failed");
            goto fail;
        }
        if (!ncc_write_all(fd, layout.data.data, layout.data.size)) {
            close(fd);
            ncc_copy_text(error, error_size, "output data write failed");
            goto fail;
        }
    }
    close(fd);
    free(layout.text.data);
    free(layout.data.data);
    free(globals);
    return 1;

fail:
    free(layout.text.data);
    free(layout.data.data);
    free(globals);
    return 0;
}
