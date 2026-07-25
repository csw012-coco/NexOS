#include "kernel/public/proc/process_file_ops.h"

static const struct process_file_ops *g_process_file_ops;

void process_file_ops_register(const struct process_file_ops *ops) {
    g_process_file_ops = ops;
}

int32_t process_file_open(struct vfs *vfs, const char *path, uint32_t flags) {
    return g_process_file_ops != 0 && g_process_file_ops->open != 0
               ? g_process_file_ops->open(vfs, path, flags)
               : -1;
}

int32_t process_file_opendir(struct vfs *vfs, const char *path) {
    return g_process_file_ops != 0 && g_process_file_ops->opendir != 0
               ? g_process_file_ops->opendir(vfs, path)
               : -1;
}

int32_t process_file_readdir(struct vfs *vfs,
                             uint32_t fd,
                             struct syscall_dirent *entry) {
    return g_process_file_ops != 0 && g_process_file_ops->readdir != 0
               ? g_process_file_ops->readdir(vfs, fd, entry)
               : -1;
}

int32_t process_file_chdir(struct vfs *vfs, const char *path) {
    return g_process_file_ops != 0 && g_process_file_ops->chdir != 0
               ? g_process_file_ops->chdir(vfs, path)
               : -1;
}

int32_t process_file_getcwd(char *buffer, uint32_t size) {
    return g_process_file_ops != 0 && g_process_file_ops->getcwd != 0
               ? g_process_file_ops->getcwd(buffer, size)
               : -1;
}

int32_t process_file_mkdir(struct vfs *vfs, const char *path) {
    return g_process_file_ops != 0 && g_process_file_ops->mkdir != 0
               ? g_process_file_ops->mkdir(vfs, path)
               : -1;
}

int32_t process_file_rmdir(struct vfs *vfs, const char *path) {
    return g_process_file_ops != 0 && g_process_file_ops->rmdir != 0
               ? g_process_file_ops->rmdir(vfs, path)
               : -1;
}

int32_t process_file_remove(struct vfs *vfs, const char *path) {
    return g_process_file_ops != 0 && g_process_file_ops->remove != 0
               ? g_process_file_ops->remove(vfs, path)
               : -1;
}

int32_t process_file_read(struct vfs *vfs,
                          uint32_t fd,
                          void *buffer,
                          uint32_t size,
                          uint32_t flags) {
    return g_process_file_ops != 0 && g_process_file_ops->read != 0
               ? g_process_file_ops->read(vfs, fd, buffer, size, flags)
               : -1;
}

int32_t process_file_write(struct vfs *vfs,
                           uint32_t fd,
                           const void *buffer,
                           uint32_t size) {
    return g_process_file_ops != 0 && g_process_file_ops->write != 0
               ? g_process_file_ops->write(vfs, fd, buffer, size)
               : -1;
}

int32_t process_file_seek(uint32_t fd, int32_t offset, uint32_t whence) {
    return g_process_file_ops != 0 && g_process_file_ops->seek != 0
               ? g_process_file_ops->seek(fd, offset, whence)
               : -1;
}

int32_t process_file_pipe(uint32_t pair[2]) {
    return g_process_file_ops != 0 && g_process_file_ops->pipe != 0
               ? g_process_file_ops->pipe(pair)
               : -1;
}

int32_t process_file_dup2(uint32_t src_fd, uint32_t dst_fd) {
    return g_process_file_ops != 0 && g_process_file_ops->dup2 != 0
               ? g_process_file_ops->dup2(src_fd, dst_fd)
               : -1;
}

uint32_t process_file_fd_kind(uint32_t fd) {
    return g_process_file_ops != 0 && g_process_file_ops->fd_kind != 0
               ? g_process_file_ops->fd_kind(fd)
               : 0u;
}

int32_t process_file_fd_query(uint32_t fd, struct syscall_fd_info *info) {
    return g_process_file_ops != 0 && g_process_file_ops->fd_query != 0
               ? g_process_file_ops->fd_query(fd, info)
               : -1;
}

int32_t process_file_close(uint32_t fd) {
    return g_process_file_ops != 0 && g_process_file_ops->close != 0
               ? g_process_file_ops->close(fd)
               : -1;
}
