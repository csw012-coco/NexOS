#pragma once

#include "kernel/public/sys/syscall_compat32.h"

typedef struct syscall_compat32_context syscall_i386_context;

int syscall_i386_dispatch_request(
    syscall_i386_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result);
