#include "kernel/internal/core/tty_internal.h"
#include "arch/x86/i386/services/shared_services.h"
#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/arch/arch_ops.h"

static void syscall_compat32_file_drain_tty_input(void *ctx_ptr, struct tty *tty) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;
    struct keyboard_event event;

    if (ctx == 0 || tty == 0 || ctx->pop_keyboard_event == 0) {
        return;
    }
    while (ctx->pop_keyboard_event(&event)) {
        struct tty *target_tty = shared_services_active_tty();

        if (target_tty == 0) {
            target_tty = tty;
        }
        tty_feed_key_event(target_tty, &event);
    }
}

static void syscall_compat32_file_wait_for_interrupt(void *ctx_ptr) {
    (void)ctx_ptr;
    if (arch != 0 && arch->wait_for_interrupt != 0) {
        arch->wait_for_interrupt();
    }
}

void syscall_compat32_file_copy_ops(struct syscall_common_user_copy_ops *ops) {
    syscall_compat32_user_copy_ops(ops, 1, 1, 1, (uint64_t)(uint32_t)-1);
}

void syscall_compat32_file_io_ops(struct syscall_compat32_context *ctx,
                              struct syscall_common_file_io_ops *ops) {
    if (ops == 0) {
        return;
    }
    ops->io_buffer = ctx != 0 ? ctx->io_buffer : 0;
    ops->io_buffer_size = ctx != 0 ? ctx->io_buffer_size : 0u;
    ops->tty = ctx != 0 ? ctx->tty : 0;
    ops->drain_tty_input = syscall_compat32_file_drain_tty_input;
    ops->wait_for_interrupt = syscall_compat32_file_wait_for_interrupt;
    ops->ctx = ctx;
}
