#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/proc/process.h"

int syscall_compat32_request_adapter_mount(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    syscall_compat32_file_copy_ops(&ops);
    return syscall_common_request_core_mount_request(process_current_mut(),
                                                     ctx->vfs,
                                                     request,
                                                     result,
                                                     &ops,
                                                     0);
}
