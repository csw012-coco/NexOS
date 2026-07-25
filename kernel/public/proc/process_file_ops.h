#pragma once

#include <stdint.h>

struct syscall_dirent;
struct syscall_fd_info;
struct vfs;

struct process_file_ops {
    int32_t (*open)(struct vfs *vfs, const char *path, uint32_t flags);
    int32_t (*opendir)(struct vfs *vfs, const char *path);
    int32_t (*readdir)(struct vfs *vfs,
                       uint32_t fd,
                       struct syscall_dirent *entry);
    int32_t (*chdir)(struct vfs *vfs, const char *path);
    int32_t (*getcwd)(char *buffer, uint32_t size);
    int32_t (*mkdir)(struct vfs *vfs, const char *path);
    int32_t (*rmdir)(struct vfs *vfs, const char *path);
    int32_t (*remove)(struct vfs *vfs, const char *path);
    int32_t (*read)(struct vfs *vfs,
                    uint32_t fd,
                    void *buffer,
                    uint32_t size,
                    uint32_t flags);
    int32_t (*write)(struct vfs *vfs,
                     uint32_t fd,
                     const void *buffer,
                     uint32_t size);
    int32_t (*seek)(uint32_t fd, int32_t offset, uint32_t whence);
    int32_t (*pipe)(uint32_t pair[2]);
    int32_t (*dup2)(uint32_t src_fd, uint32_t dst_fd);
    uint32_t (*fd_kind)(uint32_t fd);
    int32_t (*fd_query)(uint32_t fd, struct syscall_fd_info *info);
    int32_t (*close)(uint32_t fd);
};

void process_file_ops_register(const struct process_file_ops *ops);
int32_t process_file_open(struct vfs *vfs, const char *path, uint32_t flags);
int32_t process_file_opendir(struct vfs *vfs, const char *path);
int32_t process_file_readdir(struct vfs *vfs,
                             uint32_t fd,
                             struct syscall_dirent *entry);
int32_t process_file_chdir(struct vfs *vfs, const char *path);
int32_t process_file_getcwd(char *buffer, uint32_t size);
int32_t process_file_mkdir(struct vfs *vfs, const char *path);
int32_t process_file_rmdir(struct vfs *vfs, const char *path);
int32_t process_file_remove(struct vfs *vfs, const char *path);
int32_t process_file_read(struct vfs *vfs,
                          uint32_t fd,
                          void *buffer,
                          uint32_t size,
                          uint32_t flags);
int32_t process_file_write(struct vfs *vfs,
                           uint32_t fd,
                           const void *buffer,
                           uint32_t size);
int32_t process_file_seek(uint32_t fd, int32_t offset, uint32_t whence);
int32_t process_file_pipe(uint32_t pair[2]);
int32_t process_file_dup2(uint32_t src_fd, uint32_t dst_fd);
uint32_t process_file_fd_kind(uint32_t fd);
int32_t process_file_fd_query(uint32_t fd, struct syscall_fd_info *info);
int32_t process_file_close(uint32_t fd);
