#pragma once

#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "kernel/public/sys/syscall_i386.h"

static inline void syscall_i386_cleanup_pid(syscall_i386_context *ctx,
                                            uint32_t pid) {
    syscall_compat32_cleanup_pid(ctx, pid);
}

static inline int syscall_i386_page_is_shared(uint32_t pid,
                                              uint32_t user_page) {
    return syscall_compat32_page_is_shared(pid, user_page);
}

static inline uintptr_t syscall_i386_wait(
    syscall_i386_context *ctx,
    const struct process_context *context,
    uint32_t pid,
    uint32_t user_info,
    int32_t *status,
    int *blocked) {
    return syscall_compat32_wait(ctx, context, pid, user_info, status, blocked);
}

static inline uintptr_t syscall_i386_exit(syscall_i386_context *ctx,
                                          const struct process_context *context,
                                          int exit_code) {
    return syscall_compat32_exit(ctx, context, exit_code);
}

static inline uintptr_t syscall_i386_yield(
    syscall_i386_context *ctx,
    const struct process_context *context) {
    return syscall_compat32_yield(ctx, context);
}

static inline uintptr_t syscall_i386_sleep(
    syscall_i386_context *ctx,
    const struct process_context *context,
    uint32_t ticks) {
    return syscall_compat32_sleep(ctx, context, ticks);
}

static inline void syscall_i386_vm_snapshot(syscall_i386_context *ctx,
                                            struct syscall_vm_info *info) {
    syscall_compat32_vm_snapshot(ctx, info);
}
