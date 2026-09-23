#include "kernel/internal/sys/syscall_internal.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/internal/core/machine_info_internal.h"
#include "kernel/internal/core/runtime_internal.h"
#include "kernel/internal/core/system_power_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/fs/fs_service_mount_query_internal.h"
#include "kernel/internal/fs/fs_service_path_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/sys/syscall.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/internal/sys/syscall_native_request_core.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/scheduler.h"
#include "lib/string.h"

enum {
    SYSCALL_NATIVE_COPY_CHUNK = 16384u,
    SYSCALL_NATIVE_AUDIO_BUFFER_MAX = 1048576u,
    SYSCALL_NATIVE_GFX_BATCH_CHUNK = 32u,
    SYSCALL_NATIVE_GFX_BLIT_MAX_DIMENSION = 8192u,
    SYSCALL_NATIVE_ENV_MAX = 16u,
    SYSCALL_NATIVE_ENV_TEXT_MAX = NOS_PATH_BUFFER_SIZE
};

struct syscall_native_env_capture {
    char storage[SYSCALL_NATIVE_ENV_MAX][SYSCALL_NATIVE_ENV_TEXT_MAX];
    const char *envp[SYSCALL_NATIVE_ENV_MAX + 1u];
};

static uint8_t g_syscall_native_audio_buffer[SYSCALL_NATIVE_AUDIO_BUFFER_MAX];
static uint8_t g_syscall_native_copy_buffer[SYSCALL_NATIVE_COPY_CHUNK]
    __attribute__((aligned(sizeof(uint32_t))));
static uint8_t g_syscall_native_rtl8139_tx_buffer[1600u];
static struct syscall_gfx_batch_entry
    g_syscall_native_gfx_batch_entries[SYSCALL_NATIVE_GFX_BATCH_CHUNK];
static char g_syscall_native_clipboard_buffer[KERNEL_CLIPBOARD_TEXT_MAX + 1u];
static char g_syscall_native_name_buffer[NOS_TTY_LINE_MAX + 1u];

static int syscall_native_copy_from_user_ops(void *dest,
                                             uint64_t user_addr,
                                             uint32_t size) {
    return syscall_user_readable(user_addr, size) &&
           syscall_copy_from_user(dest, user_addr, size);
}

static int syscall_native_copy_to_user_ops(uint64_t user_addr,
                                           const void *src,
                                           uint32_t size) {
    return syscall_user_writable(user_addr, size) &&
           syscall_copy_to_user(user_addr, src, size);
}

static int syscall_native_copy_user_cstr_ops(char *dest,
                                             uint64_t user_addr,
                                             uint32_t size) {
    return syscall_copy_user_cstr(dest, user_addr, size);
}

static void syscall_native_copy_name(char *dst,
                                     uint32_t dst_size,
                                     const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || dst_size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int syscall_native_copy_user_envp(
    struct syscall_native_env_capture *capture,
    uint64_t user_envp_addr) {
    uint32_t i;

    if (capture == 0) {
        return 0;
    }
    capture->envp[0] = 0;
    if (user_envp_addr == 0u) {
        return 1;
    }

    for (i = 0u; i < SYSCALL_NATIVE_ENV_MAX; i++) {
        uint64_t user_entry_addr = 0u;

        if (!syscall_copy_from_user(
                &user_entry_addr,
                user_envp_addr + (uint64_t)i * sizeof(uint64_t),
                sizeof(user_entry_addr))) {
            return 0;
        }
        if (user_entry_addr == 0u) {
            capture->envp[i] = 0;
            return 1;
        }
        if (!syscall_copy_user_cstr(capture->storage[i],
                                    user_entry_addr,
                                    sizeof(capture->storage[i]))) {
            return 0;
        }
        capture->envp[i] = capture->storage[i];
        capture->envp[i + 1u] = 0;
    }

    capture->envp[SYSCALL_NATIVE_ENV_MAX] = 0;
    return 1;
}

static int syscall_native_capability_event_copy_from_user(void *dest,
                                                          uint64_t user_addr,
                                                          uint32_t size) {
    return syscall_native_copy_from_user_ops(dest, user_addr, size);
}

static void syscall_native_user_copy_ops(
    struct syscall_common_user_copy_ops *ops,
    int copy_cstr) {
    if (ops == 0) {
        return;
    }
    ops->copy_from_user = syscall_native_copy_from_user_ops;
    ops->copy_to_user = syscall_native_copy_to_user_ops;
    ops->copy_user_cstr = copy_cstr ? syscall_native_copy_user_cstr_ops : 0;
    ops->bad_pointer = syscall_kill_bad_user_pointer;
    ops->bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
}

static void syscall_native_clipboard_transfer_ops(
    struct syscall_common_clipboard_transfer_ops *ops) {
    if (ops == 0) {
        return;
    }
    ops->copy_from_user = syscall_native_copy_from_user_ops;
    ops->copy_to_user = syscall_native_copy_to_user_ops;
    ops->bad_pointer = syscall_kill_bad_user_pointer;
    ops->bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
}

static void syscall_native_io_wait_for_interrupt(void *ctx) {
    const struct process *proc = process_current();

    (void)ctx;
    if (proc != 0 && proc->pid != 0u) {
        sched_tick_excluding_pid(proc->pid);
    } else {
        sched_tick();
    }
    kernel_runtime_display_service_pending();
    kernel_runtime_wait_for_interrupt();
}

static struct tty *syscall_native_io_fd_tty(uint32_t fd) {
    struct process *proc = process_current_mut();
    struct file *file;

    if (proc == 0) {
        return 0;
    }
    file = file_table_active(proc->files, PROCESS_FILE_MAX, fd);
    return (struct tty *)file_tty_private_handle(file);
}

static void syscall_native_file_io_ops(
    struct syscall_common_file_io_ops *ops,
    uint32_t fd) {
    if (ops == 0) {
        return;
    }
    ops->io_buffer = (char *)g_syscall_native_copy_buffer;
    ops->io_buffer_size = SYSCALL_NATIVE_COPY_CHUNK;
    ops->tty = syscall_native_io_fd_tty(fd);
    ops->drain_tty_input = 0;
    ops->wait_for_interrupt = syscall_native_io_wait_for_interrupt;
    ops->ctx = 0;
}

struct syscall_native_audio_fd_stream {
    struct process *proc;
    uint32_t fd;
    uint32_t remaining;
};

static uint32_t syscall_native_audio_fd_read(
    void *ctx,
    void *buffer,
    uint32_t bytes) {
    struct syscall_native_audio_fd_stream *stream =
        (struct syscall_native_audio_fd_stream *)ctx;
    uint32_t copied = 0;
    uint32_t want;
    uint64_t rc;

    if (stream == 0 || buffer == 0 || bytes == 0u ||
        stream->remaining == 0u ||
        stream->proc == 0 ||
        stream->proc->state == PROCESS_STATE_EXITED ||
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

static uint32_t syscall_native_audio_fd_cancelled(void *ctx) {
    struct syscall_native_audio_fd_stream *stream =
        (struct syscall_native_audio_fd_stream *)ctx;

    return stream == 0 ||
           stream->proc == 0 ||
           stream->proc->state == PROCESS_STATE_EXITED ||
           stream->proc->state == PROCESS_STATE_STOPPED;
}

static void syscall_native_audio_fd_prepare(
    void *ctx,
    const struct syscall_audio_stream_info *info) {
    struct syscall_native_audio_fd_stream *stream =
        (struct syscall_native_audio_fd_stream *)ctx;

    if (stream == 0 || info == 0) {
        return;
    }
    stream->fd = info->fd;
    stream->remaining = info->data_bytes;
}

static uint64_t syscall_native_misc_clear(void *ctx) {
    struct process *proc = process_current_mut();
    struct tty *tty;

    (void)ctx;
    if (proc == 0) {
        return 0;
    }
    tty = (struct tty *)file_tty_private_handle(
        file_table_active(proc->files, PROCESS_FILE_MAX, SYS_FD_STDOUT));
    if (tty == 0) {
        tty = (struct tty *)file_tty_private_handle(
            file_table_active(proc->files, PROCESS_FILE_MAX, SYS_FD_STDERR));
    }
    if (tty == 0 && proc->console_handle != 0) {
        tty = (struct tty *)proc->console_handle;
    }
    if (tty == 0) {
        return 0;
    }
    tty_clear(tty);
    return 1;
}

static uint64_t syscall_native_misc_ticks(void *ctx) {
    uint32_t hz = kernel_runtime_timer_hz();
    uint32_t ticks;

    (void)ctx;
    ticks = g_syscall_ticks != 0 ? *g_syscall_ticks : 0u;
    if (hz == 0u) {
        hz = 100u;
    }
    return ((uint64_t)ticks * 1000ull) / hz;
}

static uint64_t syscall_native_misc_reboot(void *ctx) {
    (void)ctx;
    return kernel_reboot();
}

static uint64_t syscall_native_misc_poweroff(void *ctx) {
    (void)ctx;
    return kernel_poweroff();
}

struct syscall_native_query_context {
    struct process *proc;
    struct vfs *vfs;
};

static uint32_t syscall_native_query_fd_kind(void *ctx, uint32_t fd) {
    const struct syscall_native_query_context *query_ctx =
        (const struct syscall_native_query_context *)ctx;

    return syscall_common_request_core_process_fd_kind(
        query_ctx != 0 ? query_ctx->proc : 0,
        fd);
}

static int syscall_native_query_tty(void *ctx,
                                    uint32_t fd,
                                    struct syscall_tty_info *info) {
    const struct syscall_native_query_context *query_ctx =
        (const struct syscall_native_query_context *)ctx;

    return syscall_common_request_core_process_tty_query(
        query_ctx != 0 ? query_ctx->proc : 0,
        fd,
        info);
}

static int32_t syscall_native_query_fd_query(void *ctx,
                                             uint32_t fd,
                                             struct syscall_fd_info *info) {
    const struct syscall_native_query_context *query_ctx =
        (const struct syscall_native_query_context *)ctx;

    return syscall_common_request_core_process_fd_query(
        query_ctx != 0 ? query_ctx->proc : 0,
        fd,
        info);
}

static int syscall_native_query_fill_mount_info(
    void *ctx,
    struct syscall_mount_info *info,
    uint32_t index,
    uint32_t flags) {
    const struct syscall_native_query_context *query_ctx =
        (const struct syscall_native_query_context *)ctx;
    struct vfs *vfs = query_ctx != 0 ? query_ctx->vfs : 0;
    struct vfs_mount_info mount;
    uint32_t offset = 1u;

    if (vfs == 0 || info == 0) {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    info->kind = SYS_MOUNT_INFO_NONE;
    if (!fs_service_fill_builtin_mount_info(vfs, index, info, &offset, flags)) {
        if (index < offset ||
            !fs_service_get_mount_info(vfs, index - offset, &mount)) {
            return 0;
        }
        info->kind = mount.kind == VFS_MOUNT_FAT32
            ? SYS_MOUNT_INFO_FAT32
            : SYS_MOUNT_INFO_NXFS;
        info->disk_index = mount.disk_index;
        info->part_index = mount.part_index;
        info->source_known = 1u;
        syscall_native_copy_name(
            info->target, sizeof(info->target), mount.name);
        if ((flags & SYS_QUERY_FLAG_NO_SPACE) == 0u) {
            fs_service_fill_dynamic_space(vfs, index - offset, info);
        }
    }
    return 1;
}

static void syscall_native_query_fill_machine_info(
    void *ctx,
    struct syscall_machine_info *info) {
    (void)ctx;
    kernel_fill_machine_info(info);
}

struct syscall_native_proc_context {
    const struct syscall_frame *frame;
};

static void syscall_native_proc_fill_info(
    struct syscall_process_info *out,
    const struct process_snapshot *proc) {
    uint32_t i;

    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->state = PROCESS_STATE_FREE;
    if (proc == 0) {
        return;
    }
    out->pid = proc->pid;
    out->slot = proc->slot;
    out->state = proc->state;
    out->exit_code = proc->exit_code;
    out->wake_tick = proc->wake_tick;
    out->image_kind = proc->image_kind;
    out->caps = proc->caps;
    out->uid = proc->uid;
    out->gid = proc->gid;
    for (i = 0u; i + 1u < sizeof(out->name) && proc->name[i] != '\0'; i++) {
        out->name[i] = proc->name[i];
    }
}

static uint64_t syscall_native_proc_write_info(
    uint64_t user_info_addr,
    const struct process_snapshot *proc) {
    struct syscall_process_info info;

    syscall_native_proc_fill_info(&info, proc);
    if (!syscall_copy_to_user(user_info_addr, &info, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    return 1u;
}

static uint64_t syscall_native_proc_exit(void *ctx, int32_t code) {
    (void)ctx;
    process_exit_current(process_current_session(), code);
    return SYSCALL_EXIT_TO_KERNEL;
}

static uint64_t syscall_native_proc_yield(void *ctx) {
    struct syscall_native_proc_context *proc_ctx =
        (struct syscall_native_proc_context *)ctx;

    sched_yield_current(process_current_session(),
                        proc_ctx != 0 ? proc_ctx->frame : 0);
    return SYSCALL_EXIT_TO_KERNEL;
}

static uint64_t syscall_native_proc_error_result(void) {
    uint32_t error = process_last_error();
    int errno_value;

    switch (error) {
        case PROCESS_EXEC_ERR_BAD_ARGS:
            errno_value = NEX_ERR_INVAL;
            break;
        case PROCESS_EXEC_ERR_FILE_NOT_FOUND:
            errno_value = NEX_ERR_NOENT;
            break;
        case PROCESS_EXEC_ERR_FILE_TOO_LARGE:
            errno_value = NEX_ERR_FBIG;
            break;
        case PROCESS_EXEC_ERR_FILE_READ:
            errno_value = NEX_ERR_IO;
            break;
        case PROCESS_EXEC_ERR_ELF_HEADER:
            errno_value = NEX_ERR_BAD_ELF;
            break;
        case PROCESS_EXEC_ERR_ELF_SEGMENT_BOUNDS:
        case PROCESS_EXEC_ERR_ELF_SEGMENT_ADDR:
        case PROCESS_EXEC_ERR_ELF_SEGMENT_MAP:
            errno_value = NEX_ERR_EXEC;
            break;
        case PROCESS_EXEC_ERR_STACK_ALLOC:
            errno_value = NEX_ERR_NOMEM;
            break;
        case PROCESS_EXEC_ERR_ENTER:
            errno_value = NEX_ERR_AGAIN;
            break;
        case PROCESS_EXEC_OK:
        default:
            errno_value = NEX_ERR_INVAL;
            break;
    }
    return (uint64_t)(-(int64_t)errno_value);
}

static uint32_t syscall_native_sleep_ms_to_ticks(uint32_t ms) {
    uint32_t hz = kernel_runtime_timer_hz();
    uint64_t ticks;

    if (ms == 0u) {
        return 0u;
    }
    if (hz == 0u) {
        hz = 100u;
    }
    ticks = ((uint64_t)ms * hz + 999ull) / 1000ull;
    if (ticks == 0u) {
        ticks = 1u;
    }
    if (ticks > 0xffffffffull) {
        ticks = 0xffffffffull;
    }
    return (uint32_t)ticks;
}

static uint64_t syscall_native_proc_sleep(void *ctx, uint32_t ms) {
    struct syscall_native_proc_context *proc_ctx =
        (struct syscall_native_proc_context *)ctx;

    sched_sleep_current(process_current_session(),
                        proc_ctx != 0 ? proc_ctx->frame : 0,
                        syscall_native_sleep_ms_to_ticks(ms));
    return SYSCALL_EXIT_TO_KERNEL;
}

static uint64_t syscall_native_proc_exec_replace(void *ctx,
                                                 uint64_t user_command,
                                                 uint64_t user_envp) {
    struct syscall_native_env_capture capture;

    (void)ctx;
    if (process_current_mut() == 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_user_cstr(g_syscall_native_name_buffer,
                                user_command,
                                sizeof(g_syscall_native_name_buffer)) ||
        !syscall_native_copy_user_envp(&capture, user_envp)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!process_exec_replace_from_user(g_syscall_vfs,
                                        g_syscall_native_name_buffer,
                                        capture.envp)) {
        return syscall_native_proc_error_result();
    }
    return SYSCALL_EXIT_TO_KERNEL;
}

static uint64_t syscall_native_proc_exec(void *ctx,
                                         uint64_t user_command,
                                         uint64_t user_envp) {
    struct syscall_native_env_capture capture;
    struct process *proc = process_current_mut();

    (void)ctx;
    if (proc == 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_user_cstr(g_syscall_native_name_buffer,
                                user_command,
                                sizeof(g_syscall_native_name_buffer)) ||
        !syscall_native_copy_user_envp(&capture, user_envp)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!process_exec_from_user(g_syscall_vfs,
                                proc,
                                g_syscall_native_name_buffer,
                                capture.envp)) {
        return syscall_native_proc_error_result();
    }
    return 0u;
}

static uint64_t syscall_native_proc_fork(void *ctx) {
    struct syscall_native_proc_context *proc_ctx =
        (struct syscall_native_proc_context *)ctx;
    uint32_t child_pid = 0u;

    if (!job_fork_current(proc_ctx != 0 ? proc_ctx->frame : 0, &child_pid)) {
        return (uint64_t)-1;
    }
    return child_pid;
}

static uint64_t syscall_native_proc_wait(void *ctx,
                                         uint32_t pid,
                                         uint64_t user_info_addr) {
    const struct process *caller;
    struct process_snapshot proc;
    int ok;

    (void)ctx;
    if (user_info_addr != 0u &&
        !syscall_user_writable(user_info_addr,
                               sizeof(struct syscall_process_info))) {
        return syscall_kill_bad_user_pointer();
    }
    ok = pid == SYS_WAIT_LAST_PID
        ? process_wait_last(&proc)
        : process_wait_pid(pid, &proc);
    if (!ok) {
        caller = process_current();
        if (caller != 0 && process_has_wait_child(caller->pid, pid)) {
            return (uint64_t)(-(int64_t)NEX_ERR_AGAIN);
        }
        return (uint64_t)(-(int64_t)NEX_ERR_CHILD);
    }
    if (user_info_addr == 0u) {
        return 1u;
    }
    return syscall_native_proc_write_info(user_info_addr, &proc);
}

static uint64_t syscall_native_proc_getpid(void *ctx) {
    const struct process *proc = process_current();

    (void)ctx;
    return proc != 0 ? proc->pid : 0u;
}

static uint64_t syscall_native_proc_query(void *ctx,
                                          uint32_t kind,
                                          uint32_t index,
                                          uint64_t user_info_addr) {
    struct process_snapshot proc;
    int ok;

    (void)ctx;
    if (!syscall_user_writable(user_info_addr,
                               sizeof(struct syscall_process_info))) {
        return syscall_kill_bad_user_pointer();
    }
    switch (kind) {
        case SYS_PROC_QUERY_ALL:
            ok = process_get(index, &proc);
            break;
        case SYS_PROC_QUERY_JOBS:
            ok = job_get(index, &proc);
            break;
        case SYS_PROC_QUERY_LAST_EXIT:
            ok = process_get_last_exit(&proc);
            break;
        default:
            return 0u;
    }
    if (!ok) {
        return 0u;
    }
    return syscall_native_proc_write_info(user_info_addr, &proc);
}

static uint64_t syscall_native_proc_kill(void *ctx, uint32_t pid) {
    int rc;

    (void)ctx;
    rc = job_kill_pid(pid);
    return rc > 0 ? 1u : (uint64_t)(int64_t)rc;
}

static uint64_t syscall_native_proc_fg(void *ctx, uint32_t pid) {
    int rc;

    (void)ctx;
    rc = job_foreground_pid(pid);
    return rc > 0 ? 1u : (uint64_t)(int64_t)rc;
}

static uint64_t syscall_native_proc_bg(void *ctx, uint32_t pid) {
    int rc;

    (void)ctx;
    rc = job_background_pid(pid);
    return rc > 0 ? 1u : (uint64_t)(int64_t)rc;
}

static uint64_t syscall_native_proc_tty_claim(void *ctx) {
    int rc;

    (void)ctx;
    rc = job_claim_process_terminal(process_current());
    return rc > 0 ? 1u : (uint64_t)(int64_t)rc;
}

static uint64_t syscall_native_proc_spawn(void *ctx,
                                          uint64_t user_command,
                                          uint32_t mode,
                                          uint32_t flags,
                                          uint64_t user_envp) {
    struct syscall_native_env_capture capture;
    struct process *proc = process_current_mut();
    uint32_t child_pid = 0u;

    (void)ctx;
    if (proc == 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_user_cstr(g_syscall_native_name_buffer,
                                user_command,
                                sizeof(g_syscall_native_name_buffer)) ||
        !syscall_native_copy_user_envp(&capture, user_envp)) {
        return syscall_kill_bad_user_pointer();
    }
    if (!process_spawn_from_user(g_syscall_vfs,
                                 proc,
                                 g_syscall_native_name_buffer,
                                 capture.envp,
                                 mode,
                                 flags,
                                 &child_pid)) {
        return syscall_native_proc_error_result();
    }
    return child_pid;
}

static uint64_t syscall_native_mount_switch_root(void *ctx,
                                                 struct vfs *vfs,
                                                 const char *target) {
    (void)ctx;
    return fs_service_switch_root(vfs, target);
}

static uint64_t syscall_native_vm_page_alloc(void *ctx) {
    (void)ctx;
    return addrspace_alloc_page();
}

static uint64_t syscall_native_vm_page_free(void *ctx, uint64_t user_page) {
    (void)ctx;
    if (!syscall_user_page_arg_valid(user_page)) {
        return syscall_kill_bad_user_pointer();
    }
    return (uint64_t)addrspace_free_page(user_page);
}

static void syscall_native_vm_page_ops(
    struct syscall_common_vm_page_ops *ops) {
    if (ops == 0) {
        return;
    }
    ops->page_alloc = syscall_native_vm_page_alloc;
    ops->page_alloc_prot = 0;
    ops->page_alloc_at = 0;
    ops->page_free = syscall_native_vm_page_free;
    ops->page_protect = 0;
    ops->release_page = 0;
    ops->user_mmap_base = USER_MMAP_BASE;
    ops->user_mmap_end = USER_MMAP_END;
    ops->page_size = NOS_PAGE_SIZE;
    ops->max_pages = 0u;
    ops->ctx = 0;
}

int syscall_native_request_core_io(const struct kernel_syscall_request *request,
                                   const struct syscall_frame *frame,
                                   struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_file_io_ops io_ops;

    (void)frame;
    if (request == 0 || result == 0) {
        return 0;
    }
    syscall_native_user_copy_ops(&copy_ops, 1);
    syscall_native_file_io_ops(&io_ops, kernel_syscall_arg_u32(request, 0));
    if (syscall_common_request_core_io_request(
            process_current_mut(),
            g_syscall_vfs,
            request,
            result,
            &copy_ops,
            &io_ops)) {
        if (result->action == SYSCALL_RESULT_IO_WAIT) {
            struct tty *tty = (struct tty *)(uintptr_t)result->extra;

            sched_wait_current(process_current_session(),
                               frame,
                               (uint64_t)(int64_t)-NEX_ERR_AGAIN);
            /* Close the input-arrival race after publishing the wait state. */
            (void)job_tty_wake_waiting_processes(tty);
        }
        return 1;
    }
    return syscall_common_request_core_fs_fd_request(
        process_current_mut(), g_syscall_vfs, request, result, &copy_ops);
}

int syscall_native_request_core_fs(const struct kernel_syscall_request *request,
                            struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    syscall_native_user_copy_ops(&copy_ops, 1);
    if (syscall_common_request_core_fs_fd_request(
            process_current_mut(), g_syscall_vfs, request, result, &copy_ops)) {
        return 1;
    }
    if (syscall_common_request_core_fs_path_request(
            process_current_mut(), g_syscall_vfs, request, result, &copy_ops)) {
        return 1;
    }
    return 0;
}

int syscall_native_request_core_proc(const struct kernel_syscall_request *request,
                              const struct syscall_frame *frame,
                              struct kernel_syscall_result *result) {
    struct syscall_native_proc_context proc_ctx;
    struct syscall_common_proc_ops proc_ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    proc_ctx.frame = frame;
    proc_ops.exit = syscall_native_proc_exit;
    proc_ops.yield = syscall_native_proc_yield;
    proc_ops.exec_replace = syscall_native_proc_exec_replace;
    proc_ops.exec = syscall_native_proc_exec;
    proc_ops.fork = syscall_native_proc_fork;
    proc_ops.wait = syscall_native_proc_wait;
    proc_ops.sleep = syscall_native_proc_sleep;
    proc_ops.getpid = syscall_native_proc_getpid;
    proc_ops.proc_query = syscall_native_proc_query;
    proc_ops.kill = syscall_native_proc_kill;
    proc_ops.fg = syscall_native_proc_fg;
    proc_ops.bg = syscall_native_proc_bg;
    proc_ops.tty_claim = syscall_native_proc_tty_claim;
    proc_ops.spawn = syscall_native_proc_spawn;
    proc_ops.ctx = &proc_ctx;
    return syscall_common_request_core_proc_lifecycle_request(
        request, result, &proc_ops);
}

int syscall_native_request_core_mount(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_mount_ops mount_ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    syscall_native_user_copy_ops(&copy_ops, 1);
    mount_ops.boot_partition_lba =
        g_syscall_boot_info != 0 ? g_syscall_boot_info->partition_lba : 0u;
    mount_ops.boot_partition_sectors =
        g_syscall_boot_info != 0 ? g_syscall_boot_info->partition_sectors : 0u;
    mount_ops.has_boot_info = g_syscall_boot_info != 0;
    mount_ops.switch_root = syscall_native_mount_switch_root;
    mount_ops.ctx = 0;
    return syscall_common_request_core_mount_request(process_current_mut(),
                                                     g_syscall_vfs,
                                                     request,
                                                     result,
                                                     &copy_ops,
                                                     &mount_ops);
}

int syscall_native_request_core_query(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_query_ops query_ops;
    struct syscall_native_query_context query_ctx;

    if (request == 0 || result == 0 || request->number != SYS_QUERY) {
        return 0;
    }
    query_ctx.proc = process_current_mut();
    query_ctx.vfs = g_syscall_vfs;
    syscall_native_user_copy_ops(&copy_ops, 1);
    query_ops.fd_kind = syscall_native_query_fd_kind;
    query_ops.tty_query = syscall_native_query_tty;
    query_ops.fd_query = syscall_native_query_fd_query;
    query_ops.fill_mount_info = syscall_native_query_fill_mount_info;
    query_ops.fill_machine_info = syscall_native_query_fill_machine_info;
    query_ops.ctx = &query_ctx;
    return syscall_common_request_core_query_request_with_ops(
        request, result, &copy_ops, &query_ops);
}

int syscall_native_request_core_mem(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_vm_page_ops page_ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    syscall_native_user_copy_ops(&copy_ops, 0);
    syscall_native_vm_page_ops(&page_ops);
    if (syscall_common_request_core_mmap_request(
            request, result, &copy_ops, &page_ops)) {
        return 1;
    }
    if (syscall_common_request_core_vm_page_request(
            request, result, &page_ops)) {
        return 1;
    }
    return 0;
}

int syscall_native_request_core_ipc(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    ops.copy_from_user = syscall_native_copy_from_user_ops;
    ops.copy_to_user = syscall_native_copy_to_user_ops;
    ops.copy_user_cstr = syscall_native_copy_user_cstr_ops;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    return syscall_common_request_core_ipc_request(request, result, &ops);
}

int syscall_native_request_core_block(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    ops.copy_from_user = syscall_native_copy_from_user_ops;
    ops.copy_to_user = syscall_native_copy_to_user_ops;
    ops.copy_user_cstr = 0;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    return syscall_common_request_core_block_request(request, result, &ops);
}

int syscall_native_request_core_audio(const struct kernel_syscall_request *request,
                               struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;
    struct syscall_native_audio_fd_stream fd_stream;

    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    ops.copy_from_user = syscall_native_copy_from_user_ops;
    ops.copy_to_user = syscall_native_copy_to_user_ops;
    ops.copy_user_cstr = 0;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    if (syscall_common_request_core_audio_play_request(
            request,
            result,
            &ops,
            g_syscall_native_audio_buffer,
            sizeof(g_syscall_native_audio_buffer),
            (uint64_t)-1)) {
        return 1;
    }
    switch (request->number) {
        case SYS_AUDIO_TONE:
            return syscall_common_request_core_backend(request, result);
        case SYS_AUDIO_PLAY_FD:
            fd_stream.proc = process_current_mut();
            fd_stream.fd = 0u;
            fd_stream.remaining = 0u;
            return syscall_common_request_core_audio_stream_request(
                request,
                result,
                &ops,
                &fd_stream,
                syscall_native_audio_fd_prepare,
                syscall_native_audio_fd_read,
                syscall_native_audio_fd_cancelled);
        default:
            return 0;
    }
}

int syscall_native_request_core_net(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    ops.copy_from_user = syscall_native_copy_from_user_ops;
    ops.copy_to_user = syscall_native_copy_to_user_ops;
    ops.copy_user_cstr = 0;
    ops.bad_pointer = syscall_kill_bad_user_pointer;
    ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
    if (syscall_common_request_core_rtl8139_request(
            request,
            result,
            &ops,
            g_syscall_native_rtl8139_tx_buffer,
            sizeof(g_syscall_native_rtl8139_tx_buffer),
            (uint64_t)-1)) {
        return 1;
    }
    switch (request->number) {
        case SYS_RTL8139_TX_TEST:
            return syscall_common_request_core_backend(request, result);
        default:
            return 0;
    }
}

int syscall_native_request_core_gfx(const struct kernel_syscall_request *request,
                             struct kernel_syscall_result *result) {
    struct syscall_common_user_copy_ops copy_ops;
    struct syscall_common_clipboard_transfer_ops clipboard_ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    result->action = SYSCALL_RESULT_RETURN;
    syscall_native_user_copy_ops(&copy_ops, 0);
    switch (request->number) {
        case SYS_GFX:
            return syscall_common_request_core_gfx_request(
                request,
                result,
                &copy_ops,
                g_syscall_native_gfx_batch_entries,
                SYSCALL_NATIVE_GFX_BATCH_CHUNK,
                g_syscall_native_copy_buffer,
                SYSCALL_NATIVE_COPY_CHUNK,
                SYSCALL_NATIVE_GFX_BLIT_MAX_DIMENSION,
                (uint64_t)-1,
                1);
        case SYS_GUI_EVENT:
            if (syscall_common_request_core_gui_event_transfer_request(
                    request,
                    result,
                    &copy_ops,
                    (uint32_t)syscall_native_proc_getpid(0),
                    job_current_process_foreground_allowed())) {
                return 1;
            }
            return 0;
        case SYS_CLIPBOARD:
            syscall_native_clipboard_transfer_ops(&clipboard_ops);
            return syscall_common_request_core_clipboard_transfer_request(
                request,
                result,
                &clipboard_ops,
                g_syscall_native_clipboard_buffer,
                sizeof(g_syscall_native_clipboard_buffer));
        default:
            return 0;
    }
}

int syscall_native_request_core_misc(const struct kernel_syscall_request *request,
                              const struct syscall_frame *frame,
                              struct kernel_syscall_result *result) {
    struct syscall_common_misc_ops misc_ops;

    if (request == 0 || result == 0) {
        return 0;
    }
    (void)frame;
    result->action = SYSCALL_RESULT_RETURN;
    misc_ops.clear = syscall_native_misc_clear;
    misc_ops.ticks = syscall_native_misc_ticks;
    misc_ops.reboot = syscall_native_misc_reboot;
    misc_ops.poweroff = syscall_native_misc_poweroff;
    misc_ops.ctx = 0;
    if (syscall_common_request_core_misc_request(
            request, result, &misc_ops)) {
        return 1;
    }
    switch (request->number) {
        case SYS_CAPABILITY:
            {
                struct syscall_common_user_copy_ops ops;

                syscall_native_user_copy_ops(&ops, 1);
                return syscall_common_request_core_capability_request(
                    request, result, &ops);
            }
        case SYS_IDENTITY:
            {
                struct syscall_common_user_copy_ops ops;

                syscall_native_user_copy_ops(&ops, 1);
                return syscall_common_request_core_identity_request(
                    request, result, &ops);
            }
        case SYS_CAPABILITY_EVENT:
            {
                struct syscall_common_user_input_ops ops;

                ops.copy_from_user = syscall_native_capability_event_copy_from_user;
                ops.bad_pointer = syscall_kill_bad_user_pointer;
                ops.bad_pointer_value = SYSCALL_EXIT_TO_KERNEL;
                return syscall_common_request_core_capability_event_request(
                    request, result, &ops);
            }
        default:
            result->value = 0;
            return 1;
    }
}

int syscall_native_dispatch_request(
    const struct kernel_syscall_request *request,
    const struct syscall_frame *frame,
    struct kernel_syscall_result *result) {
    return syscall_native_request_core_io(request, frame, result) ||
           syscall_native_request_core_fs(request, result) ||
           syscall_native_request_core_proc(request, frame, result) ||
           syscall_native_request_core_mount(request, result) ||
           syscall_native_request_core_query(request, result) ||
           syscall_native_request_core_mem(request, result) ||
           syscall_native_request_core_ipc(request, result) ||
           syscall_native_request_core_block(request, result) ||
           syscall_native_request_core_audio(request, result) ||
           syscall_native_request_core_net(request, result) ||
           syscall_native_request_core_gfx(request, result) ||
           syscall_native_request_core_misc(request, frame, result);
}
