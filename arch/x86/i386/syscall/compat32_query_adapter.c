#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/proc/process.h"

static uint32_t syscall_compat32_query_fd_kind(void *ctx_ptr, uint32_t fd) {
    (void)ctx_ptr;
    return syscall_common_request_core_process_fd_kind(
        process_current_mut(),
        fd);
}

static int32_t syscall_compat32_query_tty(void *ctx_ptr,
                                          uint32_t fd,
                                          struct syscall_tty_info *info) {
    (void)ctx_ptr;
    return syscall_common_request_core_process_tty_query(
        process_current_mut(),
        fd,
        info);
}

static int32_t syscall_compat32_query_fd_query(void *ctx_ptr,
                                           uint32_t fd,
                                           struct syscall_fd_info *info) {
    (void)ctx_ptr;
    return syscall_common_request_core_process_fd_query(
        process_current_mut(),
        fd,
        info);
}

static int syscall_compat32_query_fill_mount_info(
    void *ctx_ptr,
    struct syscall_mount_info *info,
    uint32_t index,
    uint32_t flags) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    return ctx != 0 && ctx->fill_mount_info != 0
        ? ctx->fill_mount_info(info, index, flags)
        : 0;
}

static void syscall_compat32_query_fill_machine_info(
    void *ctx_ptr,
    struct syscall_machine_info *info) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx != 0 && ctx->fill_machine_info != 0) {
        ctx->fill_machine_info(info);
    }
}

int syscall_compat32_request_adapter_query(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_query_ops query_ops;

    if (ctx == 0 || request == 0 || result == 0 ||
        request->number != SYS_QUERY) {
        return 0;
    }
    syscall_compat32_user_copy_ops(&copy_ops, 1, 1, 1, (uint64_t)(uint32_t)-1);
    query_ops.fd_kind = syscall_compat32_query_fd_kind;
    query_ops.tty_query = syscall_compat32_query_tty;
    query_ops.fd_query = syscall_compat32_query_fd_query;
    query_ops.fill_mount_info = syscall_compat32_query_fill_mount_info;
    query_ops.fill_machine_info = syscall_compat32_query_fill_machine_info;
    query_ops.ctx = ctx;
    return syscall_common_request_core_query_request_with_ops(
        request, result, &copy_ops, &query_ops);
}
