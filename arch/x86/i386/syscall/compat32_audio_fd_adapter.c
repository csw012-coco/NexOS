#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/proc/process.h"

struct syscall_compat32_audio_fd_stream {
    struct syscall_compat32_context *syscall_ctx;
    struct process *proc;
    uint32_t fd;
    uint32_t remaining;
};

static uint32_t syscall_compat32_audio_fd_read(
    void *ctx,
    void *buffer,
    uint32_t bytes) {
    struct syscall_compat32_audio_fd_stream *stream =
        (struct syscall_compat32_audio_fd_stream *)ctx;
    uint32_t copied;
    uint32_t want;
    uint64_t rc;

    if (stream == 0 || buffer == 0 || bytes == 0u ||
        stream->remaining == 0u ||
        stream->syscall_ctx == 0 ||
        stream->proc == 0 ||
        stream->proc->state == PROCESS_STATE_EXITED ||
        stream->proc->state == PROCESS_STATE_STOPPED) {
        return 0u;
    }
    want = bytes > stream->remaining ? stream->remaining : bytes;
    if (want == 0u) {
        return 0u;
    }
    rc = fs_service_read(stream->proc,
                         stream->syscall_ctx->vfs,
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

static uint32_t syscall_compat32_audio_fd_cancelled(void *ctx) {
    struct syscall_compat32_audio_fd_stream *stream =
        (struct syscall_compat32_audio_fd_stream *)ctx;

    return stream == 0 ||
           stream->proc == 0 ||
           stream->proc->state == PROCESS_STATE_EXITED ||
           stream->proc->state == PROCESS_STATE_STOPPED;
}

static void syscall_compat32_audio_fd_prepare(
    void *ctx,
    const struct syscall_audio_stream_info *info) {
    struct syscall_compat32_audio_fd_stream *stream =
        (struct syscall_compat32_audio_fd_stream *)ctx;

    if (stream == 0 || info == 0) {
        return;
    }
    stream->fd = info->fd;
    stream->remaining = info->data_bytes;
}

int syscall_compat32_audio_fd_request(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result,
    const struct syscall_common_user_copy_ops *copy_ops) {
    struct syscall_compat32_audio_fd_stream fd_stream;

    if (ctx == 0 || request == 0 || result == 0 || copy_ops == 0) {
        return 0;
    }
    fd_stream.syscall_ctx = ctx;
    fd_stream.proc = process_current_mut();
    fd_stream.fd = 0u;
    fd_stream.remaining = 0u;

    return syscall_common_request_core_audio_stream_request(
        request,
        result,
        copy_ops,
        &fd_stream,
        syscall_compat32_audio_fd_prepare,
        syscall_compat32_audio_fd_read,
        syscall_compat32_audio_fd_cancelled);
}
