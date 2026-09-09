#pragma once

#include <stdint.h>

#include "kernel/public/driver/driver.h"

enum {
    DRIVER_ELF32_MODULE_ALLOC_MAX_COUNT = 32u,
    DRIVER_ELF32_MODULE_ALLOC_MAX_PAGES = 256u,
    DRIVER_ELF32_MODULE_VIRT_BASE = 0xd1000000u,
    DRIVER_ELF32_MAX_SECTIONS = 128u,
    DRIVER_ELF32_MAX_FILE_SIZE = 1024u * 1024u,
    DRIVER_ELF32_PAGE_SIZE = 4096u,
    DRIVER_ELF32_IDENT_SIZE = 16u,
    DRIVER_ELF32_HEADER_SIZE = 52u,
    DRIVER_ELF32_CLASS_32 = 1u,
    DRIVER_ELF32_DATA_LSB = 1u,
    DRIVER_ELF32_ET_REL = 1u,
    DRIVER_ELF32_EM_386 = 3u,
    DRIVER_ELF32_SHT_SYMTAB = 2u,
    DRIVER_ELF32_SHT_STRTAB = 3u,
    DRIVER_ELF32_SHT_REL = 9u,
    DRIVER_ELF32_SHT_NOBITS = 8u,
    DRIVER_ELF32_SHF_ALLOC = 0x2u,
    DRIVER_ELF32_SHN_UNDEF = 0u,
    DRIVER_ELF32_SHN_ABS = 0xfff1u,
    DRIVER_ELF32_R_386_32 = 1u,
    DRIVER_ELF32_R_386_PC32 = 2u
};

struct driver_elf32_header {
    uint8_t ident[DRIVER_ELF32_IDENT_SIZE];
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

struct driver_elf32_section {
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

struct driver_elf32_symbol {
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
} __attribute__((packed));

struct driver_elf32_rel {
    uint32_t offset;
    uint32_t info;
} __attribute__((packed));

struct driver_elf32_kernel_symbol {
    const char *name;
    uint32_t value;
};

struct driver_elf32_module_allocation {
    void *virt;
    uint32_t phys;
    uint32_t phys_pages[DRIVER_ELF32_MODULE_ALLOC_MAX_PAGES];
    uint32_t page_count;
};

void *driver_elf32_alloc_pages(uint32_t page_count,
                              uint32_t *phys_out,
                              uint32_t *alloc_size_out);
void driver_elf32_free_pages(void *virt, uint32_t page_count);
int driver_elf32_kernel_symbol_resolve(const char *name, uint32_t *value_out);
