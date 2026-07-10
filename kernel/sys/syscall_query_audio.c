#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/runtime_internal.h"
#include "kernel/internal/core/system_query_internal.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "drivers/audio/audio.h"

enum {
    SYSCALL_AUDIO_BUFFER_MAX = 1048576u
};

static uint8_t g_syscall_audio_buffer[SYSCALL_AUDIO_BUFFER_MAX];

struct syscall_audio_tone_call {
    uint32_t index;
    uint32_t hz;
    uint32_t duration_ms;
};

static int syscall_audio_play_tone_local(void *ctx) {
    const struct syscall_audio_tone_call *call =
        (const struct syscall_audio_tone_call *)ctx;

    return kernel_audio_play_tone(call->index, call->hz, call->duration_ms);
}

struct syscall_audio_buffer_call {
    uint32_t index;
    const struct syscall_audio_play_info *info;
    const uint8_t *buffer;
};

static int syscall_audio_play_buffer_local(void *ctx) {
    const struct syscall_audio_buffer_call *call =
        (const struct syscall_audio_buffer_call *)ctx;

    return kernel_audio_play_buffer(call->index, call->info, call->buffer);
}

struct syscall_audio_stream_call {
    uint32_t index;
    struct audio_pcm_stream *stream;
};

static int syscall_audio_play_stream_local(void *ctx) {
    const struct syscall_audio_stream_call *call =
        (const struct syscall_audio_stream_call *)ctx;

    return kernel_audio_play_stream(call->index, call->stream);
}

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

    {
        int present = kernel_query_audio_info(index, &info);

        if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
            return syscall_kill_bad_user_pointer();
        }
        return present ? 1u : 0u;
    }
}

uint64_t syscall_handle_audio_tone(uint32_t index, uint32_t hz, uint32_t duration_ms) {
    struct syscall_audio_tone_call call;

    call.index = index;
    call.hz = hz;
    call.duration_ms = duration_ms;
    return kernel_runtime_run_with_irqs_enabled(syscall_audio_play_tone_local, &call) ? 1u : 0u;
}

uint64_t syscall_handle_audio_play(uint32_t index, uint64_t user_info_addr) {
    struct syscall_audio_play_info info;
    struct syscall_audio_buffer_call call;

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
    if (!syscall_copy_from_user(g_syscall_audio_buffer, info.data_addr, info.bytes)) {
        return syscall_kill_bad_user_pointer();
    }
    call.index = index;
    call.info = &info;
    call.buffer = g_syscall_audio_buffer;
    return kernel_runtime_run_with_irqs_enabled(syscall_audio_play_buffer_local, &call) ? 1u : 0u;
}

uint64_t syscall_handle_audio_play_fd(uint32_t index, uint64_t user_info_addr) {
    struct syscall_audio_stream_info info;
    struct syscall_audio_fd_stream fd_stream;
    struct audio_pcm_stream stream;
    struct syscall_audio_stream_call call;

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

    call.index = index;
    call.stream = &stream;
    return kernel_runtime_run_with_irqs_enabled(syscall_audio_play_stream_local, &call) ? 1u : 0u;
}
