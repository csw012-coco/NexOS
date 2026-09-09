#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/process_user_backend.h"
#include "abi/syscall_abi.h"

static uint64_t syscall_compat32_proc_adapter_exec(void *ctx_ptr,
                                                   uint64_t user_command,
                                                   uint64_t user_envp) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    (void)user_envp;
    if (user_command > 0xffffffffu) {
        return (uint64_t)(uint32_t)(int32_t)-NEX_ERR_INVAL;
    }
    return syscall_compat32_exec(ctx, (uint32_t)user_command);
}

static uint64_t syscall_compat32_proc_adapter_fork(void *ctx_ptr) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;
    uint32_t child_pid = 0u;

    if (ctx == 0 ||
        process_user_fork_from_user(ctx->process_context, &child_pid) < 0) {
        return (uint64_t)(uint32_t)-1;
    }
    return child_pid;
}

static uint64_t syscall_compat32_proc_adapter_getpid(void *ctx_ptr) {
    const struct syscall_compat32_context *ctx =
        (const struct syscall_compat32_context *)ctx_ptr;

    return ctx != 0 ? ctx->pid : 0u;
}

static uint64_t syscall_compat32_proc_adapter_query(void *ctx_ptr,
                                                    uint32_t kind,
                                                    uint32_t index,
                                                    uint64_t user_info_addr) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (user_info_addr > 0xffffffffu) {
        return 0u;
    }
    return syscall_compat32_proc_query(
        ctx, kind, index, (uint32_t)user_info_addr);
}

static uint64_t syscall_compat32_proc_adapter_kill(void *ctx_ptr, uint32_t pid) {
    return syscall_compat32_kill(
        (struct syscall_compat32_context *)ctx_ptr, pid);
}

static uint64_t syscall_compat32_proc_adapter_fg(void *ctx_ptr, uint32_t pid) {
    struct process *proc = process_current_mut();
    uint32_t rc = syscall_compat32_fg(
        (struct syscall_compat32_context *)ctx_ptr, pid);

    if ((int32_t)rc > 0 && proc != 0 && proc->console_handle != 0) {
        tty_set_foreground_pid((struct tty *)proc->console_handle, pid);
    }
    return rc;
}

static uint64_t syscall_compat32_proc_adapter_bg(void *ctx_ptr, uint32_t pid) {
    return syscall_compat32_bg(
        (struct syscall_compat32_context *)ctx_ptr, pid);
}

static uint64_t syscall_compat32_proc_adapter_tty_claim(void *ctx_ptr) {
    struct process *proc = process_current_mut();

    (void)ctx_ptr;
    if (proc == 0 || proc->pid == 0u || proc->console_handle == 0) {
        return (uint64_t)(uint32_t)(int32_t)-NEX_ERR_INVAL;
    }
    tty_set_foreground_pid((struct tty *)proc->console_handle, proc->pid);
    return 1u;
}

static uint64_t syscall_compat32_proc_adapter_spawn(void *ctx_ptr,
                                                    uint64_t user_command,
                                                    uint32_t mode,
                                                    uint32_t flags,
                                                    uint64_t user_envp) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    (void)user_envp;
    if (user_command > 0xffffffffu) {
        return (uint64_t)(uint32_t)(int32_t)-NEX_ERR_INVAL;
    }
    return syscall_compat32_spawn(ctx, (uint32_t)user_command, mode, flags);
}

int syscall_compat32_request_adapter_proc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    const struct syscall_common_proc_ops ops = {
        .exec = syscall_compat32_proc_adapter_exec,
        .fork = syscall_compat32_proc_adapter_fork,
        .getpid = syscall_compat32_proc_adapter_getpid,
        .proc_query = syscall_compat32_proc_adapter_query,
        .kill = syscall_compat32_proc_adapter_kill,
        .fg = syscall_compat32_proc_adapter_fg,
        .bg = syscall_compat32_proc_adapter_bg,
        .tty_claim = syscall_compat32_proc_adapter_tty_claim,
        .spawn = syscall_compat32_proc_adapter_spawn,
        .ctx = ctx
    };

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    return syscall_common_request_core_proc_lifecycle_request(
        request, result, &ops);
}
