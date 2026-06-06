#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/system_query_internal.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "drivers/audio/audio.h"

enum {
    SYSCALL_AUDIO_BUFFER_MAX = 1048576u
};

static uint8_t g_syscall_audio_buffer[SYSCALL_AUDIO_BUFFER_MAX];

struct syscall_audio_fd_stream {
    struct process *proc;
    uint32_t fd;
    uint32_t remaining;
};

static uint32_t syscall_audio_fd_read_local(void *ctx, void *buffer, uint32_t bytes) {
    struct syscall_audio_fd_stream *stream = (struct syscall_audio_fd_stream *)ctx;
    uint32_t copied = 0;
    uint32_t want;
    uint64_t rc;

    if (stream == 0 || buffer == 0 || bytes == 0u || stream->remaining == 0u ||
        stream->proc == 0 || stream->proc->state == PROCESS_STATE_EXITED ||
        stream->proc->state == PROCESS_STATE_STOPPED) {
        return 0u;
    }
    want = bytes > stream->remaining ? stream->remaining : bytes;
    rc = fs_service_read(stream->proc,
                         g_syscall_vfs,
                         stream->fd,
                         buffer,
                         want,
                         SYS_READ_BLOCKING,
                         &copied);
    if (rc == (uint64_t)-1 || copied == 0u) {
        return 0u;
    }
    if (copied > stream->remaining) {
        copied = stream->remaining;
    }
    stream->remaining -= copied;
    return copied;
}

static uint32_t syscall_audio_fd_cancelled_local(void *ctx) {
    struct syscall_audio_fd_stream *stream = (struct syscall_audio_fd_stream *)ctx;

    return stream == 0 ||
           stream->proc == 0 ||
           stream->proc->state == PROCESS_STATE_EXITED ||
           stream->proc->state == PROCESS_STATE_STOPPED;
}

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
    if ((info.flags & ~SYS_AUDIO_PLAY_F_ASYNC) != 0u) {
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

uint64_t syscall_handle_audio_play_fd(uint32_t index, uint64_t user_info_addr) {
    struct syscall_audio_stream_info info;
    struct syscall_audio_fd_stream fd_stream;
    struct audio_pcm_stream stream;

    if (!syscall_user_readable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (info.data_bytes == 0u) {
        return 0u;
    }
    if (info.channels == 0u || info.bits_per_sample == 0u || info.sample_rate == 0u) {
        return 0u;
    }
    if ((info.flags & ~SYS_AUDIO_PLAY_F_ASYNC) != 0u) {
        return 0u;
    }

    fd_stream.proc = process_current_mut();
    fd_stream.fd = info.fd;
    fd_stream.remaining = info.data_bytes;

    stream.sample_rate = info.sample_rate;
    stream.channels = info.channels;
    stream.bits_per_sample = info.bits_per_sample;
    stream.data_bytes = info.data_bytes;
    stream.flags = info.flags;
    stream.ctx = &fd_stream;
    stream.read = syscall_audio_fd_read_local;
    stream.cancelled = syscall_audio_fd_cancelled_local;

    return kernel_audio_play_stream(index, &stream) ? 1u : 0u;
}
