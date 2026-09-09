#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"

enum {
    COMPAT32_AUDIO_BUFFER_MAX = 65536u
};

static uint8_t g_compat32_audio_buffer[COMPAT32_AUDIO_BUFFER_MAX];

int syscall_compat32_request_adapter_audio(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    syscall_compat32_user_copy_ops(&copy_ops, 1, 0, 0, (uint64_t)(uint32_t)-1);
    if (syscall_common_request_core_audio_play_request(
            request,
            result,
            &copy_ops,
            g_compat32_audio_buffer,
            sizeof(g_compat32_audio_buffer),
            0xffffffffu)) {
        return 1;
    }
    switch (request->number) {
        case SYS_AUDIO_TONE:
            return syscall_common_request_core_backend(request, result);
        case SYS_AUDIO_PLAY_FD:
            return syscall_compat32_audio_fd_request(
                ctx, request, result, &copy_ops);
        default:
            return 0;
    }
}
