#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "hal/hal.h"
#include "fs/vfs.h"

static struct file *fs_service_active_file(struct process *proc, uint32_t fd) {
    return proc != 0 ? file_table_active(proc->files, PROCESS_FILE_MAX, fd) : 0;
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

static uint32_t fs_service_stdin_copied_size(uint32_t size, uint32_t flags, int64_t bytes) {
    uint32_t copy_size = (uint32_t)bytes;

    if ((flags & SYS_READ_CHAR) == 0 && copy_size < size) {
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

uint64_t fs_service_read(struct process *proc,
                         struct vfs *vfs,
                         uint32_t fd,
                         void *buffer,
                         uint32_t size,
                         uint32_t flags,
                         uint32_t *copied_out) {
    struct file *file;
    uint32_t copy_size;

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
    if (fd == SYS_FD_STDIN) {
        if (flags & SYS_READ_NONBLOCK) {
            int64_t bytes = file_read(file, vfs, buffer, size, flags);

            if (bytes <= 0) {
                return 0;
            }
            copy_size = fs_service_stdin_copied_size(size, flags, bytes);
            fs_service_set_copied(copied_out, copy_size);
            return (uint64_t)bytes;
        }
        for (;;) {
            int64_t bytes = file_read(file, vfs, buffer, size, flags);

            if (bytes > 0) {
                copy_size = fs_service_stdin_copied_size(size, flags, bytes);
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
            hal_cpu_halt();
        }
    }
    for (;;) {
        int64_t bytes = file_read(file, vfs, buffer, size, flags);

        if (bytes < 0) {
            return (uint64_t)-1;
        }
        if (bytes == 0) {
            if ((flags & SYS_READ_NONBLOCK) != 0 || !file_read_would_block(file)) {
                return 0;
            }
            if (fs_service_read_interrupted_local(proc)) {
                return 0;
            }
            hal_cpu_halt();
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
