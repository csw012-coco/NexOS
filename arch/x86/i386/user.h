#pragma once

#include <stdint.h>

struct early_vfs;

struct i386_user_image {
    uint32_t root;
    uint32_t entry;
    uint32_t stack_top;
};

enum {
    I386_USER_STACK_TOP = 0xc0000000u,
    I386_USER_STACK_PAGE = I386_USER_STACK_TOP - 4096u
};

int i386_user_load_elf(struct early_vfs *vfs,
                       const char *path,
                       uint32_t *entry_out,
                       uint32_t *stack_top_out);
int i386_user_map_stack(uint32_t stack_top);
int i386_user_load_elf_space(struct early_vfs *vfs,
                             const char *path,
                             uint32_t stack_top,
                             struct i386_user_image *image);
