#include "fs/vfs_internal.h"
#include "abi/syscall_abi.h"
#include "kernel/public/proc/process.h"
#include "arch/x86/i386/syscall/compat32_internal.h"
#include "lib/string.h"

static void syscall_compat32_fill_process_info_local(
    struct syscall_process_info *out,
    const struct process_snapshot *snapshot) {
    uint32_t i;

    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (snapshot == 0) {
        return;
    }
    out->pid = snapshot->pid;
    out->slot = snapshot->slot;
    out->state = snapshot->state;
    out->exit_code = snapshot->exit_code;
    out->wake_tick = snapshot->wake_tick;
    out->image_kind = snapshot->image_kind;
    out->caps = snapshot->caps;
    out->uid = snapshot->uid;
    out->gid = snapshot->gid;
    for (i = 0u;
         i + 1u < sizeof(out->name) && snapshot->name[i] != '\0';
         i++) {
        out->name[i] = snapshot->name[i];
    }
}

uint32_t syscall_compat32_spawn(struct syscall_compat32_context *ctx,
                                 uint32_t user_command,
                                 uint32_t mode,
                                 uint32_t flags) {
    char command[512];

    if (ctx == 0 || ctx->spawn_command == 0 ||
        (mode != SYS_SPAWN_AUTO && mode != SYS_SPAWN_ELF) ||
        (flags & ~SYS_SPAWN_BACKGROUND) != 0u) {
        return (uint32_t)(int32_t)-NEX_ERR_INVAL;
    }
    if (!syscall_compat32_copy_user_cstr(command, user_command, sizeof(command))) {
        return (uint32_t)(int32_t)-NEX_ERR_INVAL;
    }
    return (uint32_t)ctx->spawn_command(command, mode, flags);
}

uint32_t syscall_compat32_exec(struct syscall_compat32_context *ctx,
                                uint32_t user_command) {
    return syscall_compat32_spawn(ctx, user_command, SYS_SPAWN_AUTO, 0u);
}

uintptr_t syscall_compat32_wait(struct syscall_compat32_context *ctx,
                                 const struct process_context *context,
                                 uint32_t pid,
                                 uint32_t user_info,
                                 int32_t *status,
                                 int *blocked) {
    struct process_snapshot snapshot;
    uintptr_t action;

    if (status != 0) {
        *status = -NEX_ERR_INVAL;
    }
    if (blocked != 0) {
        *blocked = 0;
    }
    if (ctx == 0 || ctx->wait == 0 ||
        context == 0 || status == 0 || blocked == 0) {
        return 0u;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    action = ctx->wait(context, pid, status, blocked, user_info, &snapshot);
    if (!*blocked && snapshot.pid != 0u && user_info != 0u) {
        struct syscall_process_info info;

        syscall_compat32_fill_process_info_local(&info, &snapshot);
        (void)syscall_compat32_copy_to_user(user_info, &info, sizeof(info));
        *status = 1;
    }
    return action;
}

uintptr_t syscall_compat32_exit(struct syscall_compat32_context *ctx,
                                 const struct process_context *context,
                                 int exit_code) {
    if (ctx == 0 || ctx->exit == 0 || context == 0) {
        return 0u;
    }
    syscall_compat32_cleanup_pid(ctx, ctx->pid);
    return ctx->exit(context, exit_code);
}

uintptr_t syscall_compat32_yield(struct syscall_compat32_context *ctx,
                                  const struct process_context *context) {
    if (ctx == 0 || ctx->yield == 0 || context == 0) {
        return 0u;
    }
    return ctx->yield(context);
}

uintptr_t syscall_compat32_sleep(struct syscall_compat32_context *ctx,
                                  const struct process_context *context,
                                  uint32_t ticks) {
    if (ctx == 0 || ctx->sleep == 0 || context == 0) {
        return 0u;
    }
    return ctx->sleep(context, ticks);
}
