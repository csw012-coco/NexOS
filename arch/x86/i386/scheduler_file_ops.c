#include "abi/syscall_abi.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/fs/path_resolve_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/process_file_ops.h"
#include "scheduler_internal.h"

static void scheduler_file_copy_text(char *dst,
                                     uint32_t size,
                                     const char *src) {
    uint32_t i = 0u;

    if (dst == 0 || size == 0u) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static struct process *scheduler_file_current_process(void) {
    return process_current_mut();
}

static int scheduler_file_resolve_path(struct process *process,
                                       const char *path,
                                       char *resolved,
                                       uint32_t resolved_size) {
    if (process == 0 || path == 0 || resolved == 0 || resolved_size == 0u) {
        return 0;
    }
    return fs_resolve_process_path(process, path, resolved, resolved_size);
}

static uint32_t scheduler_file_vfs_node_size(const struct vfs_node *node) {
    if (node == 0 || node->kind != VFS_NODE_FILE) {
        return 0u;
    }
    if (node->mount_kind == VFS_MOUNT_FAT32) {
        return node->handle.fat32_file.size;
    }
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        return node->handle.nxfs_inode.size;
    }
    return 0u;
}

int32_t i386_scheduler_open(struct vfs *vfs,
                            const char *path,
                            uint32_t flags) {
    struct process *process = scheduler_file_current_process();
    struct vfs_node node;
    struct file *opened_file;
    char resolved[NOS_PATH_BUFFER_SIZE];
    uint32_t vfs_flags = 0u;
    uint32_t access;
    uint32_t fd;

    if (vfs == 0 || path == 0 || process == 0) {
        return -1;
    }
    access = flags & (SYS_OPEN_READ | SYS_OPEN_WRITE);
    if (flags == 0u) {
        access = SYS_OPEN_READ;
    } else if (access == 0u &&
               (flags & (SYS_OPEN_CREAT |
                         SYS_OPEN_TRUNC |
                         SYS_OPEN_APPEND)) != 0u) {
        access = SYS_OPEN_WRITE;
    }
    if (access == 0u ||
        (flags & ~(SYS_OPEN_CREAT |
                   SYS_OPEN_TRUNC |
                   SYS_OPEN_APPEND |
                   SYS_OPEN_READ |
                   SYS_OPEN_WRITE)) != 0u ||
        !scheduler_file_resolve_path(process,
                                     path,
                                     resolved,
                                     sizeof(resolved))) {
        return -1;
    }
    if ((flags & SYS_OPEN_CREAT) != 0u) {
        vfs_flags |= VFS_OPEN_CREATE;
    }
    if ((flags & SYS_OPEN_TRUNC) != 0u) {
        vfs_flags |= VFS_OPEN_TRUNCATE;
    }
    if ((flags & SYS_OPEN_APPEND) != 0u) {
        vfs_flags |= VFS_OPEN_APPEND;
    }
    if (vfs_open(vfs, resolved, vfs_flags, &node) != 0) {
        return -1;
    }
    if (node.mount_kind == VFS_MOUNT_DEVFS) {
        if (node.aux_index == VFS_DEV_TTY ||
            node.aux_index == VFS_DEV_TTY2 ||
            node.aux_index == VFS_DEV_TTY3) {
            access = SYS_OPEN_READ | SYS_OPEN_WRITE;
        } else if (node.aux_index == VFS_DEV_STDIN) {
            access = SYS_OPEN_READ;
        } else if (node.aux_index == VFS_DEV_STDOUT ||
                   node.aux_index == VFS_DEV_STDERR) {
            access = SYS_OPEN_WRITE;
        }
    }
    if (!file_table_open_vfs(process->files,
                             NOS_PROCESS_FILE_MAX,
                             3u,
                             &node,
                             resolved,
                             process->console_handle,
                             &fd,
                             &opened_file)) {
        return -1;
    }
    opened_file->flags =
        (access & SYS_OPEN_READ ? KERNEL_FILE_ACCESS_READ : 0u) |
        (access & SYS_OPEN_WRITE ? KERNEL_FILE_ACCESS_WRITE : 0u);
    if ((flags & SYS_OPEN_APPEND) != 0u) {
        file_set_offset(opened_file, node.handle.fat32_file.size);
    }
    return (int32_t)fd;
}

int32_t i386_scheduler_opendir(struct vfs *vfs, const char *path) {
    struct process *process = scheduler_file_current_process();
    struct vfs_node node;
    char resolved[NOS_PATH_BUFFER_SIZE];
    uint32_t fd;

    if (vfs == 0 || path == 0 || process == 0) {
        return -1;
    }
    if (!scheduler_file_resolve_path(process,
                                     path,
                                     resolved,
                                     sizeof(resolved)) ||
        vfs_opendir(vfs, resolved, &node) != 0) {
        return -1;
    }
    return file_table_open_vfs(process->files,
                               NOS_PROCESS_FILE_MAX,
                               3u,
                               &node,
                               resolved,
                               0,
                               &fd,
                               0)
        ? (int32_t)fd
        : -1;
}

int32_t i386_scheduler_readdir(struct vfs *vfs,
                               uint32_t fd,
                               struct syscall_dirent *entry) {
    struct process *process = scheduler_file_current_process();
    struct file *file;
    struct vfs_dirent vfs_entry;
    int64_t result;

    if (process == 0 || vfs == 0 || entry == 0 ||
        fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &process->files[fd];
    if (file->kind != KERNEL_FILE_VFS ||
        file->vfs_node.kind != VFS_NODE_DIR) {
        return -1;
    }
    result = file_readdir(file, vfs, &vfs_entry);
    if (result <= 0) {
        return (int32_t)result;
    }
    scheduler_file_copy_text(entry->name,
                             sizeof(entry->name),
                             vfs_entry.name);
    entry->size = vfs_entry.size;
    entry->attributes = vfs_entry.attributes;
    return 1;
}

int32_t i386_scheduler_chdir(struct vfs *vfs, const char *path) {
    struct process *process = scheduler_file_current_process();
    struct vfs_node directory;
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (vfs == 0 || path == 0 || process == 0) {
        return -1;
    }
    if (!scheduler_file_resolve_path(process,
                                     path,
                                     resolved,
                                     sizeof(resolved)) ||
        vfs_opendir(vfs, resolved, &directory) != 0) {
        return -1;
    }
    process_set_cwd(process, resolved);
    return 0;
}

int32_t i386_scheduler_getcwd(char *buffer, uint32_t size) {
    const char *cwd;
    uint32_t length = 0u;
    struct process *process = scheduler_file_current_process();

    if (process == 0 || buffer == 0 || size == 0u) {
        return -1;
    }
    cwd = process_cwd(process);
    while (cwd[length] != '\0') {
        length++;
    }
    if (length + 1u > size) {
        return -1;
    }
    scheduler_file_copy_text(buffer, size, cwd);
    return 0;
}

int32_t i386_scheduler_mkdir(struct vfs *vfs, const char *path) {
    struct process *process = scheduler_file_current_process();
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (vfs == 0 || path == 0 || process == 0) {
        return -1;
    }
    if (!scheduler_file_resolve_path(process,
                                     path,
                                     resolved,
                                     sizeof(resolved))) {
        return -1;
    }
    return vfs_mkdir(vfs, resolved) == 0 ? 0 : -1;
}

int32_t i386_scheduler_rmdir(struct vfs *vfs, const char *path) {
    struct process *process = scheduler_file_current_process();
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (vfs == 0 || path == 0 || process == 0) {
        return -1;
    }
    if (!scheduler_file_resolve_path(process,
                                     path,
                                     resolved,
                                     sizeof(resolved))) {
        return -1;
    }
    return vfs_rmdir(vfs, resolved) == 0 ? 0 : -1;
}

int32_t i386_scheduler_remove(struct vfs *vfs, const char *path) {
    struct process *process = scheduler_file_current_process();
    char resolved[NOS_PATH_BUFFER_SIZE];

    if (vfs == 0 || path == 0 || process == 0) {
        return -1;
    }
    if (!scheduler_file_resolve_path(process,
                                     path,
                                     resolved,
                                     sizeof(resolved))) {
        return -1;
    }
    return vfs_unlink(vfs, resolved) == 0 ? 0 : -1;
}

int32_t i386_scheduler_read(struct vfs *vfs,
                            uint32_t fd,
                            void *buffer,
                            uint32_t size,
                            uint32_t flags) {
    struct process *process = scheduler_file_current_process();
    struct file *file;
    int64_t result;

    if (process == 0 || buffer == 0 ||
        fd >= NOS_PROCESS_FILE_MAX || size == 0u ||
        (flags & ~(SYS_READ_NONBLOCK | SYS_READ_CHAR)) != 0u) {
        return -1;
    }
    file = &process->files[fd];
    if (file->kind == KERNEL_FILE_VFS &&
        (file->flags & KERNEL_FILE_ACCESS_READ) == 0u) {
        return -1;
    }
    result = file_read(file,
                       vfs,
                       buffer,
                       size,
                       (flags & SYS_READ_NONBLOCK
                            ? KERNEL_FILE_READ_NONBLOCK
                            : KERNEL_FILE_READ_BLOCKING) |
                           (flags & SYS_READ_CHAR
                                ? KERNEL_FILE_READ_CHAR
                                : 0u));
    if (result == KERNEL_FILE_IO_WOULD_BLOCK) {
        return -2;
    }
    return result < 0 || result > 0x7fffffffu ? -1 : (int32_t)result;
}

int32_t i386_scheduler_write(struct vfs *vfs,
                             uint32_t fd,
                             const void *buffer,
                             uint32_t size) {
    struct process *process = scheduler_file_current_process();
    struct file *file;
    int64_t result;

    if (process == 0 || buffer == 0 ||
        fd >= NOS_PROCESS_FILE_MAX || size == 0u) {
        return -1;
    }
    file = &process->files[fd];
    if (file->kind == KERNEL_FILE_TTY_STDOUT ||
        file->kind == KERNEL_FILE_TTY_STDERR) {
        if (i386_scheduler_quiet_tty_output() != 0u) {
            return (int32_t)size;
        }
    }
    if (file->kind == KERNEL_FILE_VFS &&
        (file->flags & KERNEL_FILE_ACCESS_WRITE) == 0u) {
        return -1;
    }
    result = file_write(file, vfs, buffer, size);
    if (result == KERNEL_FILE_IO_WOULD_BLOCK) {
        return -2;
    }
    return result < 0 || result > 0x7fffffffu ? -1 : (int32_t)result;
}

int32_t i386_scheduler_seek(uint32_t fd,
                            int32_t offset,
                            uint32_t whence) {
    struct process *process = scheduler_file_current_process();
    struct file *file;
    int64_t base;
    int64_t next;

    if (process == 0 || fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &process->files[fd];
    if (file->kind != KERNEL_FILE_VFS ||
        file->vfs_node.kind != VFS_NODE_FILE) {
        return -1;
    }
    if (whence == SYS_SEEK_SET) {
        base = 0;
    } else if (whence == SYS_SEEK_CUR) {
        base = file->offset;
    } else if (whence == SYS_SEEK_END) {
        base = scheduler_file_vfs_node_size(&file->vfs_node);
    } else {
        return -1;
    }
    next = base + offset;
    if (next < 0 || next > 0x7fffffffll) {
        return -1;
    }
    file_set_offset(file, (uint32_t)next);
    return (int32_t)next;
}

int32_t i386_scheduler_pipe(uint32_t pair[2]) {
    struct process *process = scheduler_file_current_process();

    if (process == 0 || pair == 0) {
        return -1;
    }
    return file_table_open_pipe_pair(process->files,
                                     NOS_PROCESS_FILE_MAX,
                                     3u,
                                     pair)
        ? 0
        : -1;
}

int32_t i386_scheduler_dup2(uint32_t src_fd, uint32_t dst_fd) {
    struct process *process = scheduler_file_current_process();

    if (process == 0 ||
        src_fd >= NOS_PROCESS_FILE_MAX ||
        dst_fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    if (process->files[src_fd].kind == KERNEL_FILE_NONE) {
        return -1;
    }
    if (src_fd == dst_fd) {
        return (int32_t)dst_fd;
    }
    if (!file_clone(&process->files[dst_fd], &process->files[src_fd])) {
        return -1;
    }
    process->files[dst_fd].flags &= (uint8_t)~KERNEL_FILE_CLOSE_ON_SPAWN;
    return (int32_t)dst_fd;
}

uint32_t i386_scheduler_fd_kind(uint32_t fd) {
    struct process *process = scheduler_file_current_process();

    if (process == 0 || fd >= NOS_PROCESS_FILE_MAX) {
        return KERNEL_FILE_NONE;
    }
    return process->files[fd].kind;
}

int32_t i386_scheduler_fd_query(uint32_t fd, struct syscall_fd_info *info) {
    struct process *process = scheduler_file_current_process();
    const struct file *file;

    if (process == 0 || info == 0 || fd >= NOS_PROCESS_FILE_MAX) {
        return 0;
    }
    file = &process->files[fd];
    for (uint32_t i = 0u; i < sizeof(*info); i++) {
        ((uint8_t *)info)[i] = 0u;
    }
    info->fd = fd;
    info->kind = file->kind;
    info->flags = file->flags;
    info->offset = file->offset;
    info->node_kind = file->vfs_node.kind;
    info->mount_kind = file->vfs_node.mount_kind;
    info->readable =
        file->kind == KERNEL_FILE_TTY_STDIN ||
        file->kind == KERNEL_FILE_PIPE_READ ||
        ((file->flags & KERNEL_FILE_ACCESS_READ) != 0u);
    info->writable =
        file->kind == KERNEL_FILE_TTY_STDOUT ||
        file->kind == KERNEL_FILE_TTY_STDERR ||
        file->kind == KERNEL_FILE_PIPE_WRITE ||
        ((file->flags & KERNEL_FILE_ACCESS_WRITE) != 0u);
    scheduler_file_copy_text(info->path,
                             sizeof(info->path),
                             file->opened_path);
    return file->kind != KERNEL_FILE_NONE ? 1 : 0;
}

int32_t i386_scheduler_close(uint32_t fd) {
    struct process *process = scheduler_file_current_process();
    struct file *file;

    if (process == 0 || fd >= NOS_PROCESS_FILE_MAX) {
        return -1;
    }
    file = &process->files[fd];
    if (file->kind == KERNEL_FILE_NONE) {
        return -1;
    }
    return (int32_t)file_close(file);
}

static const struct process_file_ops i386_process_file_ops = {
    i386_scheduler_open,
    i386_scheduler_opendir,
    i386_scheduler_readdir,
    i386_scheduler_chdir,
    i386_scheduler_getcwd,
    i386_scheduler_mkdir,
    i386_scheduler_rmdir,
    i386_scheduler_remove,
    i386_scheduler_read,
    i386_scheduler_write,
    i386_scheduler_seek,
    i386_scheduler_pipe,
    i386_scheduler_dup2,
    i386_scheduler_fd_kind,
    i386_scheduler_fd_query,
    i386_scheduler_close
};

void i386_scheduler_register_file_ops(void) {
    process_file_ops_register(&i386_process_file_ops);
}
