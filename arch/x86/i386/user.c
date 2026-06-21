#include "arch/x86/i386/paging.h"
#include "arch/x86/i386/pmm.h"
#include "arch/x86/i386/user.h"
#include "fs/early_vfs.h"
#include "lib/string.h"

enum {
    ELF_IDENT_SIZE = 16u,
    ELF_CLASS_32 = 1u,
    ELF_DATA_LSB = 1u,
    ELF_TYPE_EXEC = 2u,
    ELF_MACHINE_386 = 3u,
    ELF_PROGRAM_LOAD = 1u,
    ELF_PROGRAM_WRITABLE = 1u << 1,
    ELF_MAX_PROGRAM_HEADERS = 16u
};

struct elf32_header {
    uint8_t ident[ELF_IDENT_SIZE];
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

struct elf32_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed));

static int map_user_range(uint32_t start, uint32_t end, int writable) {
    uint32_t page = start & ~(I386_PAGE_SIZE - 1u);
    uint32_t limit = (end + I386_PAGE_SIZE - 1u) & ~(I386_PAGE_SIZE - 1u);

    while (page < limit) {
        uint32_t existing;

        if (!i386_paging_translate(page, &existing)) {
            uint32_t frame = i386_pmm_alloc_page();

            if (frame == I386_PMM_INVALID_PAGE ||
                !i386_paging_map_page(page, frame, writable, 1)) {
                return 0;
            }
            memset((void *)page, 0, I386_PAGE_SIZE);
        }
        page += I386_PAGE_SIZE;
    }
    return 1;
}

int i386_user_map_stack(uint32_t stack_top) {
    if ((stack_top & (I386_PAGE_SIZE - 1u)) != 0u ||
        stack_top <= I386_PAGING_IDENTITY_LIMIT) {
        return 0;
    }
    return map_user_range(stack_top - I386_PAGE_SIZE, stack_top, 1);
}

static int map_image_page(uint32_t root,
                          uint32_t virtual_address,
                          int writable,
                          uint32_t *physical_out) {
    uint32_t physical;

    if (i386_paging_translate_in(root, virtual_address, &physical)) {
        *physical_out = physical & ~(I386_PAGE_SIZE - 1u);
        return 1;
    }

    physical = i386_pmm_alloc_page();
    if (physical == I386_PMM_INVALID_PAGE ||
        !i386_paging_map_page_in(root,
                                 virtual_address,
                                 physical,
                                 writable,
                                 1)) {
        return 0;
    }
    *physical_out = physical;
    return 1;
}

int i386_user_load_elf_space(struct early_vfs *vfs,
                             const char *path,
                             uint32_t stack_top,
                             struct i386_user_image *image) {
    struct early_vfs_node node;
    struct elf32_header header;
    struct elf32_program_header programs[ELF_MAX_PROGRAM_HEADERS];
    uint32_t bytes_read;
    uint32_t root;
    int loaded = 0;

    if (vfs == 0 || path == 0 || image == 0 ||
        (stack_top & (I386_PAGE_SIZE - 1u)) != 0u ||
        early_vfs_open(vfs, path, &node) != 0 ||
        early_vfs_read(vfs, &node, 0, &header, sizeof(header), &bytes_read) != 0 ||
        bytes_read != sizeof(header) ||
        header.ident[0] != 0x7fu ||
        header.ident[1] != 'E' ||
        header.ident[2] != 'L' ||
        header.ident[3] != 'F' ||
        header.ident[4] != ELF_CLASS_32 ||
        header.ident[5] != ELF_DATA_LSB ||
        header.type != ELF_TYPE_EXEC ||
        header.machine != ELF_MACHINE_386 ||
        header.phnum == 0u ||
        header.phnum > ELF_MAX_PROGRAM_HEADERS ||
        header.phentsize != sizeof(struct elf32_program_header) ||
        early_vfs_read(vfs,
                       &node,
                       header.phoff,
                       programs,
                       header.phnum * sizeof(programs[0]),
                       &bytes_read) != 0 ||
        bytes_read != header.phnum * sizeof(programs[0])) {
        return 0;
    }

    root = i386_paging_create_address_space();
    if (root == 0u) {
        return 0;
    }

    for (uint32_t i = 0; i < header.phnum; i++) {
        const struct elf32_program_header *program = &programs[i];
        uint32_t page;
        uint32_t limit;

        if (program->type != ELF_PROGRAM_LOAD) {
            continue;
        }
        if (program->memsz < program->filesz ||
            program->vaddr < I386_PAGING_IDENTITY_LIMIT ||
            program->vaddr + program->memsz < program->vaddr ||
            program->offset + program->filesz < program->offset ||
            program->offset + program->filesz > early_vfs_file_size(&node)) {
            return 0;
        }

        page = program->vaddr & ~(I386_PAGE_SIZE - 1u);
        limit = (program->vaddr + program->memsz + I386_PAGE_SIZE - 1u) &
                ~(I386_PAGE_SIZE - 1u);
        while (page < limit) {
            uint32_t physical;
            uint8_t *temporary;
            uint32_t copy_start;
            uint32_t copy_end;

            if (!map_image_page(root,
                                page,
                                (program->flags & ELF_PROGRAM_WRITABLE) != 0,
                                &physical) ||
                !i386_paging_temporary_map(physical, 2u, (void **)&temporary)) {
                return 0;
            }
            memset(temporary, 0, I386_PAGE_SIZE);

            copy_start = page > program->vaddr ? page : program->vaddr;
            copy_end = page + I386_PAGE_SIZE;
            if (copy_end > program->vaddr + program->filesz) {
                copy_end = program->vaddr + program->filesz;
            }
            if (copy_end > copy_start &&
                (early_vfs_read(vfs,
                                &node,
                                program->offset + copy_start - program->vaddr,
                                temporary + copy_start - page,
                                copy_end - copy_start,
                                &bytes_read) != 0 ||
                 bytes_read != copy_end - copy_start)) {
                i386_paging_temporary_unmap(2u);
                return 0;
            }
            i386_paging_temporary_unmap(2u);
            page += I386_PAGE_SIZE;
        }
        loaded = 1;
    }

    if (!loaded) {
        return 0;
    }
    {
        uint32_t stack_frame;
        uint8_t *temporary;

        if (!map_image_page(root,
                            stack_top - I386_PAGE_SIZE,
                            1,
                            &stack_frame) ||
            !i386_paging_temporary_map(stack_frame, 2u, (void **)&temporary)) {
            return 0;
        }
        memset(temporary, 0, I386_PAGE_SIZE);
        i386_paging_temporary_unmap(2u);
    }

    image->root = root;
    image->entry = header.entry;
    image->stack_top = stack_top;
    return 1;
}

int i386_user_load_elf(struct early_vfs *vfs,
                       const char *path,
                       uint32_t *entry_out,
                       uint32_t *stack_top_out) {
    struct early_vfs_node node;
    struct elf32_header header;
    struct elf32_program_header programs[ELF_MAX_PROGRAM_HEADERS];
    uint32_t bytes_read = 0;

    if (vfs == 0 || path == 0 || entry_out == 0 || stack_top_out == 0 ||
        early_vfs_open(vfs, path, &node) != 0 ||
        early_vfs_read(vfs, &node, 0, &header, sizeof(header), &bytes_read) != 0 ||
        bytes_read != sizeof(header) ||
        header.ident[0] != 0x7fu ||
        header.ident[1] != 'E' ||
        header.ident[2] != 'L' ||
        header.ident[3] != 'F' ||
        header.ident[4] != ELF_CLASS_32 ||
        header.ident[5] != ELF_DATA_LSB ||
        header.type != ELF_TYPE_EXEC ||
        header.machine != ELF_MACHINE_386 ||
        header.phnum == 0 ||
        header.phnum > ELF_MAX_PROGRAM_HEADERS ||
        header.phentsize != sizeof(struct elf32_program_header)) {
        return 0;
    }

    if (early_vfs_read(vfs,
                       &node,
                       header.phoff,
                       programs,
                       header.phnum * sizeof(programs[0]),
                       &bytes_read) != 0 ||
        bytes_read != header.phnum * sizeof(programs[0])) {
        return 0;
    }

    for (uint32_t i = 0; i < header.phnum; i++) {
        const struct elf32_program_header *program = &programs[i];

        if (program->type != ELF_PROGRAM_LOAD) {
            continue;
        }
        if (program->memsz < program->filesz ||
            program->vaddr < I386_PAGING_IDENTITY_LIMIT ||
            program->vaddr + program->memsz < program->vaddr ||
            program->offset + program->filesz < program->offset ||
            program->offset + program->filesz > early_vfs_file_size(&node) ||
            !map_user_range(program->vaddr,
                            program->vaddr + program->memsz,
                            (program->flags & ELF_PROGRAM_WRITABLE) != 0)) {
            return 0;
        }
        memset((void *)program->vaddr, 0, program->memsz);
        if (program->filesz != 0 &&
            (early_vfs_read(vfs,
                            &node,
                            program->offset,
                            (void *)program->vaddr,
                            program->filesz,
                            &bytes_read) != 0 ||
             bytes_read != program->filesz)) {
            return 0;
        }
    }

    if (!i386_user_map_stack(I386_USER_STACK_TOP)) {
        return 0;
    }
    *entry_out = header.entry;
    *stack_top_out = I386_USER_STACK_TOP;
    return 1;
}
