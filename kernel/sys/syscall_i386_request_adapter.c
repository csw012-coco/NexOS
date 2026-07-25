#include "block/blockdev.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/system_query_internal.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "kernel/public/proc/process_user_backend.h"
#include "kernel/public/sys/syscall_i386.h"
#include "drivers/audio/audio.h"
#include "lib/string.h"

/*
 * i386 native syscall adapter.
 *
 * The int 0x40 entry path decodes 32-bit registers into a
 * kernel_syscall_request, then this layer handles the i386 ABI details that
 * still need 32-bit user pointer copying or per-process i386 state. Keep
 * shared syscall semantics moving outward into kernel/sys common handlers.
 */

extern int job_current_process_foreground_allowed(void) __attribute__((weak));

int syscall_i386_request_adapter_io(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_OPEN:
            result->value = syscall_compat32_open(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_READ:
            result->value = syscall_compat32_read(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2),
                kernel_syscall_arg_u32(request, 3));
            return 1;
        case SYS_WRITE:
            result->value = syscall_compat32_write(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_CLOSE:
            result->value = syscall_compat32_close(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_DUP2:
            result->value = syscall_compat32_dup2(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_PIPE:
            result->value = syscall_compat32_pipe(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SEEK:
            result->value = syscall_compat32_seek(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_i32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        default:
            return 0;
    }
}

int syscall_i386_request_adapter_fs(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MKDIR:
            result->value = syscall_compat32_mkdir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_RMDIR:
            result->value = syscall_compat32_rmdir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_REMOVE:
            result->value = syscall_compat32_remove(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_CHDIR:
            result->value = syscall_compat32_chdir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_GETCWD:
            result->value = syscall_compat32_getcwd(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_OPENDIR:
            result->value = syscall_compat32_opendir(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_READDIR:
            result->value = syscall_compat32_readdir(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_SWITCH_ROOT:
            result->value = (uint32_t)-1;
            return 1;
        default:
            return 0;
    }
}

int syscall_i386_request_adapter_proc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_EXIT:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->action = SYSCALL_RESULT_EXIT;
            return 1;
        case SYS_YIELD:
            result->value = 0u;
            result->action = SYSCALL_RESULT_YIELD;
            return 1;
        case SYS_EXEC_REPLACE:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->action = SYSCALL_RESULT_EXEC;
            return 1;
        case SYS_EXEC:
            result->value = syscall_compat32_exec(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_FORK: {
            uint32_t child_pid = 0u;

            if (process_user_fork_from_user(ctx->process_context, &child_pid) < 0) {
                result->value = (uint32_t)-1;
                return 1;
            }
            result->value = child_pid;
            return 1;
        }
        case SYS_WAIT:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->extra = kernel_syscall_arg_u32(request, 1);
            result->action = SYSCALL_RESULT_WAIT;
            return 1;
        case SYS_SLEEP:
            result->value = kernel_syscall_arg_u32(request, 0);
            result->action = SYSCALL_RESULT_SLEEP;
            return 1;
        case SYS_GETPID:
            result->value = ctx->pid;
            return 1;
        case SYS_PROC_QUERY:
            result->value = syscall_compat32_proc_query(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_KILL:
            result->value = syscall_compat32_kill(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_FG:
            result->value = syscall_compat32_fg(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_BG:
            result->value = syscall_compat32_bg(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_SPAWN:
            result->value = syscall_compat32_spawn(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        default:
            return 0;
    }
}

int syscall_i386_request_adapter_mount(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    switch (request->number) {
        case SYS_MOUNT:
            result->value = syscall_compat32_mount(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_UMOUNT:
            result->value = syscall_compat32_umount(
                ctx,
                kernel_syscall_arg_u32(request, 0));
            return 1;
        default:
            return 0;
    }
}

int syscall_i386_dispatch_request(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    (void)process_current_session();
    return syscall_i386_request_adapter_io(ctx, request, result) ||
           syscall_i386_request_adapter_fs(ctx, request, result) ||
           syscall_i386_request_adapter_proc(ctx, request, result) ||
           syscall_i386_request_adapter_mount(ctx, request, result) ||
           syscall_i386_request_adapter_query(ctx, request, result) ||
           syscall_i386_request_adapter_mem(ctx, request, result) ||
           syscall_i386_request_adapter_ipc(ctx, request, result) ||
           syscall_i386_request_adapter_misc(ctx, request, result);
}

int syscall_compat32_dispatch_request(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    return syscall_i386_dispatch_request(ctx, request, result);
}
