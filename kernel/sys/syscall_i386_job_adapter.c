#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/sys/syscall_i386.h"

/*
 * i386 foreground/background compatibility helpers.
 *
 * The common job-control path is preferred when present. The fallback keeps
 * older i386 boot shells working by manipulating the tty foreground pid.
 */

extern uint64_t syscall_handle_fg(uint32_t pid) __attribute__((weak));
extern uint64_t syscall_handle_bg(uint32_t pid) __attribute__((weak));

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
    if (syscall_handle_fg != 0) {
        return (uint32_t)syscall_handle_fg(pid);
    }
    if (ctx == 0 || ctx->tty == 0 || !syscall_compat32_pid_exists(ctx, pid)) {
        return 0u;
    }
    tty_set_foreground_pid(ctx->tty, pid);
    return 1u;
}

uint32_t syscall_compat32_bg(struct syscall_compat32_context *ctx,
                             uint32_t pid) {
    if (syscall_handle_bg != 0) {
        return (uint32_t)syscall_handle_bg(pid);
    }
    if (ctx == 0 || ctx->tty == 0 || pid == 0u) {
        return 0u;
    }
    tty_clear_foreground_pid(ctx->tty, pid);
    return 1u;
}
