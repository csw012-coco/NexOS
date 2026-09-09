#include "kernel/internal/proc/process_types_internal.h"
#include "arch/x86/i386/syscall/compat32_internal.h"

static int syscall_compat32_pid_exists(struct syscall_compat32_context *ctx,
                                       uint32_t pid) {
    struct process_snapshot snapshot;

    if (ctx == 0 || ctx->process_snapshot == 0 || pid == 0u) {
        return 0;
    }
    for (uint32_t slot = 0u; slot < NOS_PROCESS_SLOT_MAX; slot++) {
        if (ctx->process_snapshot(slot, &snapshot) &&
            snapshot.pid == pid &&
            snapshot.state != PROCESS_STATE_FREE) {
            return 1;
        }
    }
    return 0;
}

uint32_t syscall_compat32_fg(struct syscall_compat32_context *ctx,
                             uint32_t pid) {
    if (ctx == 0) {
        return (uint32_t)(int32_t)-NEX_ERR_INVAL;
    }
    if (!syscall_compat32_pid_exists(ctx, pid)) {
        return (uint32_t)(int32_t)(pid == 0u ? -NEX_ERR_INVAL : -NEX_ERR_SRCH);
    }
    return 1u;
}

uint32_t syscall_compat32_bg(struct syscall_compat32_context *ctx,
                             uint32_t pid) {
    if (ctx == 0) {
        return (uint32_t)(int32_t)-NEX_ERR_INVAL;
    }
    if (!syscall_compat32_pid_exists(ctx, pid)) {
        return (uint32_t)(int32_t)(pid == 0u ? -NEX_ERR_INVAL : -NEX_ERR_SRCH);
    }
    return 1u;
}
