#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

enum {
    COMPAT32_RTL8139_TX_BUFFER_MAX = 1600u
};

static uint8_t g_compat32_rtl8139_tx_buffer[COMPAT32_RTL8139_TX_BUFFER_MAX];

int syscall_compat32_request_adapter_net(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;

    (void)ctx;
    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    syscall_compat32_user_copy_ops(&copy_ops, 1, 1, 0, (uint64_t)(uint32_t)-1);
    if (syscall_common_request_core_rtl8139_request(
            request,
            result,
            &copy_ops,
            g_compat32_rtl8139_tx_buffer,
            sizeof(g_compat32_rtl8139_tx_buffer),
            0xffffffffu)) {
        return 1;
    }
    switch (request->number) {
        case SYS_RTL8139_TX_TEST:
            return syscall_common_request_core_backend(request, result);
        default:
            return 0;
    }
}
