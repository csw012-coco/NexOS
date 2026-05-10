#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/fs/file_device_backend.h"
#include "fs/vfs.h"

static void file_init_with_ops(struct file *file, uint8_t kind, const struct file_ops *ops) {
    if (file == 0) {
        return;
    }
    file_reset(file);
    file->kind = kind;
    file->ops = ops;
}

static int64_t file_reset_and_ok(struct file *file) {
    file_reset(file);
    return 0;
}

static int64_t file_vfs_read(struct file *file,
                             const struct vfs *vfs,
                             void *buffer,
                             uint32_t size,
                             uint32_t flags) {
    uint32_t vfs_flags = VFS_READ_BLOCKING;

    if ((flags & KERNEL_FILE_READ_NONBLOCK) != 0) {
        vfs_flags |= VFS_READ_NONBLOCK;
    }
    if ((flags & KERNEL_FILE_READ_CHAR) != 0) {
        vfs_flags |= VFS_READ_CHAR;
    }
    return vfs_read((struct vfs *)vfs, &file->vfs_node, &file->offset, buffer, size, vfs_flags);
}

static int64_t file_vfs_write(struct file *file,
                              const struct vfs *vfs,
                              const void *buffer,
                              uint32_t size) {
    return vfs_write((struct vfs *)vfs, &file->vfs_node, &file->offset, buffer, size, file->opened_path);
}

static int64_t file_vfs_close(struct file *file) {
    return file_reset_and_ok(file);
}

static int64_t file_vfs_readdir(struct file *file,
                                const struct vfs *vfs,
                                struct vfs_dirent *entry) {
    return vfs_readdir((struct vfs *)vfs, &file->vfs_node, &file->dir_index, entry);
}

static const struct file_ops g_file_ops_vfs = {
    .read = file_vfs_read,
    .write = file_vfs_write,
    .close = file_vfs_close,
    .readdir = file_vfs_readdir,
};

void file_init_vfs(struct file *file, const struct vfs_node *node, const char *opened_path, void *console_handle) {
    if (file_device_backend_bind(file, node, console_handle)) {
        return;
    }
    file_init_with_ops(file, KERNEL_FILE_VFS, &g_file_ops_vfs);
    if (node != 0) {
        file->vfs_node = *node;
    }
    if (opened_path != 0) {
        uint32_t i = 0;

        while (opened_path[i] != '\0' && i + 1u < sizeof(file->opened_path)) {
            file->opened_path[i] = opened_path[i];
            i++;
        }
        file->opened_path[i] = '\0';
    }
}
