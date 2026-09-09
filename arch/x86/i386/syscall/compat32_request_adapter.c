#include "arch/x86/i386/syscall/compat32_internal.h"

/*
 * 32-bit compatibility syscall adapter.
 *
 * The int 0x40 entry path decodes 32-bit registers into a
 * kernel_syscall_request. This file now only chains the compat32 ABI adapters;
 * syscall implementation details live either in common syscall helpers or in
 * focused 32-bit copy/ABI adapter files.
 */

int syscall_compat32_dispatch_request(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    return syscall_compat32_request_adapter_io(ctx, request, result) ||
           syscall_compat32_request_adapter_fs(ctx, request, result) ||
           syscall_compat32_request_adapter_proc(ctx, request, result) ||
           syscall_compat32_request_adapter_mount(ctx, request, result) ||
           syscall_compat32_request_adapter_query(ctx, request, result) ||
           syscall_compat32_request_adapter_mem(ctx, request, result) ||
           syscall_compat32_request_adapter_ipc(ctx, request, result) ||
           syscall_compat32_request_adapter_gfx(ctx, request, result) ||
           syscall_compat32_request_adapter_audio(ctx, request, result) ||
           syscall_compat32_request_adapter_block(ctx, request, result) ||
           syscall_compat32_request_adapter_net(ctx, request, result) ||
           syscall_compat32_request_adapter_ui(ctx, request, result) ||
           syscall_compat32_request_adapter_misc(ctx, request, result);
}
