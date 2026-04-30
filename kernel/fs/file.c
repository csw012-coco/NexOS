#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/fs/file_pipe_backend.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"

struct file *file_table_slot(struct file *table, uint32_t count, uint32_t fd) {
    if (table == 0 || fd >= count) {
        return 0;
    }
    return &table[fd];
}

struct file *file_table_active(struct file *table, uint32_t count, uint32_t fd) {
    struct file *file = file_table_slot(table, count, fd);

    return file != 0 && file_is_active(file) ? file : 0;
}

struct file *file_table_alloc(struct file *table, uint32_t count, uint32_t start_index, uint32_t *fd_out) {
    if (table == 0 || fd_out == 0 || start_index >= count) {
        return 0;
    }
    for (uint32_t i = start_index; i < count; i++) {
        if (!file_is_active(&table[i])) {
            *fd_out = i;
            return &table[i];
        }
    }
    return 0;
}

int file_table_open_vfs(struct file *table,
                        uint32_t count,
                        uint32_t start_index,
                        const struct vfs_node *node,
                        const char *opened_path,
                        void *console_handle,
                        uint32_t *fd_out,
                        struct file **handle_out) {
    struct file *opened_file = file_table_alloc(table, count, start_index, fd_out);

    if (opened_file == 0) {
        return 0;
    }
    file_init_vfs(opened_file, node, opened_path, console_handle);
    if (handle_out != 0) {
        *handle_out = opened_file;
    }
    return 1;
}

void file_reset(struct file *file) {
    if (file == 0) {
        return;
    }
    file->kind = KERNEL_FILE_NONE;
    file->offset = 0;
    file->dir_index = 0;
    vfs_node_reset(&file->vfs_node);
    file->opened_path[0] = '\0';
    file->private_data = 0;
    file->ops = 0;
}

int file_table_open_pipe_pair(struct file *table,
                              uint32_t count,
                              uint32_t start_index,
                              uint32_t pair_out[2]) {
    struct file *read_file;
    struct file *write_file;
    uint32_t read_fd;
    uint32_t write_fd;

    if (table == 0 || pair_out == 0) {
        return 0;
    }
    read_file = file_table_alloc(table, count, start_index, &read_fd);
    if (read_file == 0) {
        return 0;
    }
    write_file = file_table_alloc(table, count, start_index, &write_fd);
    if (write_file == 0 || write_fd == read_fd) {
        for (uint32_t i = read_fd + 1u; i < count; i++) {
            if (!file_is_active(&table[i])) {
                write_file = &table[i];
                write_fd = i;
                break;
            }
        }
    }
    if (write_file == 0 || write_fd == read_fd) {
        return 0;
    }
    if (!file_init_pipe_pair(read_file, write_file)) {
        file_discard(read_file);
        file_discard(write_file);
        return 0;
    }
    pair_out[0] = read_fd;
    pair_out[1] = write_fd;
    return 1;
}

void file_set_offset(struct file *file, uint32_t offset) {
    if (file == 0) {
        return;
    }
    file->offset = offset;
}

int file_is_active(const struct file *file) {
    return file != 0 && file->kind != KERNEL_FILE_NONE && file->ops != 0;
}

int file_init_pipe_pair(struct file *read_file, struct file *write_file) {
    return file_pipe_backend_init_pair(read_file, write_file);
}

void file_discard(struct file *file) {
    if (file == 0 || !file_is_active(file)) {
        return;
    }
    if (file_pipe_backend_is_kind(file->kind)) {
        file_pipe_backend_discard(file);
        return;
    }
    file_reset(file);
}

int file_clone(struct file *dst, const struct file *src) {
    if (dst == 0 || src == 0 || !file_is_active(src)) {
        return 0;
    }
    file_discard(dst);
    if (file_pipe_backend_is_kind(src->kind)) {
        return file_pipe_backend_clone(dst, src);
    }
    *dst = *src;
    return 1;
}

int file_read_would_block(const struct file *file) {
    if (file != 0) {
        if (file->kind == KERNEL_FILE_TTY_STDIN) {
            return 1;
        }
        if (file->kind == KERNEL_FILE_VFS &&
            file->vfs_node.mount_kind == VFS_MOUNT_DEVFS &&
            (file->vfs_node.aux_index == VFS_DEV_TTY ||
             file->vfs_node.aux_index == VFS_DEV_STDIN)) {
            return 1;
        }
    }
    return file_pipe_backend_read_would_block(file);
}

int file_write_would_block(const struct file *file) {
    return file_pipe_backend_write_would_block(file);
}

int64_t file_read(struct file *file,
                  const struct vfs *vfs,
                  void *buffer,
                  uint32_t size,
                  uint32_t flags) {
    if (!file_is_active(file) || file->ops->read == 0) {
        return -1;
    }
    return file->ops->read(file, vfs, buffer, size, flags);
}

int64_t file_write(struct file *file,
                   const struct vfs *vfs,
                   const void *buffer,
                   uint32_t size) {
    if (!file_is_active(file) || file->ops->write == 0) {
        return -1;
    }
    return file->ops->write(file, vfs, buffer, size);
}

int64_t file_close(struct file *file) {
    if (!file_is_active(file) || file->ops->close == 0) {
        return -1;
    }
    return file->ops->close(file);
}

int64_t file_readdir(struct file *file,
                     const struct vfs *vfs,
                     struct vfs_dirent *entry) {
    if (!file_is_active(file) || file->ops->readdir == 0) {
        return -1;
    }
    return file->ops->readdir(file, vfs, entry);
}
