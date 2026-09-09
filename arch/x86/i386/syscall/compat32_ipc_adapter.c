#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

/*
 * i386 IPC syscall ABI adapter.
 *
 * This keeps 32-bit name/buffer copying out of the native int 0x40 request
 * adapter. IPC object lifecycle and dispatch live in the common syscall core.
 */

static void syscall_compat32_ipc_copy_ops(
    struct syscall_common_user_copy_ops *ops) {
    syscall_compat32_user_copy_ops(ops, 1, 1, 1, 0u);
}

int syscall_compat32_request_adapter_ipc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    syscall_compat32_ipc_copy_ops(&copy_ops);
    return syscall_common_request_core_ipc_request(request, result, &copy_ops);
}
