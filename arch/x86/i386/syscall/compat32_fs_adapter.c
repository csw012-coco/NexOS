#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/proc/process.h"

static int syscall_compat32_fs_fd_request(
    struct process *proc,
    struct vfs *vfs,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *ops) {
    struct kernel_syscall_request adjusted;

    if (request == 0) {
        return 0;
    }
    if (request->number != SYS_SEEK) {
        return syscall_common_request_core_fs_fd_request(
            proc, vfs, request, result, ops);
    }
    adjusted = *request;
    adjusted.args[1] = (uint64_t)(int64_t)(int32_t)request->args[1];
    return syscall_common_request_core_fs_fd_request(
        proc, vfs, &adjusted, result, ops);
}

int syscall_compat32_request_adapter_fs(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    syscall_compat32_file_copy_ops(&ops);
    if (syscall_compat32_fs_fd_request(
            process_current_mut(), ctx->vfs, request, result, &ops)) {
        return 1;
    }
    return syscall_common_request_core_fs_path_request(process_current_mut(),
                                                       ctx->vfs,
                                                       request,
                                                       result,
                                                       &ops);
}
