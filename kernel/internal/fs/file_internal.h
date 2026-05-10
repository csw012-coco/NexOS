#pragma once

#include <stdint.h>
#include "kernel/public/fs/vfs_types.h"

enum kernel_file_kind {
    KERNEL_FILE_NONE = 0,
    KERNEL_FILE_TTY_STDIN = 1,
    KERNEL_FILE_TTY_STDOUT = 2,
    KERNEL_FILE_TTY_STDERR = 3,
    KERNEL_FILE_VFS = 4,
    KERNEL_FILE_PIPE_READ = 5,
    KERNEL_FILE_PIPE_WRITE = 6
};

enum kernel_file_read_flags {
    KERNEL_FILE_READ_BLOCKING = 0,
    KERNEL_FILE_READ_NONBLOCK = 1u,
    KERNEL_FILE_READ_CHAR = 2u
};

struct file;
struct vfs;

struct file_ops {
    int64_t (*read)(struct file *file,
                    const struct vfs *vfs,
                    void *buffer,
                    uint32_t size,
                    uint32_t flags);
    int64_t (*write)(struct file *file,
                     const struct vfs *vfs,
                     const void *buffer,
                     uint32_t size);
    int64_t (*close)(struct file *file);
    int64_t (*readdir)(struct file *file,
                       const struct vfs *vfs,
                       struct vfs_dirent *entry);
};

struct file {
    uint8_t kind;
    uint32_t offset;
    uint32_t dir_index;
    struct vfs_node vfs_node;
    char opened_path[NOS_PATH_BUFFER_SIZE];
    void *private_data;
    const struct file_ops *ops;
};

void file_reset(struct file *file);
void file_init_console_in(struct file *file, void *console_handle);
void file_init_console_out(struct file *file, void *console_handle);
void file_init_console_err(struct file *file, void *console_handle);
void file_init_vfs(struct file *file, const struct vfs_node *node, const char *opened_path, void *console_handle);
struct file *file_table_slot(struct file *table, uint32_t count, uint32_t fd);
struct file *file_table_active(struct file *table, uint32_t count, uint32_t fd);
struct file *file_table_alloc(struct file *table, uint32_t count, uint32_t start_index, uint32_t *fd_out);
int file_table_open_vfs(struct file *table,
                        uint32_t count,
                        uint32_t start_index,
                        const struct vfs_node *node,
                        const char *opened_path,
                        void *console_handle,
                        uint32_t *fd_out,
                        struct file **handle_out);
int file_table_open_pipe_pair(struct file *table,
                              uint32_t count,
                              uint32_t start_index,
                              uint32_t pair_out[2]);
int file_init_pipe_pair(struct file *read_file, struct file *write_file);
int file_clone(struct file *dst, const struct file *src);
void file_discard(struct file *file);
void file_set_offset(struct file *file, uint32_t offset);
int file_is_active(const struct file *file);
int file_read_would_block(const struct file *file);
int file_write_would_block(const struct file *file);
int64_t file_close(struct file *file);

int64_t file_read(struct file *file,
                  const struct vfs *vfs,
                  void *buffer,
                  uint32_t size,
                  uint32_t flags);
int64_t file_write(struct file *file,
                   const struct vfs *vfs,
                   const void *buffer,
                   uint32_t size);
int64_t file_readdir(struct file *file,
                     const struct vfs *vfs,
                     struct vfs_dirent *entry);
