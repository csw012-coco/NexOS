#pragma once

#include <stdint.h>

#include "kernel/public/sys/syscall.h"

struct bootx_console_info;

enum kernel_gfx_buffer_kind {
    KERNEL_GFX_BUFFER_INVALID = 0,
    KERNEL_GFX_BUFFER_INFO_OUT = 1,
    KERNEL_GFX_BUFFER_COMMAND_IN = 2
};

void kernel_gfx_init(const struct bootx_console_info *console);
enum kernel_gfx_buffer_kind kernel_gfx_buffer_kind(uint32_t op);
int kernel_gfx_dispatch(uint32_t op, const struct syscall_gfx_command *cmd, struct syscall_gfx_info *info);
void kernel_gfx_begin_batch(void);
void kernel_gfx_end_batch(uint32_t flags);
