#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/proc/process.h"

int syscall_compat32_request_adapter_io(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_file_io_ops io_ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    syscall_compat32_file_copy_ops(&copy_ops);
    syscall_compat32_file_io_ops(ctx, &io_ops);
    return syscall_common_request_core_io_request(process_current_mut(),
                                                  ctx->vfs,
                                                  request,
                                                  result,
                                                  &copy_ops,
                                                  &io_ops);
}
