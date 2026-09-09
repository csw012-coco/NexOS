#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/core/system_power_internal.h"
#include "kernel/public/core/tty.h"

static uint64_t syscall_compat32_misc_clear(void *ctx_ptr) {
    struct syscall_compat32_context *ctx =
        (struct syscall_compat32_context *)ctx_ptr;

    if (ctx != 0 && ctx->tty != 0) {
        tty_clear(ctx->tty);
    }
    return 0u;
}

static uint64_t syscall_compat32_misc_ticks(void *ctx_ptr) {
    const struct syscall_compat32_context *ctx =
        (const struct syscall_compat32_context *)ctx_ptr;

    return ctx != 0 ? ctx->ticks : 0u;
}

static uint64_t syscall_compat32_misc_reboot(void *ctx_ptr) {
    (void)ctx_ptr;
    return (uint64_t)(uint32_t)-1;
}

static uint64_t syscall_compat32_misc_poweroff(void *ctx_ptr) {
    (void)ctx_ptr;
    return kernel_poweroff();
}

int syscall_compat32_request_adapter_misc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_misc_ops misc_ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    misc_ops.clear = syscall_compat32_misc_clear;
    misc_ops.ticks = syscall_compat32_misc_ticks;
    misc_ops.reboot = syscall_compat32_misc_reboot;
    misc_ops.poweroff = syscall_compat32_misc_poweroff;
    misc_ops.ctx = ctx;
    if (syscall_common_request_core_misc_request(
            request, result, &misc_ops)) {
        return 1;
    }
    if (request->number == SYS_CAPABILITY) {
        struct syscall_common_user_copy_ops ops;

        syscall_compat32_user_copy_ops(
            &ops, 1, 0, 1, (uint64_t)(uint32_t)-1);
        return syscall_common_request_core_capability_request(
            request, result, &ops);
    }
    if (request->number == SYS_IDENTITY) {
        struct syscall_common_user_copy_ops ops;

        syscall_compat32_user_copy_ops(
            &ops, 1, 1, 1, (uint64_t)(uint32_t)-1);
        return syscall_common_request_core_identity_request(
            request, result, &ops);
    }
    if (request->number == SYS_CAPABILITY_EVENT) {
        struct syscall_common_user_input_ops ops;

        ops.copy_from_user = syscall_compat32_copy_from_user;
        ops.bad_pointer = 0;
        ops.bad_pointer_value = (uint64_t)(uint32_t)-1;
        return syscall_common_request_core_capability_event_request(
            request, result, &ops);
    }
    return 0;
}
