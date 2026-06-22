#pragma once

#include <stdint.h>

enum syscall_common_action {
    SYSCALL_COMMON_RETURN = 0,
    SYSCALL_COMMON_YIELD = 1,
    SYSCALL_COMMON_EXIT = 2
};

struct syscall_register_request {
    uint32_t number;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t instruction_pointer;
    uint32_t stack_pointer;
};

struct syscall_common_context {
    uint32_t pid;
    uint32_t ticks;
    void *opaque;
    uint32_t (*open)(void *opaque,
                     uint32_t user_path,
                     uint32_t flags);
    uint32_t (*read)(void *opaque,
                     uint32_t fd,
                     uint32_t user_address,
                     uint32_t size,
                     uint32_t flags);
    uint32_t (*write)(void *opaque,
                      uint32_t fd,
                      uint32_t user_address,
                      uint32_t size);
    uint32_t (*close)(void *opaque, uint32_t fd);
};

struct syscall_common_result {
    uint32_t value;
    enum syscall_common_action action;
};

struct syscall_common_result syscall_dispatch_common(
    const struct syscall_register_request *request,
    const struct syscall_common_context *context);
