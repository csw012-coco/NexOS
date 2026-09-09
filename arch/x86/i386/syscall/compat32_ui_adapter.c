#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/process_scheduler_ops.h"

static char g_compat32_clipboard_buffer[KERNEL_CLIPBOARD_TEXT_MAX + 1u];

static void syscall_compat32_clipboard_transfer_ops(
    struct syscall_common_clipboard_transfer_ops *ops) {
    if (ops == 0) {
        return;
    }
    ops->copy_from_user = syscall_compat32_copy_from_user;
    ops->copy_to_user = syscall_compat32_copy_to_user;
    ops->bad_pointer = 0;
    ops->bad_pointer_value = (uint64_t)(uint32_t)-1;
}

static int syscall_compat32_clipboard_request(
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_clipboard_transfer_ops ops;

    syscall_compat32_clipboard_transfer_ops(&ops);
    return syscall_common_request_core_clipboard_transfer_request(
        request,
        result,
        &ops,
        g_compat32_clipboard_buffer,
        sizeof(g_compat32_clipboard_buffer));
}

int syscall_compat32_request_adapter_ui(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;

    (void)ctx;
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    syscall_compat32_user_copy_ops(
        &copy_ops, 1, 1, 0, (uint64_t)(uint32_t)-1);
    switch (request->number) {
        case SYS_GUI_EVENT:
            return syscall_common_request_core_gui_event_transfer_request(
                    request,
                    result,
                    &copy_ops,
                    process_scheduler_current_pid(),
                    job_current_process_foreground_allowed());
        case SYS_CLIPBOARD:
            return syscall_compat32_clipboard_request(request, result);
        default:
            return 0;
    }
}
