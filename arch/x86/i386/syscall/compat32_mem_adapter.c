#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

/*
 * i386 memory ABI adapter.
 *
 * Keep only compat32 pointer copying and request handoff here. i386 VM page
 * backend ops live in syscall_compat32_vm_page_ops.c.
 */

static void syscall_compat32_mem_copy_ops(
    struct syscall_common_user_copy_ops *ops) {
    syscall_compat32_user_copy_ops(ops, 1, 0, 0, 0u);
}

int syscall_compat32_request_adapter_mem(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_vm_page_ops page_ops;
    struct syscall_common_user_copy_ops copy_ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    syscall_compat32_vm_page_ops(ctx, &page_ops);
    syscall_compat32_mem_copy_ops(&copy_ops);
    if (syscall_common_request_core_mmap_request(
            request, result, &copy_ops, &page_ops)) {
        return 1;
    }
    if (syscall_common_request_core_vm_page_request(
            request, result, &page_ops)) {
        return 1;
    }
    return 0;
}
