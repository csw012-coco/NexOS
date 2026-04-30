#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/system_query_internal.h"

enum {
    SYSCALL_AUDIO_BUFFER_MAX = 131072u
};

static uint8_t g_syscall_audio_buffer[SYSCALL_AUDIO_BUFFER_MAX];

uint64_t syscall_handle_audio_query(uint32_t index, uint64_t user_info_addr) {
    struct syscall_audio_info info;

    if (!syscall_user_writable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    {
        int present = kernel_query_audio_info(index, &info);

        if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
            return syscall_kill_bad_user_pointer();
        }
        return present ? 1u : 0u;
    }
}

uint64_t syscall_handle_audio_tone(uint32_t index, uint32_t hz, uint32_t duration_ms) {
    return kernel_audio_play_tone(index, hz, duration_ms) ? 1u : 0u;
}

uint64_t syscall_handle_audio_play(uint32_t index, uint64_t user_info_addr) {
    struct syscall_audio_play_info info;

    if (!syscall_user_readable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (info.bytes == 0u || info.bytes > SYSCALL_AUDIO_BUFFER_MAX) {
        return 0u;
    }
    if (info.channels == 0u || info.bits_per_sample == 0u || info.sample_rate == 0u) {
        return 0u;
    }
    if (!syscall_user_readable(info.data_addr, info.bytes)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(g_syscall_audio_buffer, info.data_addr, info.bytes)) {
        return syscall_kill_bad_user_pointer();
    }
    return kernel_audio_play_buffer(index, &info, g_syscall_audio_buffer)
               ? 1u
               : 0u;
}
