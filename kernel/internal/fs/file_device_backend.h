#pragma once

#include "kernel/internal/fs/file_internal.h"

struct file_device_backend_runtime_ops {
    int (*serial_foreground_allowed)(void);
};

void file_device_backend_runtime_ops_register(
    const struct file_device_backend_runtime_ops *ops);
int file_device_backend_bind(struct file *file, const struct vfs_node *node, void *console_handle);
