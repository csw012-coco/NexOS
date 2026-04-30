#pragma once

#include "kernel/internal/fs/file_internal.h"

int file_device_backend_bind(struct file *file, const struct vfs_node *node, void *console_handle);
