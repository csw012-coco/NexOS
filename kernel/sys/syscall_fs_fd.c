#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/public/core/profile.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/core/tty.h"

static uint32_t g_sys_write_profile;

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

static int syscall_fd_is_pipe_write(uint32_t fd) {
    struct process *proc = process_current_mut();
    struct file *file;

    if (proc == 0) {
        return 0;
    }
    file = file_table_active(proc->files, PROCESS_FILE_MAX, fd);
    return file != 0 && file->kind == KERNEL_FILE_PIPE_WRITE;
}

static uint64_t syscall_handle_write_chunked(uint32_t fd, const struct syscall_user_buffer *buffer) {
    uint32_t remaining;
    uint32_t total;

    if (buffer == 0 || buffer->user_addr == 0 || buffer->size == 0) {
        return 0;
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

static uint64_t syscall_sleep_would_block(const struct syscall_frame *frame, uint64_t result) {
    struct process *proc = process_current_mut();

    sched_resume_current_syscall(process_current_session(), frame, result);

    if (proc != 0) {
        proc->state = PROCESS_STATE_WAITING;
    }

    return SYSCALL_EXIT_TO_KERNEL;
}

uint64_t syscall_handle_write(const struct syscall_user_buffer *buffer,
                              const struct syscall_frame *frame) {
    uint64_t result;

    (void)frame;

    result = syscall_handle_write_chunked(SYS_FD_STDOUT, buffer);

    if ((int64_t)result == KERNEL_FILE_IO_WOULD_BLOCK) {
        return syscall_sleep_would_block(frame, result);
    }

    return result;
}

uint64_t syscall_handle_fd_write(uint32_t fd,
                                 const struct syscall_user_buffer *buffer,
                                 const struct syscall_frame *frame) {
    uint64_t start;
    uint64_t result;

    (void)frame;

    if (g_sys_write_profile == 0u) {
        g_sys_write_profile = kernel_profile_register("sys.write");
    }

    start = kernel_profile_clock();
    result = syscall_handle_write_chunked(fd, buffer);
    kernel_profile_record(g_sys_write_profile,
                          kernel_profile_clock() - start,
                          (int64_t)result > 0 ? result : 0u);

    /*
     * Only WOULD_BLOCK sleeps. Successful writes return normally.
     */
    if ((int64_t)result == KERNEL_FILE_IO_WOULD_BLOCK) {
        return syscall_sleep_would_block(frame, result);
    }

    return result;
}

uint64_t syscall_handle_clear(void) {
    struct process *proc = process_current_mut();
    struct tty *tty;

    if (proc == 0) {
        return 0;
    }
    tty = (struct tty *)file_tty_private_handle(file_table_active(proc->files, PROCESS_FILE_MAX, SYS_FD_STDOUT));
    if (tty == 0) {
        tty = (struct tty *)file_tty_private_handle(file_table_active(proc->files, PROCESS_FILE_MAX, SYS_FD_STDIN));
    }
    if (tty == 0) {
        tty = (struct tty *)file_tty_private_handle(file_table_active(proc->files, PROCESS_FILE_MAX, SYS_FD_STDERR));
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

uint64_t syscall_handle_fd_read(uint32_t fd,
                                const struct syscall_user_buffer *buffer,
                                uint32_t flags,
                                const struct syscall_frame *frame) {
    uint32_t chunk_size;
    uint32_t copied = 0;
    uint64_t rc;

    if (fd == SYS_FD_STDIN) {
        rc = syscall_handle_read(buffer, flags);

        if ((int64_t)rc == KERNEL_FILE_IO_WOULD_BLOCK) {
            return rc;
            //return syscall_sleep_would_block(frame, rc);
        }

        return rc;
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

    if ((int64_t)rc == KERNEL_FILE_IO_WOULD_BLOCK) {
        //return syscall_sleep_would_block(frame, rc);
        return rc;
    }

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

uint64_t syscall_handle_seek(uint32_t fd, int64_t offset, uint32_t whence) {
    return (uint64_t)fs_service_seek(process_current_mut(), fd, offset, whence);
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
