#include "kernel/internal/fs/file_device_backend.h"
#include "kernel/internal/fs/file_internal.h"
#include "fs/vfs_internal.h"
#include "kernel/public/core/tty.h"

static struct tty *file_i386_tty_handle(void *console_handle) {
    if (console_handle != 0) {
        return (struct tty *)console_handle;
    }
    return tty_active();
}

static int64_t file_i386_console_close(struct file *file) {
    file_reset(file);
    return 0;
}

static int64_t file_i386_tty_read(struct file *file,
                                  const struct vfs *vfs,
                                  void *buffer,
                                  uint32_t size,
                                  uint32_t flags) {
    struct tty *tty = (struct tty *)file->private_data;
    uint32_t mode;

    (void)vfs;
    if (tty == 0 || buffer == 0 || size == 0u) {
        return 0;
    }
    mode = (flags & KERNEL_FILE_READ_CHAR) != 0 ? TTY_READ_CHAR : TTY_READ_LINE;
    tty_set_raw_input(tty, (flags & KERNEL_FILE_READ_CHAR) != 0);
    return tty_read(tty, (char *)buffer, size, mode);
}

static int64_t file_i386_tty_write(struct file *file,
                                   const struct vfs *vfs,
                                   const void *buffer,
                                   uint32_t size) {
    struct tty *tty = (struct tty *)file->private_data;

    (void)vfs;
    if (tty == 0 || buffer == 0 || size == 0u) {
        return 0;
    }
    return (int64_t)tty_write(tty, (const char *)buffer, size, 0x0fu);
}

static int64_t file_i386_readdir_unsupported(struct file *file,
                                             const struct vfs *vfs,
                                             struct vfs_dirent *entry) {
    (void)file;
    (void)vfs;
    (void)entry;
    return -1;
}

static const struct file_ops file_i386_tty_in_ops = {
    .read = file_i386_tty_read,
    .write = 0,
    .close = file_i386_console_close,
    .readdir = file_i386_readdir_unsupported,
};

static const struct file_ops file_i386_tty_out_ops = {
    .read = 0,
    .write = file_i386_tty_write,
    .close = file_i386_console_close,
    .readdir = file_i386_readdir_unsupported,
};

static const struct file_ops file_i386_devfs_tty_ops = {
    .read = file_i386_tty_read,
    .write = file_i386_tty_write,
    .close = file_i386_console_close,
    .readdir = file_i386_readdir_unsupported,
};

static void file_i386_init_console(struct file *file,
                                   uint8_t kind,
                                   void *console_handle,
                                   const struct file_ops *ops) {
    file_reset(file);
    file->kind = kind;
    file->private_data = file_i386_tty_handle(console_handle);
    file->ops = ops;
}

void file_init_console_in(struct file *file, void *console_handle) {
    file_i386_init_console(file, KERNEL_FILE_TTY_STDIN, console_handle, &file_i386_tty_in_ops);
}

void file_init_console_out(struct file *file, void *console_handle) {
    file_i386_init_console(file, KERNEL_FILE_TTY_STDOUT, console_handle, &file_i386_tty_out_ops);
}

void file_init_console_err(struct file *file, void *console_handle) {
    file_i386_init_console(file, KERNEL_FILE_TTY_STDERR, console_handle, &file_i386_tty_out_ops);
}

int file_device_backend_bind(struct file *file,
                             const struct vfs_node *node,
                             void *console_handle) {
    if (file == 0 || node == 0 || node->mount_kind != VFS_MOUNT_DEVFS) {
        return 0;
    }
    if (node->aux_index == VFS_DEV_TTY ||
        node->aux_index == VFS_DEV_TTY2 ||
        node->aux_index == VFS_DEV_TTY3) {
        file_reset(file);
        file->kind = KERNEL_FILE_VFS;
        file->vfs_node = *node;
        file->private_data = file_i386_tty_handle(console_handle);
        file->ops = &file_i386_devfs_tty_ops;
        return 1;
    }
    if (node->aux_index == VFS_DEV_STDIN) {
        file_reset(file);
        file->kind = KERNEL_FILE_VFS;
        file->vfs_node = *node;
        file->private_data = file_i386_tty_handle(console_handle);
        file->ops = &file_i386_tty_in_ops;
        return 1;
    }
    if (node->aux_index == VFS_DEV_STDOUT ||
        node->aux_index == VFS_DEV_STDERR) {
        file_reset(file);
        file->kind = KERNEL_FILE_VFS;
        file->vfs_node = *node;
        file->private_data = file_i386_tty_handle(console_handle);
        file->ops = &file_i386_tty_out_ops;
        return 1;
    }
    return 0;
}
