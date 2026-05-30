#include "kernel/internal/fs/file_device_backend.h"
#include "drivers/serial/uart.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/job_control.h"

static void file_device_init_with_ops(struct file *file, uint8_t kind, const struct file_ops *ops) {
    if (file == 0) {
        return;
    }
    file_reset(file);
    file->kind = kind;
    file->ops = ops;
}

static int64_t file_device_reset_and_ok(struct file *file) {
    file_reset(file);
    return 0;
}

static int64_t file_device_readdir_unsupported(struct file *file,
                                               const struct vfs *vfs,
                                               struct vfs_dirent *entry) {
    (void)file;
    (void)vfs;
    (void)entry;
    return -1;
}

static int file_device_tty_foreground_allowed(const struct tty *tty) {
    const struct process *proc;
    uint32_t foreground_pid;

    if (tty == NULL) {
        return 0;
    }
    proc = process_current();
    if (proc == NULL) {
        return 1;
    }
    foreground_pid = tty_foreground_pid(tty);
    return foreground_pid != 0u && proc->pid == foreground_pid;
}

static int64_t file_device_tty_read(struct file *file,
                                    const struct vfs *vfs,
                                    void *buffer,
                                    uint32_t size,
                                    uint32_t flags) {
    struct tty *tty = (struct tty *)file->private_data;
    uint32_t mode;

    (void)file;
    (void)vfs;
    if (tty == NULL || buffer == NULL || size == 0) {
        return 0;
    }
    if (!file_device_tty_foreground_allowed(tty)) {
        return 0;
    }
    mode = (flags & KERNEL_FILE_READ_CHAR) != 0 ? TTY_READ_CHAR : TTY_READ_LINE;
    tty_set_raw_input(tty, (flags & KERNEL_FILE_READ_CHAR) != 0);
    return tty_read(tty, (char *)buffer, size, mode);
}

static int64_t file_device_tty_write(struct file *file,
                                     const struct vfs *vfs,
                                     const void *buffer,
                                     uint32_t size) {
    struct tty *tty = (struct tty *)file->private_data;

    (void)file;
    (void)vfs;
    if (tty == NULL || buffer == NULL || size == 0) {
        return 0;
    }
    if (!file_device_tty_foreground_allowed(tty)) {
        return (int64_t)size;
    }
    return (int64_t)tty_write(tty, (const char *)buffer, size, 0x0f);
}

static int64_t file_device_close(struct file *file) {
    return file_device_reset_and_ok(file);
}

static int64_t file_device_uart_read(struct file *file,
                                     const struct vfs *vfs,
                                     void *buffer,
                                     uint32_t size,
                                     uint32_t flags) {
    (void)file;
    (void)vfs;
    (void)flags;
    if (buffer == 0 || size == 0u) {
        return 0;
    }
    if (!job_serial_current_process_foreground_allowed()) {
        return 0;
    }
    return (int64_t)uart_read_tty((char *)buffer,
                                  size,
                                  (flags & KERNEL_FILE_READ_CHAR) != 0);
}

static int64_t file_device_uart_write(struct file *file,
                                      const struct vfs *vfs,
                                      const void *buffer,
                                      uint32_t size) {
    (void)file;
    (void)vfs;
    if (buffer == 0 || size == 0u) {
        return 0;
    }
    if (!job_serial_current_process_foreground_allowed()) {
        return (int64_t)size;
    }
    return (int64_t)uart_write_buffer((const char *)buffer, size);
}

static const struct file_ops g_file_ops_tty_in = {
    .read = file_device_tty_read,
    .write = 0,
    .close = file_device_close,
    .readdir = file_device_readdir_unsupported,
};

static const struct file_ops g_file_ops_tty_out = {
    .read = 0,
    .write = file_device_tty_write,
    .close = file_device_close,
    .readdir = file_device_readdir_unsupported,
};

static const struct file_ops g_file_ops_devfs_tty = {
    .read = file_device_tty_read,
    .write = file_device_tty_write,
    .close = file_device_close,
    .readdir = file_device_readdir_unsupported,
};

static const struct file_ops g_file_ops_devfs_uart = {
    .read = file_device_uart_read,
    .write = file_device_uart_write,
    .close = file_device_close,
    .readdir = file_device_readdir_unsupported,
};

static struct tty *file_device_tty_for_node(const struct vfs_node *node, void *console_handle) {
    if (node == NULL) {
        return NULL;
    }
    if (node->aux_index == VFS_DEV_TTY2) {
        return tty_virtual(1u);
    }
    if (node->aux_index == VFS_DEV_TTY3) {
        return tty_virtual(2u);
    }
    return (struct tty *)console_handle;
}

static void file_device_bind_vfs_node(struct file *file,
                                      const struct vfs_node *node,
                                      const struct file_ops *ops) {
    if (file == NULL || node == NULL) {
        return;
    }
    file_device_init_with_ops(file, KERNEL_FILE_VFS, ops);
    file->vfs_node = *node;
}

void file_init_console_in(struct file *file, void *console_handle) {
    file_device_init_with_ops(file, KERNEL_FILE_TTY_STDIN, &g_file_ops_tty_in);
    file->private_data = console_handle;
}

void file_init_console_out(struct file *file, void *console_handle) {
    file_device_init_with_ops(file, KERNEL_FILE_TTY_STDOUT, &g_file_ops_tty_out);
    file->private_data = console_handle;
}

void file_init_console_err(struct file *file, void *console_handle) {
    file_device_init_with_ops(file, KERNEL_FILE_TTY_STDERR, &g_file_ops_tty_out);
    file->private_data = console_handle;
}

int file_device_backend_bind(struct file *file, const struct vfs_node *node, void *console_handle) {
    if (file == NULL || node == NULL || node->mount_kind != VFS_MOUNT_DEVFS) {
        return 0;
    }
    if (node->aux_index == VFS_DEV_TTY ||
        node->aux_index == VFS_DEV_TTY2 ||
        node->aux_index == VFS_DEV_TTY3) {
        file_device_bind_vfs_node(file, node, &g_file_ops_devfs_tty);
        file->private_data = file_device_tty_for_node(node, console_handle);
        return 1;
    }
    if (node->aux_index == VFS_DEV_STDIN) {
        file_device_bind_vfs_node(file, node, &g_file_ops_tty_in);
        file->private_data = console_handle;
        return 1;
    }
    if (node->aux_index == VFS_DEV_STDOUT) {
        file_device_bind_vfs_node(file, node, &g_file_ops_tty_out);
        file->private_data = console_handle;
        return 1;
    }
    if (node->aux_index == VFS_DEV_STDERR) {
        file_device_bind_vfs_node(file, node, &g_file_ops_tty_out);
        file->private_data = console_handle;
        return 1;
    }
    if (node->aux_index == VFS_DEV_TTYS0) {
        uart_set_console_input_enabled(0);
        file_device_bind_vfs_node(file, node, &g_file_ops_devfs_uart);
        return 1;
    }
    return 0;
}
