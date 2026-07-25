#include "block/blockdev.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "kernel/public/sys/syscall_i386.h"
#include "drivers/audio/audio.h"
#include "lib/string.h"

/*
 * i386 device/UI syscall ABI adapter.
 *
 * Keeps 32-bit user copy and chunking for block, clipboard, gfx, GUI events,
 * RTL8139 and audio stream syscalls outside the native int 0x40 request
 * adapter. Common validation and backend dispatch still live in syscall common
 * helpers.
 */

extern int job_current_process_foreground_allowed(void) __attribute__((weak));

enum {
    COMPAT32_GFX_BATCH_CHUNK = 16u,
    COMPAT32_GFX_BLIT_MAX_DIMENSION = 8192u,
    COMPAT32_AUDIO_BUFFER_MAX = 65536u,
    COMPAT32_RTL8139_TX_BUFFER_MAX = 1600u
};

static char g_compat32_clipboard_buffer[KERNEL_CLIPBOARD_TEXT_MAX + 1u];
static struct syscall_gfx_batch_entry g_compat32_gfx_batch_entries[COMPAT32_GFX_BATCH_CHUNK];
static uint8_t g_compat32_gfx_blit_buffer[SYSCALL_COPY_CHUNK] __attribute__((aligned(sizeof(uint32_t))));
static uint8_t g_compat32_audio_buffer[COMPAT32_AUDIO_BUFFER_MAX];
static uint8_t g_compat32_rtl8139_tx_buffer[COMPAT32_RTL8139_TX_BUFFER_MAX];

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

static uint32_t syscall_compat32_block_read(uint32_t disk_index,
                                             uint64_t lba,
                                             uint32_t user_info) {
    struct syscall_block_read_info info;

    if (!syscall_common_request_core_block_read_dispatch(disk_index, lba, &info)) {
        return 0u;
    }
    return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
}

static uint32_t syscall_compat32_block_write(uint32_t disk_index,
                                              uint64_t lba,
                                              uint32_t user_info) {
    struct syscall_block_write_info info;

    if (!arch_copy_from_user(&info, user_info, sizeof(info))) {
        return 0u;
    }
    if (!syscall_common_request_core_block_write_dispatch(disk_index, lba, &info)) {
        (void)arch_copy_to_user(user_info, &info, sizeof(info));
        return 0u;
    }
    return arch_copy_to_user(user_info, &info, sizeof(info)) ? 1u : 0u;
}

static int syscall_compat32_clipboard_copy_from_user(void *dest,
                                                     uint64_t user_addr,
                                                     uint32_t size) {
    if (user_addr > 0xffffffffu) {
        return 0;
    }
    return arch_copy_from_user(dest, (uint32_t)user_addr, size);
}

static int syscall_compat32_clipboard_copy_to_user(uint64_t user_addr,
                                                   const void *src,
                                                   uint32_t size) {
    if (user_addr > 0xffffffffu) {
        return 0;
    }
    return arch_copy_to_user((uint32_t)user_addr, src, size);
}

static uint32_t syscall_compat32_clipboard(uint32_t op, uint32_t user_info) {
    struct syscall_common_clipboard_transfer_ops ops;

    ops.copy_from_user = syscall_compat32_clipboard_copy_from_user;
    ops.copy_to_user = syscall_compat32_clipboard_copy_to_user;
    ops.bad_pointer = 0;
    ops.bad_pointer_value = (uint64_t)(uint32_t)-1;
    return (uint32_t)syscall_common_request_core_clipboard_transfer(
        op,
        user_info,
        &ops,
        g_compat32_clipboard_buffer,
        sizeof(g_compat32_clipboard_buffer));
}

static uint32_t syscall_compat32_gfx_batch(uint32_t user_info) {
    struct syscall_gfx_batch batch;
    uint32_t processed = 0u;
    int valid = 1;

    if (!arch_copy_from_user(&batch, user_info, sizeof(batch)) ||
        (batch.count != 0u && batch.entries_addr > 0xffffffffu) ||
        !syscall_common_request_core_gfx_batch_valid(&batch)) {
        return (uint32_t)-1;
    }

    kernel_gfx_begin_batch();
    while (processed < batch.count) {
        uint32_t count = batch.count - processed;
        uint32_t bytes;
        uint32_t entries_addr;

        if (count > COMPAT32_GFX_BATCH_CHUNK) {
            count = COMPAT32_GFX_BATCH_CHUNK;
        }
        bytes = count * sizeof(struct syscall_gfx_batch_entry);
        entries_addr = (uint32_t)batch.entries_addr +
            processed * sizeof(struct syscall_gfx_batch_entry);
        if (!arch_copy_from_user(g_compat32_gfx_batch_entries, entries_addr, bytes)) {
            valid = 0;
            break;
        }
        valid = syscall_common_request_core_gfx_batch_dispatch(
            g_compat32_gfx_batch_entries, count, 0);
        if (!valid) {
            break;
        }
        processed += count;
    }
    kernel_gfx_end_batch(valid ? batch.flags : 0u);
    return valid ? 0u : (uint32_t)-1;
}

static uint32_t syscall_compat32_gfx_blit(uint32_t user_info) {
    struct syscall_gfx_blit blit;
    struct syscall_common_gfx_blit_plan plan;
    int noop = 0;

    if (!arch_copy_from_user(&blit, user_info, sizeof(blit)) ||
        !syscall_common_request_core_gfx_blit_plan(
            &blit,
            COMPAT32_GFX_BLIT_MAX_DIMENSION,
            0xffffffffu,
            &plan,
            &noop)) {
        return (uint32_t)-1;
    }
    if (noop) {
        return 0u;
    }

    for (uint32_t x = 0u; x < plan.visible_width;) {
        uint32_t chunk_width = plan.visible_width - x;
        uint32_t row_bytes;
        uint32_t rows_per_chunk;

        if (chunk_width > SYSCALL_COPY_CHUNK / sizeof(uint32_t)) {
            chunk_width = SYSCALL_COPY_CHUNK / sizeof(uint32_t);
        }
        row_bytes = chunk_width * sizeof(uint32_t);
        rows_per_chunk = SYSCALL_COPY_CHUNK / row_bytes;
        for (uint32_t y = 0u; y < plan.visible_height;) {
            uint32_t row_count = plan.visible_height - y;

            if (row_count > rows_per_chunk) {
                row_count = rows_per_chunk;
            }
            for (uint32_t row = 0u; row < row_count; row++) {
                uint64_t row_addr = plan.first_addr +
                                    (uint64_t)(y + row) * plan.pitch +
                                    (uint64_t)x * sizeof(uint32_t);

                if (row_addr > 0xffffffffu ||
                    !arch_copy_from_user(&g_compat32_gfx_blit_buffer[row * row_bytes],
                                         (uint32_t)row_addr,
                                         row_bytes)) {
                    return (uint32_t)-1;
                }
            }
            if (!syscall_common_request_core_gfx_blit_dispatch(
                    (const uint32_t *)g_compat32_gfx_blit_buffer,
                    row_bytes,
                    chunk_width,
                    row_count,
                    plan.dst_x + (int32_t)x,
                    plan.dst_y + (int32_t)y)) {
                return (uint32_t)-1;
            }
            y += row_count;
        }
        x += chunk_width;
    }
    return 0u;
}

static uint32_t syscall_compat32_gfx(uint32_t op, uint32_t user_info) {
    struct syscall_gfx_command cmd;
    struct syscall_gfx_info info;
    enum kernel_gfx_buffer_kind buffer_kind;

    if (op == SYS_GFX_BATCH) {
        return syscall_compat32_gfx_batch(user_info);
    }
    if (op == SYS_GFX_BLIT) {
        return syscall_compat32_gfx_blit(user_info);
    }

    buffer_kind = kernel_gfx_buffer_kind(op);
    switch (buffer_kind) {
        case KERNEL_GFX_BUFFER_INFO_OUT:
            memset(&info, 0, sizeof(info));
            if (!syscall_common_request_core_gfx_info(op, &info)) {
                return (uint32_t)-1;
            }
            return arch_copy_to_user(user_info, &info, sizeof(info))
                ? 0u
                : (uint32_t)-1;
        case KERNEL_GFX_BUFFER_COMMAND_IN:
            if (!arch_copy_from_user(&cmd, user_info, sizeof(cmd))) {
                return (uint32_t)-1;
            }
            return syscall_common_request_core_gfx_command(op, &cmd)
                ? 0u
                : (uint32_t)-1;
        case KERNEL_GFX_BUFFER_INVALID:
        default:
            return (uint32_t)-1;
    }
}

static uint32_t syscall_compat32_gui_event(uint32_t op, uint32_t user_info) {
    if (op == SYS_GUI_EVENT_CURSOR_INIT) {
        struct syscall_gui_event_cursor cursor;

        (void)syscall_common_request_core_gui_event_cursor_init(&cursor);
        return arch_copy_to_user(user_info, &cursor, sizeof(cursor))
            ? 0u
            : (uint32_t)-1;
    }
    if (op == SYS_GUI_EVENT_POLL) {
        struct syscall_gui_event_poll poll;
        const struct process *proc;
        uint64_t result;

        if (!arch_copy_from_user(&poll, user_info, sizeof(poll))) {
            return (uint32_t)-1;
        }
        proc = process_current();
        result = syscall_common_request_core_gui_event_poll(
            &poll, proc == 0 ? 0u : proc->pid);
        return arch_copy_to_user(user_info, &poll, sizeof(poll))
            ? (uint32_t)result
            : (uint32_t)-1;
    }
    if (op == SYS_GUI_EVENT_GRAB) {
        const struct process *proc = process_current();
        int foreground_allowed = job_current_process_foreground_allowed == 0 ||
            job_current_process_foreground_allowed();

        return (uint32_t)syscall_common_request_core_gui_event_grab(
            proc == 0 ? 0u : proc->pid,
            foreground_allowed);
    }
    if (op == SYS_GUI_EVENT_RELEASE) {
        const struct process *proc = process_current();

        return (uint32_t)syscall_common_request_core_gui_event_release(
            proc == 0 ? 0u : proc->pid);
    }
    return (uint32_t)-1;
}

struct compat32_audio_fd_stream {
    struct syscall_compat32_context *syscall_ctx;
    struct process *proc;
    uint32_t fd;
    uint32_t remaining;
};

static uint32_t compat32_audio_fd_read(void *ctx, void *buffer, uint32_t bytes) {
    struct compat32_audio_fd_stream *stream =
        (struct compat32_audio_fd_stream *)ctx;
    uint32_t copied = 0u;
    uint32_t want;
    int32_t rc;

    if (stream == 0 || buffer == 0 || bytes == 0u ||
        stream->remaining == 0u ||
        stream->syscall_ctx == 0 ||
        stream->syscall_ctx->read == 0 ||
        stream->proc == 0 ||
        stream->proc->state == PROCESS_STATE_EXITED ||
        stream->proc->state == PROCESS_STATE_STOPPED) {
        return 0u;
    }
    want = bytes > stream->remaining ? stream->remaining : bytes;
    if (want > stream->syscall_ctx->io_buffer_size) {
        want = stream->syscall_ctx->io_buffer_size;
    }
    if (want == 0u) {
        return 0u;
    }
    rc = stream->syscall_ctx->read(stream->syscall_ctx->vfs,
                                   stream->fd,
                                   buffer,
                                   want,
                                   SYS_READ_BLOCKING);
    if (rc <= 0) {
        return 0u;
    }
    copied = (uint32_t)rc;
    if (copied > stream->remaining) {
        copied = stream->remaining;
    }
    stream->remaining -= copied;
    return copied;
}

static uint32_t compat32_audio_fd_cancelled(void *ctx) {
    struct compat32_audio_fd_stream *stream =
        (struct compat32_audio_fd_stream *)ctx;

    return stream == 0 ||
           stream->proc == 0 ||
           stream->proc->state == PROCESS_STATE_EXITED ||
           stream->proc->state == PROCESS_STATE_STOPPED;
}

static uint32_t syscall_compat32_audio_play_fd(
    struct syscall_compat32_context *ctx,
    uint32_t index,
    uint32_t user_info) {
    struct syscall_audio_stream_info info;
    struct compat32_audio_fd_stream fd_stream;
    struct audio_pcm_stream stream;

    if (ctx == 0 || !arch_copy_from_user(&info, user_info, sizeof(info))) {
        return (uint32_t)-1;
    }
    if (!syscall_common_request_core_audio_stream_valid(&info)) {
        return 0u;
    }

    fd_stream.syscall_ctx = ctx;
    fd_stream.proc = process_current_mut();
    fd_stream.fd = info.fd;
    fd_stream.remaining = info.data_bytes;

    syscall_common_request_core_audio_stream_init(
        &stream,
        &info,
        &fd_stream,
        compat32_audio_fd_read,
        compat32_audio_fd_cancelled);
    return (uint32_t)syscall_common_request_core_audio_stream_dispatch(
        index, &stream);
}

int syscall_i386_request_adapter_query(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;
    uint32_t kind;

    if (ctx == 0 || request == 0 || result == 0 ||
        request->number != SYS_QUERY) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    kind = kernel_syscall_arg_u32(request, 0);
    ops.copy_from_user = syscall_compat32_clipboard_copy_from_user;
    ops.copy_to_user = syscall_compat32_clipboard_copy_to_user;
    ops.bad_pointer = 0;
    ops.bad_pointer_value = (uint64_t)(uint32_t)-1;
    if (syscall_common_request_core_query_request(request, result, &ops)) {
        return 1;
    }
    result->value = syscall_compat32_query(
        ctx,
        kind,
        kernel_syscall_arg_u32(request, 1),
        kernel_syscall_arg_u32(request, 2),
        kernel_syscall_arg_u32(request, 3));
    return 1;
}

int syscall_i386_request_adapter_misc(
    struct syscall_compat32_context *ctx,
    const struct kernel_syscall_request *request,
    struct kernel_syscall_result *result) {
    struct syscall_common_misc_ops misc_ops;
    struct syscall_common_user_copy_ops copy_ops;

    if (ctx == 0 || request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    misc_ops.clear = syscall_compat32_misc_clear;
    misc_ops.ticks = syscall_compat32_misc_ticks;
    misc_ops.reboot = syscall_compat32_misc_reboot;
    misc_ops.ctx = ctx;
    if (syscall_common_request_core_misc_request(
            request, result, &misc_ops)) {
        return 1;
    }
    copy_ops.copy_from_user = syscall_compat32_clipboard_copy_from_user;
    copy_ops.copy_to_user = syscall_compat32_clipboard_copy_to_user;
    copy_ops.bad_pointer = 0;
    copy_ops.bad_pointer_value = (uint64_t)(uint32_t)-1;
    if (syscall_common_request_core_audio_play_request(
            request,
            result,
            &copy_ops,
            g_compat32_audio_buffer,
            sizeof(g_compat32_audio_buffer),
            0xffffffffu)) {
        return 1;
    }
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
        case SYS_BLOCK_READ:
            result->value = syscall_compat32_block_read(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_BLOCK_WRITE:
            result->value = syscall_compat32_block_write(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u64(request, 1),
                kernel_syscall_arg_u32(request, 2));
            return 1;
        case SYS_BLOCK_FLUSH:
            result->value = syscall_common_request_core_block_flush_dispatch(
                kernel_syscall_arg_u32(request, 0));
            return 1;
        case SYS_AUDIO_TONE:
            return syscall_common_request_core_backend(request, result);
        case SYS_AUDIO_PLAY_FD:
            result->value = syscall_compat32_audio_play_fd(
                ctx,
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_RTL8139_TX_TEST:
            return syscall_common_request_core_backend(request, result);
        case SYS_GFX:
            result->value = syscall_compat32_gfx(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_GUI_EVENT:
            if (syscall_common_request_core_gui_event_request(
                    request,
                    result,
                    process_scheduler_current_pid(),
                    job_current_process_foreground_allowed == 0 ||
                        job_current_process_foreground_allowed())) {
                return 1;
            }
            result->value = syscall_compat32_gui_event(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_CLIPBOARD:
            if (syscall_common_request_core_clipboard(request, result)) {
                return 1;
            }
            result->value = syscall_compat32_clipboard(
                kernel_syscall_arg_u32(request, 0),
                kernel_syscall_arg_u32(request, 1));
            return 1;
        case SYS_CAPABILITY_EVENT:
            {
                struct syscall_common_user_input_ops ops;

                ops.copy_from_user = syscall_compat32_clipboard_copy_from_user;
                ops.bad_pointer = 0;
                ops.bad_pointer_value = (uint64_t)(uint32_t)-1;
                return syscall_common_request_core_capability_event_request(
                    request, result, &ops);
            }
        default:
            result->value = 0u;
            return 1;
    }
}

