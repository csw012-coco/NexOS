#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/proc/process_types_internal.h"

static uint64_t syscall_maybe_abort_interrupted_process(uint64_t rc) {
    const struct process *proc = process_current();

    if (proc != NULL &&
        (proc->state == PROCESS_STATE_EXITED || proc->state == PROCESS_STATE_STOPPED)) {
        return SYSCALL_EXIT_TO_KERNEL;
    }
    return rc;
}

static uint64_t syscall_handle_read(const struct syscall_user_buffer *buffer, uint32_t flags) {
    uint32_t chunk_max;
    uint32_t copy_size;

    if (buffer == 0 || buffer->user_addr == 0 || buffer->size == 0) {
        return 0;
    }
    if (!syscall_user_writable(buffer->user_addr, buffer->size)) {
        return syscall_kill_bad_user_pointer();
    }

    chunk_max = buffer->size > SYSCALL_COPY_CHUNK ? SYSCALL_COPY_CHUNK : buffer->size;
    {
        uint64_t rc = fs_service_read(process_current_mut(),
                                      g_syscall_vfs,
                                      SYS_FD_STDIN,
                                      g_syscall_copy_buffer,
                                      chunk_max,
                                      flags,
                                      &copy_size);
        if ((int64_t)rc <= 0) {
            return syscall_maybe_abort_interrupted_process(rc);
        }
        if (!syscall_copy_to_user(buffer->user_addr, g_syscall_copy_buffer, copy_size)) {
            return syscall_kill_bad_user_pointer();
        }
        return syscall_maybe_abort_interrupted_process(rc);
    }
}

static uint64_t syscall_handle_write_chunked(uint32_t fd, const struct syscall_user_buffer *buffer) {
    uint32_t remaining;
    uint32_t total;

    if (buffer == 0 || buffer->user_addr == 0 || buffer->size == 0) {
        return 0;
    }
    if (!syscall_user_readable(buffer->user_addr, buffer->size)) {
        return syscall_kill_bad_user_pointer();
    }

    remaining = buffer->size;
    total = 0;
    while (remaining != 0) {
        uint32_t chunk = remaining > SYSCALL_COPY_CHUNK ? SYSCALL_COPY_CHUNK : remaining;
        uint64_t written;

        if (!syscall_copy_from_user(g_syscall_copy_buffer, buffer->user_addr + total, chunk)) {
            return syscall_kill_bad_user_pointer();
        }
        written = fs_service_write(process_current_mut(), g_syscall_vfs, fd, g_syscall_copy_buffer, chunk);
        if ((int64_t)written < 0) {
            return written;
        }
        total += (uint32_t)written;
        if (written < chunk) {
            break;
        }
        remaining -= chunk;
    }
    return total;
}

uint64_t syscall_handle_write(const struct syscall_user_buffer *buffer) {
    return syscall_handle_write_chunked(SYS_FD_STDOUT, buffer);
}

uint64_t syscall_handle_fd_write(uint32_t fd, const struct syscall_user_buffer *buffer) {
    return syscall_handle_write_chunked(fd, buffer);
}

uint64_t syscall_handle_fd_read(uint32_t fd, const struct syscall_user_buffer *buffer, uint32_t flags) {
    uint32_t chunk_size;
    uint32_t copied = 0;
    uint64_t rc;

    if (fd == SYS_FD_STDIN) {
        return syscall_handle_read(buffer, flags);
    }
    if (!syscall_user_writable(buffer->user_addr, buffer->size)) {
        return syscall_kill_bad_user_pointer();
    }
    chunk_size = buffer->size > SYSCALL_COPY_CHUNK ? SYSCALL_COPY_CHUNK : buffer->size;
    rc = fs_service_read(process_current_mut(),
                         g_syscall_vfs,
                         fd,
                         g_syscall_copy_buffer,
                         chunk_size,
                         flags,
                         &copied);
    if ((int64_t)rc <= 0) {
        return syscall_maybe_abort_interrupted_process(rc);
    }
    if (!syscall_copy_to_user(buffer->user_addr, g_syscall_copy_buffer, copied)) {
        return syscall_kill_bad_user_pointer();
    }
    return syscall_maybe_abort_interrupted_process(rc);
}

uint64_t syscall_handle_close(uint32_t fd) {
    return fs_service_close(process_current_mut(), fd);
}

uint64_t syscall_handle_dup2(uint32_t src_fd, uint32_t dst_fd) {
    return fs_service_dup2(process_current_mut(), src_fd, dst_fd);
}

uint64_t syscall_handle_pipe(uint64_t user_pair_addr) {
    uint32_t pair[2];

    if (!syscall_user_writable(user_pair_addr, sizeof(pair))) {
        return syscall_kill_bad_user_pointer();
    }
    if (fs_service_pipe(process_current_mut(), pair) != 0) {
        return (uint64_t)-1;
    }
    if (!syscall_copy_to_user(user_pair_addr, pair, sizeof(pair))) {
        return syscall_kill_bad_user_pointer();
    }
    return 0;
}

uint64_t syscall_handle_readdir(uint32_t fd, uint64_t user_entry_addr) {
    struct syscall_dirent entry;

    if (!syscall_user_writable(user_entry_addr, sizeof(entry))) {
        return syscall_kill_bad_user_pointer();
    }
    {
        uint64_t rc = fs_service_readdir(process_current_mut(), g_syscall_vfs, fd, &entry);

        if (rc != 1) {
            return rc;
        }
    }
    if (!syscall_copy_to_user(user_entry_addr, &entry, sizeof(entry))) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}
