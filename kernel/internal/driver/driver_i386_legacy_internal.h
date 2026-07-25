#pragma once

#include <stdint.h>

#include "kernel/public/driver/driver.h"

enum {
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

void *driver_i386_alloc_pages(uint32_t page_count,
                              uint32_t *phys_out,
                              uint32_t *alloc_size_out);
void driver_i386_free_pages(void *virt, uint32_t page_count);
int driver_i386_kernel_symbol_resolve(const char *name, uint32_t *value_out);
