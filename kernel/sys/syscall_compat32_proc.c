#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/proc/process.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"

static void compat32_fill_process_info(struct syscall_process_info *out,
                                        const struct process_snapshot *snapshot) {
    uint32_t i;

    if (out == 0) {
        return;
    }
    out->pid = 0u;
    out->slot = 0u;
    out->state = PROCESS_STATE_FREE;
    out->exit_code = 0;
    out->wake_tick = 0u;
    out->image_kind = PROCESS_IMAGE_NONE;
    for (i = 0u; i < sizeof(out->name); i++) {
        out->name[i] = '\0';
    }
    if (snapshot == 0) {
        return;
    }
    out->pid = snapshot->pid;
    out->slot = snapshot->slot;
    out->state = snapshot->state;
    out->exit_code = snapshot->exit_code;
    out->wake_tick = snapshot->wake_tick;
    out->image_kind = snapshot->image_kind;
    for (i = 0u;
         i + 1u < sizeof(out->name) && snapshot->name[i] != '\0';
         i++) {
        out->name[i] = snapshot->name[i];
    }
}

uint32_t syscall_compat32_proc_query(struct syscall_compat32_context *ctx,
                                      uint32_t kind,
                                      uint32_t index,
                                      uint32_t user_info) {
    struct process_snapshot snapshot;
    struct syscall_process_info info = {0};

    if (ctx == 0 || ctx->process_snapshot == 0 ||
        kind != SYS_PROC_QUERY_ALL ||
        !ctx->process_snapshot(index, &snapshot)) {
        return 0u;
    }
    compat32_fill_process_info(&info, &snapshot);
    return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
}

uint32_t syscall_compat32_kill(struct syscall_compat32_context *ctx,
                                uint32_t pid) {
    uint32_t result;

    if (ctx == 0 || ctx->kill == 0) {
        return (uint32_t)-1;
    }
    result = (uint32_t)ctx->kill(pid);
    if (result == 1u) {
        syscall_compat32_cleanup_pid(ctx, pid);
    }
    return result;
}
