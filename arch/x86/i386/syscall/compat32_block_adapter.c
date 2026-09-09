#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

int syscall_compat32_request_adapter_block(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;

    (void)ctx;
    if (request == 0 || result == 0) {
        return 0;
    }
    syscall_compat32_user_copy_ops(&copy_ops, 1, 1, 0, (uint64_t)(uint32_t)-1);
    return syscall_common_request_core_block_request(request, result, &copy_ops);
}
