#include "kernel/internal/fs/file_device_backend.h"
#include "kernel/internal/fs/file_internal.h"

static int64_t file_i386_console_close(struct file *file) {
    file_reset(file);
    return 0;
}

static const struct file_ops file_i386_console_ops = {
    .read = 0,
    .write = 0,
    .close = file_i386_console_close,
    .readdir = 0,
};

static void file_i386_init_console(struct file *file,
                                   uint8_t kind,
                                   void *console_handle) {
    file_reset(file);
    file->kind = kind;
    file->private_data = console_handle;
    file->ops = &file_i386_console_ops;
}

void file_init_console_in(struct file *file, void *console_handle) {
    file_i386_init_console(file, KERNEL_FILE_TTY_STDIN, console_handle);
}

void file_init_console_out(struct file *file, void *console_handle) {
    file_i386_init_console(file, KERNEL_FILE_TTY_STDOUT, console_handle);
}

void file_init_console_err(struct file *file, void *console_handle) {
    file_i386_init_console(file, KERNEL_FILE_TTY_STDERR, console_handle);
}

int file_device_backend_bind(struct file *file,
                             const struct vfs_node *node,
                             void *console_handle) {
    (void)file;
    (void)node;
    (void)console_handle;
    return 0;
}
