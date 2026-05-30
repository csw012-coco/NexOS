#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/scheduler.h"
#include "hal/hal.h"
#include "fs/vfs.h"

static struct file *fs_service_active_file(struct process *proc, uint32_t fd) {
    return proc != 0 ? file_table_active(proc->files, PROCESS_FILE_MAX, fd) : 0;
}

static void fs_service_refresh_stdio_console(struct process *proc, uint32_t fd, const struct file *file) {
    void *tty_handle;

    if (proc == 0 || fd > SYS_FD_STDERR) {
        return;
    }
    tty_handle = file_tty_private_handle(file);
    if (tty_handle != 0) {
        proc->console_handle = tty_handle;
    }
    job_ensure_process_terminal_owner(proc);
}

static void fs_service_fill_syscall_dirent(struct syscall_dirent *dst, const struct vfs_dirent *src) {
    if (dst == 0 || src == 0) {
        return;
    }
    for (uint32_t i = 0; i < sizeof(*dst); i++) {
        ((uint8_t *)dst)[i] = 0;
    }
    for (uint32_t i = 0; i < sizeof(dst->name); i++) {
        dst->name[i] = src->name[i];
        if (src->name[i] == '\0') {
            break;
        }
    }
    dst->size = src->size;
    dst->attributes = src->attributes;
}

static void fs_service_set_copied(uint32_t *copied_out, uint32_t copied) {
    if (copied_out != 0) {
        *copied_out = copied;
    }
}

static uint32_t fs_service_map_read_flags(uint32_t syscall_flags) {
    uint32_t flags = KERNEL_FILE_READ_BLOCKING;

    if ((syscall_flags & SYS_READ_NONBLOCK) != 0) {
        flags |= KERNEL_FILE_READ_NONBLOCK;
    }
    if ((syscall_flags & SYS_READ_CHAR) != 0) {
        flags |= KERNEL_FILE_READ_CHAR;
    }
    return flags;
}

static uint32_t fs_service_stdin_copied_size(uint32_t size, uint32_t flags, int64_t bytes) {
    uint32_t copy_size = (uint32_t)bytes;

    if ((flags & KERNEL_FILE_READ_CHAR) == 0 && copy_size < size) {
        copy_size++;
    }
    return copy_size;
}

static int fs_service_read_interrupted_local(const struct process *proc) {
    if (proc == 0) {
        return 0;
    }
    return proc->state == PROCESS_STATE_EXITED || proc->state == PROCESS_STATE_STOPPED;
}

static void fs_service_restore_process_session_local(struct process_session *session,
                                                     struct user_page_mapping *mappings) {
    struct process *proc;

    if (session == 0 || mappings == 0) {
        return;
    }
    process_bind_session(session, mappings);
    proc = &session->process;
    if (proc->address_space != 0 && proc->address_space->user_cr3 != 0) {
        (void)vmm_switch_root_or_fail(proc->address_space->user_cr3);
    }
}

static int fs_service_wait_for_read_local(struct process *proc) {
    struct process_session *session;
    struct user_page_mapping *mappings;
    enum process_state previous_state;

    if (proc == 0 || fs_service_read_interrupted_local(proc)) {
        return 0;
    }
    session = process_current_session();
    mappings = process_current_mappings();
    previous_state = proc->state;
    proc->state = PROCESS_STATE_WAITING;
    sched_tick();
    fs_service_restore_process_session_local(session, mappings);
    if (fs_service_read_interrupted_local(proc)) {
        return 0;
    }
    if (proc->state == PROCESS_STATE_WAITING || proc->state == PROCESS_STATE_READY) {
        proc->state = previous_state == PROCESS_STATE_RUNNING ? PROCESS_STATE_RUNNING : previous_state;
    }
    return 1;
}

uint64_t fs_service_read(struct process *proc,
                         struct vfs *vfs,
                         uint32_t fd,
                         void *buffer,
                         uint32_t size,
                         uint32_t flags,
                         uint32_t *copied_out) {
    struct file *file;
    uint32_t copy_size;
    uint32_t file_flags;

    if (copied_out != 0) {
        *copied_out = 0;
    }
    if (buffer == 0 || size == 0) {
        return 0;
    }
    file = fs_service_active_file(proc, fd);
    if (file == 0) {
        return 0;
    }
    file_flags = fs_service_map_read_flags(flags);
    if (fd == SYS_FD_STDIN) {
        if ((file_flags & KERNEL_FILE_READ_NONBLOCK) != 0) {
            int64_t bytes = file_read(file, vfs, buffer, size, file_flags);

            if (bytes <= 0) {
                return 0;
            }
            copy_size = fs_service_stdin_copied_size(size, file_flags, bytes);
            fs_service_set_copied(copied_out, copy_size);
            return (uint64_t)bytes;
        }
        for (;;) {
            int64_t bytes = file_read(file, vfs, buffer, size, file_flags);

            if (bytes > 0) {
                copy_size = fs_service_stdin_copied_size(size, file_flags, bytes);
                fs_service_set_copied(copied_out, copy_size);
                return (uint64_t)bytes;
            }
            if (bytes < 0) {
                return (uint64_t)-1;
            }
            if (!file_read_would_block(file)) {
                return 0;
            }
            if (fs_service_read_interrupted_local(proc)) {
                return 0;
            }
            if (!fs_service_wait_for_read_local(proc)) {
                return 0;
            }
        }
    }
    for (;;) {
        int64_t bytes = file_read(file, vfs, buffer, size, file_flags);

        if (bytes < 0) {
            return (uint64_t)-1;
        }
        if (bytes == 0) {
            if ((file_flags & KERNEL_FILE_READ_NONBLOCK) != 0 || !file_read_would_block(file)) {
                return 0;
            }
            if (fs_service_read_interrupted_local(proc)) {
                return 0;
            }
            if (!fs_service_wait_for_read_local(proc)) {
                return 0;
            }
            continue;
        }
        fs_service_set_copied(copied_out, (uint32_t)bytes);
        return (uint64_t)bytes;
    }
}

uint64_t fs_service_write(struct process *proc,
                          struct vfs *vfs,
                          uint32_t fd,
                          const void *buffer,
                          uint32_t size) {
    struct file *file;
    uint32_t remaining;
    uint32_t total = 0;

    if (buffer == 0 || size == 0) {
        return 0;
    }
    file = fs_service_active_file(proc, fd);
    if (file == 0) {
        return 0;
    }
    remaining = size;
    while (remaining != 0) {
        int64_t written = file_write(file, vfs, (const uint8_t *)buffer + total, remaining);

        if (written < 0) {
            return (uint64_t)-1;
        }
        if (written == 0 && file_write_would_block(file)) {
            hal_cpu_halt();
            continue;
        }
        total += (uint32_t)written;
        if ((uint32_t)written >= remaining) {
            break;
        }
        remaining -= (uint32_t)written;
        if (file_write_would_block(file)) {
            hal_cpu_halt();
            continue;
        }
        break;
    }
    return total;
}

uint64_t fs_service_close(struct process *proc, uint32_t fd) {
    struct file *file = fs_service_active_file(proc, fd);

    if (fd <= SYS_FD_STDERR || file == 0) {
        return (uint64_t)-1;
    }
    return (uint64_t)file_close(file);
}

uint64_t fs_service_dup2(struct process *proc, uint32_t src_fd, uint32_t dst_fd) {
    struct file *src;
    struct file *dst;

    if (proc == 0 || src_fd >= PROCESS_FILE_MAX || dst_fd >= PROCESS_FILE_MAX) {
        return (uint64_t)-1;
    }
    if (src_fd == dst_fd) {
        return src_fd;
    }
    src = fs_service_active_file(proc, src_fd);
    dst = proc != 0 ? file_table_slot(proc->files, PROCESS_FILE_MAX, dst_fd) : 0;
    if (src == 0 || dst == 0) {
        return (uint64_t)-1;
    }
    file_discard(dst);
    if (!file_clone(dst, src)) {
        return (uint64_t)-1;
    }
    fs_service_refresh_stdio_console(proc, dst_fd, dst);
    return dst_fd;
}

uint64_t fs_service_pipe(struct process *proc, uint32_t pair_out[2]) {
    if (proc == 0 || pair_out == 0) {
        return (uint64_t)-1;
    }
    return file_table_open_pipe_pair(proc->files, PROCESS_FILE_MAX, 3u, pair_out) ? 0u : (uint64_t)-1;
}

uint64_t fs_service_readdir(struct process *proc,
                            struct vfs *vfs,
                            uint32_t fd,
                            struct syscall_dirent *entry) {
    struct file *file = fs_service_active_file(proc, fd);
    struct vfs_dirent vfs_entry;
    int64_t rc;

    if (file == 0 || entry == 0) {
        return (uint64_t)-1;
    }
    rc = file_readdir(file, vfs, &vfs_entry);
    if (rc <= 0) {
        return (uint64_t)(rc < 0 ? -1 : 0);
    }
    fs_service_fill_syscall_dirent(entry, &vfs_entry);
    return 1;
}
