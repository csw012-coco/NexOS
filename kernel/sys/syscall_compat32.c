#include "kernel/internal/core/tty_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "fs/vfs_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "lib/string.h"

static void syscall_compat32_drain_tty_input(struct syscall_compat32_context *ctx) {
    struct keyboard_event event;

    if (ctx == 0 || ctx->tty == 0 || ctx->pop_keyboard_event == 0) {
        return;
    }
    while (ctx->pop_keyboard_event(&event)) {
        tty_feed_key_event(ctx->tty, &event);
    }
}

static int syscall_compat32_fd_is_tty_input(struct syscall_compat32_context *ctx, uint32_t fd) {
    struct syscall_fd_info info;

    if (ctx == 0 || ctx->fd_kind == 0) {
        return 0;
    }
    if (ctx->fd_kind(fd) == KERNEL_FILE_TTY_STDIN) {
        return 1;
    }
    if (ctx->fd_query == 0 || ctx->fd_query(fd, &info) <= 0) {
        return 0;
    }
    if (info.kind != KERNEL_FILE_VFS ||
        info.mount_kind != VFS_MOUNT_DEVFS ||
        info.readable == 0u) {
        return 0;
    }
    return streq(info.path, "/dev/tty") ||
           streq(info.path, "/dev/tty1") ||
           streq(info.path, "/dev/stdin");
}

uint32_t syscall_compat32_open(struct syscall_compat32_context *ctx,
                                uint32_t user_path,
                                uint32_t flags) {
    char path[NOS_PATH_BUFFER_SIZE];

    if (ctx == 0 || ctx->open == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(path, user_path, sizeof(path))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->open(ctx->vfs, path, flags);
}

uint32_t syscall_compat32_read(struct syscall_compat32_context *ctx,
                                uint32_t fd,
                                uint32_t user_address,
                                uint32_t size,
                                uint32_t flags) {
    if (ctx == 0 || ctx->io_buffer == 0 || ctx->io_buffer_size == 0u ||
        ctx->read == 0 || ctx->fd_kind == 0 ||
        size == 0u || size > ctx->io_buffer_size) {
        return (uint32_t)-1;
    }
    if (syscall_compat32_fd_is_tty_input(ctx, fd)) {
        char *buffer = ctx->io_buffer;
        uint32_t mode =
            (flags & SYS_READ_CHAR) != 0u ? TTY_READ_CHAR : TTY_READ_LINE;

        if (ctx->tty == 0) {
            return (uint32_t)-1;
        }
        tty_set_raw_input(ctx->tty, mode == TTY_READ_CHAR);
        syscall_compat32_drain_tty_input(ctx);
        if ((flags & SYS_READ_NONBLOCK) == 0u) {
            while ((mode == TTY_READ_LINE && !tty_has_line(ctx->tty)) ||
                   (mode == TTY_READ_CHAR &&
                    tty_read(ctx->tty, buffer, size, mode) == 0u)) {
                arch->wait_for_interrupt();
                syscall_compat32_drain_tty_input(ctx);
            }
            if (mode == TTY_READ_CHAR) {
                return arch_copy_to_user(user_address, buffer, 1u)
                    ? 1u
                    : (uint32_t)-1;
            }
        } else if (mode == TTY_READ_LINE && !tty_has_line(ctx->tty)) {
            return 0u;
        }
        {
            uint32_t count = tty_read(ctx->tty, buffer, size, mode);

            if (count == 0u) {
                return 0u;
            }
            if (mode == TTY_READ_LINE) {
                if (count == 1u && buffer[0] == '\0') {
                    buffer[0] = '\n';
                    return arch_copy_to_user(user_address, buffer, 1u)
                        ? 1u
                        : (uint32_t)-1;
                }
                if (count < size) {
                    buffer[count] = '\n';
                    count++;
                }
            }
            return arch_copy_to_user(user_address, buffer, count)
                ? count
                : (uint32_t)-1;
        }
    }
    {
        int32_t scheduled = ctx->read(ctx->vfs,
                                      fd,
                                      ctx->io_buffer,
                                      size,
                                      flags);

        if (scheduled != -2) {
            if (scheduled <= 0) {
                return (uint32_t)scheduled;
            }
            return arch_copy_to_user(user_address,
                                     ctx->io_buffer,
                                     (uint32_t)scheduled)
                ? (uint32_t)scheduled
                : (uint32_t)-1;
        }
    }
    return (uint32_t)-2;
}

uint32_t syscall_compat32_write(struct syscall_compat32_context *ctx,
                                 uint32_t fd,
                                 uint32_t user_address,
                                 uint32_t size) {
    if (ctx == 0 || ctx->io_buffer == 0 || ctx->io_buffer_size == 0u ||
        ctx->write == 0 || ctx->fd_kind == 0 ||
        size == 0u || size > ctx->io_buffer_size ||
        !arch_copy_from_user(ctx->io_buffer, user_address, size)) {
        return (uint32_t)-1;
    }
    {
        int32_t scheduled = ctx->write(ctx->vfs, fd, ctx->io_buffer, size);

        if (scheduled != -2) {
            return (uint32_t)scheduled;
        }
    }
    if (ctx->fd_kind(fd) == KERNEL_FILE_TTY_STDOUT ||
        ctx->fd_kind(fd) == KERNEL_FILE_TTY_STDERR) {
        if (ctx->tty == 0) {
            return (uint32_t)-1;
        }
        return tty_write(ctx->tty,
                         ctx->io_buffer,
                         size,
                         ctx->fd_kind(fd) == KERNEL_FILE_TTY_STDERR
                             ? 0x0cu
                             : 0x0fu);
    }
    return (uint32_t)-1;
}

uint32_t syscall_compat32_close(struct syscall_compat32_context *ctx,
                                 uint32_t fd) {
    if (ctx == 0 || ctx->close == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->close(fd);
}

uint32_t syscall_compat32_seek(struct syscall_compat32_context *ctx,
                                uint32_t fd,
                                int32_t offset,
                                uint32_t whence) {
    if (ctx == 0 || ctx->seek == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->seek(fd, offset, whence);
}

uint32_t syscall_compat32_page_alloc(struct syscall_compat32_context *ctx) {
    if (ctx == 0 || ctx->page_alloc == 0) {
        return 0u;
    }
    return ctx->page_alloc();
}

uint32_t syscall_compat32_page_free(struct syscall_compat32_context *ctx,
                                     uint32_t user_page) {
    if (ctx == 0 || ctx->page_free == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->page_free(user_page);
}

uint32_t syscall_compat32_spawn(struct syscall_compat32_context *ctx,
                                 uint32_t user_command,
                                 uint32_t mode,
                                 uint32_t flags) {
    char command[512];

    if (ctx == 0 || ctx->spawn_command == 0 ||
        (mode != SYS_SPAWN_AUTO && mode != SYS_SPAWN_ELF) ||
        (flags & ~SYS_SPAWN_BACKGROUND) != 0u ||
        !arch_copy_user_cstr(command, user_command, sizeof(command))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->spawn_command(command, mode, flags);
}

uint32_t syscall_compat32_exec(struct syscall_compat32_context *ctx,
                                uint32_t user_command) {
    return syscall_compat32_spawn(ctx, user_command, SYS_SPAWN_AUTO, 0u);
}

uintptr_t syscall_compat32_wait(struct syscall_compat32_context *ctx,
                                 const struct process_context *context,
                                 uint32_t pid,
                                 int32_t *status,
                                 int *blocked) {
    if (ctx == 0 || ctx->wait == 0 ||
        context == 0 || status == 0 || blocked == 0) {
        return 0u;
    }
    return ctx->wait(context, pid, status, blocked);
}

uintptr_t syscall_compat32_exit(struct syscall_compat32_context *ctx,
                                 const struct process_context *context,
                                 int exit_code) {
    if (ctx == 0 || ctx->exit == 0 || context == 0) {
        return 0u;
    }
    syscall_compat32_cleanup_pid(ctx, ctx->pid);
    return ctx->exit(context, exit_code);
}

uintptr_t syscall_compat32_yield(struct syscall_compat32_context *ctx,
                                  const struct process_context *context) {
    if (ctx == 0 || ctx->yield == 0 || context == 0) {
        return 0u;
    }
    return ctx->yield(context);
}

uintptr_t syscall_compat32_sleep(struct syscall_compat32_context *ctx,
                                  const struct process_context *context,
                                  uint32_t ticks) {
    if (ctx == 0 || ctx->sleep == 0 || context == 0) {
        return 0u;
    }
    return ctx->sleep(context, ticks);
}

uint32_t syscall_compat32_dup2(struct syscall_compat32_context *ctx,
                                uint32_t src_fd,
                                uint32_t dst_fd) {
    if (ctx == 0 || ctx->dup2 == 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->dup2(src_fd, dst_fd);
}
