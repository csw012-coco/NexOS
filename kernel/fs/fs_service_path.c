#include "kernel/internal/fs/fs_service_path_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/fs/file_pipe_backend.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/process.h"
#include "lib/parse.h"
#include "lib/string.h"

static uint64_t fs_service_mount_error(uint32_t code) {
    return (uint64_t)(-(int64_t)code);
}

static uint32_t fs_service_nxfs_effective_mode(const struct vfs_node *node);

static uint64_t fs_service_access_denied_path(const char *path,
                                              const struct vfs_node *node,
                                              uint32_t required) {
    const struct process *proc = process_current();
    uint32_t mode = 0u;
    uint32_t owner_uid = 0u;
    uint32_t owner_gid = 0u;
    uint32_t mount_kind = 0u;

    if (node != 0) {
        mount_kind = node->mount_kind;
        if (node->mount_kind == VFS_MOUNT_NXFS) {
            mode = fs_service_nxfs_effective_mode(node);
            owner_uid = node->handle.nxfs_inode.uid;
            owner_gid = node->handle.nxfs_inode.gid;
        }
    }

    kprint("security: vfs access denied pid=%u uid=%u gid=%u caps=%x path=%s mount=%u mode=%u owner=%u:%u req=%u\n",
           proc != 0 ? proc->pid : 0u,
           process_uid(proc),
           process_gid(proc),
           process_capabilities(proc),
           path != 0 ? path : "(null)",
           mount_kind,
           mode,
           owner_uid,
           owner_gid,
           required);
    return (uint64_t)(int64_t)-NEX_ERR_ACCES;
}

static int fs_service_process_has_capability(struct process *proc,
                                             uint32_t cap) {
    return process_has_capability(proc, cap);
}

static uint32_t fs_service_map_file_access_flags(uint32_t syscall_flags);

enum {
    FS_SERVICE_NXFS_PERM_EXEC = 1u,
    FS_SERVICE_NXFS_PERM_WRITE = 2u,
    FS_SERVICE_NXFS_PERM_READ = 4u,
    FS_SERVICE_NXFS_FILE_DEFAULT_MODE = 0644u,
    FS_SERVICE_NXFS_DIR_DEFAULT_MODE = 0755u,
    FS_SERVICE_NXFS_MODE_PERM_MASK = 07777u,
    FS_SERVICE_NXFS_CAP_MODE_SHIFT = 16u,
    FS_SERVICE_NXFS_CAP_POLICY_PRESENT = 1u << 31
};

static uint32_t fs_service_nxfs_effective_mode(
    const struct vfs_node *node) {
    uint32_t mode;

    if (node == 0 || node->mount_kind != VFS_MOUNT_NXFS) {
        return 0u;
    }
    mode = node->handle.nxfs_inode.mode & 0777u;
    if (mode != 0u) {
        return mode;
    }
    return node->kind == VFS_NODE_DIR
        ? FS_SERVICE_NXFS_DIR_DEFAULT_MODE
        : FS_SERVICE_NXFS_FILE_DEFAULT_MODE;
}

static int fs_service_can_access_nxfs_node(struct process *proc,
                                           const struct vfs_node *node,
                                           uint32_t required) {
    uint32_t mode;
    uint32_t bits;

    if (node == 0 || node->mount_kind != VFS_MOUNT_NXFS || required == 0u) {
        return 1;
    }
    if (proc == 0) {
        return 0;
    }
    if (process_uid(proc) == 0u) {
        return 1;
    }
    mode = fs_service_nxfs_effective_mode(node);
    if (process_uid(proc) == node->handle.nxfs_inode.uid) {
        bits = (mode >> 6) & 7u;
    } else if (process_gid(proc) == node->handle.nxfs_inode.gid) {
        bits = (mode >> 3) & 7u;
    } else {
        bits = mode & 7u;
    }
    return (bits & required) == required;
}

static int fs_service_parent_path(const char *path, char *out, uint32_t size) {
    uint32_t last_slash = 0u;
    uint32_t i = 0u;
    uint32_t len;

    if (path == 0 || out == 0 || size < 2u) {
        return 0;
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            last_slash = i;
        }
        i++;
    }
    if (last_slash == 0u) {
        out[0] = '/';
        out[1] = '\0';
        return 1;
    }
    len = last_slash;
    if (len + 1u > size) {
        return 0;
    }
    for (i = 0u; i < len; i++) {
        out[i] = path[i];
    }
    out[len] = '\0';
    return 1;
}

static int fs_service_can_access_parent_dir(struct process *proc,
                                            struct vfs *vfs,
                                            const char *path,
                                            uint32_t required) {
    char parent_path[NOS_PATH_BUFFER_SIZE];
    struct vfs_node parent;

    if (required == 0u) {
        return 1;
    }
    if (!fs_service_parent_path(path, parent_path, sizeof(parent_path))) {
        return 0;
    }
    if (vfs_opendir(vfs, parent_path, &parent) != 0) {
        return 0;
    }
    return fs_service_can_access_nxfs_node(proc, &parent, required);
}

static uint32_t fs_service_open_required_nxfs_permissions(uint32_t flags) {
    uint32_t required = 0u;
    uint32_t access = fs_service_map_file_access_flags(flags);

    if ((access & KERNEL_FILE_ACCESS_READ) != 0u) {
        required |= FS_SERVICE_NXFS_PERM_READ;
    }
    if ((access & KERNEL_FILE_ACCESS_WRITE) != 0u ||
        (flags & (SYS_OPEN_CREAT | SYS_OPEN_TRUNC | SYS_OPEN_APPEND)) != 0u) {
        required |= FS_SERVICE_NXFS_PERM_WRITE;
    }
    return required;
}

static int fs_service_apply_created_nxfs_metadata(struct process *proc,
                                                  struct vfs *vfs,
                                                  struct vfs_node *node,
                                                  uint32_t mode) {
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (proc == 0 || vfs == 0 || node == 0 ||
        node->mount_kind != VFS_MOUNT_NXFS) {
        return 1;
    }
    if (!vfs_get_mount_instance(vfs,
                                VFS_MOUNT_NXFS,
                                node->mount_slot,
                                &mount)) {
        return 0;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    return nxfs_set_inode_metadata(nxfs,
                                   node->aux_index,
                                   &node->handle.nxfs_inode,
                                   (node->handle.nxfs_inode.mode &
                                    ~FS_SERVICE_NXFS_MODE_PERM_MASK) |
                                       (mode & FS_SERVICE_NXFS_MODE_PERM_MASK),
                                   process_uid(proc),
                                   process_gid(proc)) == 0;
}

static int fs_service_lookup_nxfs_node(struct vfs *vfs,
                                       const char *path,
                                       struct vfs_node *node) {
    if (vfs == 0 || path == 0 || node == 0) {
        return 0;
    }
    if (vfs_open(vfs, path, 0, node) == 0 &&
        node->mount_kind == VFS_MOUNT_NXFS) {
        return 1;
    }
    if (vfs_opendir(vfs, path, node) == 0 &&
        node->mount_kind == VFS_MOUNT_NXFS) {
        return 1;
    }
    return 0;
}

static int fs_service_is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static const char *fs_service_skip_token(const char *text) {
    while (text != 0 && *text != '\0' && *text != ' ' && *text != '\t' && *text != '\r' && *text != '\n') {
        text++;
    }
    return text;
}

static int fs_service_map_mount_kind(uint32_t syscall_kind, uint8_t *vfs_kind_out) {
    if (vfs_kind_out == 0) {
        return 0;
    }
    switch (syscall_kind) {
        case SYS_MOUNT_AUTO:
            *vfs_kind_out = VFS_MOUNT_NONE;
            return 1;
        case SYS_MOUNT_FAT32:
            *vfs_kind_out = VFS_MOUNT_FAT32;
            return 1;
        case SYS_MOUNT_NXFS:
            *vfs_kind_out = VFS_MOUNT_NXFS;
            return 1;
        default:
            return 0;
    }
}

static uint32_t fs_service_map_open_flags(uint32_t syscall_flags) {
    uint32_t flags = 0;

    if ((syscall_flags & SYS_OPEN_CREAT) != 0) {
        flags |= VFS_OPEN_CREATE;
    }
    if ((syscall_flags & SYS_OPEN_TRUNC) != 0) {
        flags |= VFS_OPEN_TRUNCATE;
    }
    if ((syscall_flags & SYS_OPEN_APPEND) != 0) {
        flags |= VFS_OPEN_APPEND;
    }
    return flags;
}

static uint32_t fs_service_map_file_access_flags(uint32_t syscall_flags) {
    uint32_t access = 0u;

    if ((syscall_flags & SYS_OPEN_READ) != 0u) {
        access |= KERNEL_FILE_ACCESS_READ;
    }
    if ((syscall_flags & SYS_OPEN_WRITE) != 0u) {
        access |= KERNEL_FILE_ACCESS_WRITE;
    }
    if (access == 0u) {
        access = ((syscall_flags &
                   (SYS_OPEN_CREAT | SYS_OPEN_TRUNC | SYS_OPEN_APPEND)) != 0u)
            ? KERNEL_FILE_ACCESS_WRITE
            : KERNEL_FILE_ACCESS_READ;
    }
    return access;
}

static uint32_t fs_service_devfs_required_capability(
    const struct vfs_node *node) {
    if (node == 0 || node->mount_kind != VFS_MOUNT_DEVFS) {
        return 0u;
    }
    switch (node->aux_index) {
        case VFS_DEV_BLOCK_DEVICE:
        case VFS_DEV_BLOCK_PARTITION:
            return PROCESS_CAP_RAW_BLOCK;
        case VFS_DEV_FRAMEBUFFER:
            return PROCESS_CAP_DISPLAY;
        case VFS_DEV_AUDIO:
        case VFS_DEV_SPEAKER:
            return PROCESS_CAP_AUDIO;
        default:
            return 0u;
    }
}

static uint32_t fs_service_eventfs_required_capability(
    const struct vfs_node *node) {
    if (node == 0 || node->mount_kind != VFS_MOUNT_EVENTFS) {
        return 0u;
    }
    switch (node->aux_index) {
        case VFS_EVENT_INPUT_DIR:
        case VFS_EVENT_INPUT_KEYBOARD:
        case VFS_EVENT_INPUT_KEYBOARD_JSON:
        case VFS_EVENT_INPUT_MOUSE:
        case VFS_EVENT_INPUT_MOUSE_JSON:
            return PROCESS_CAP_INPUT;
        case VFS_EVENT_NET_DIR:
        case VFS_EVENT_NET_STATUS:
        case VFS_EVENT_NET_STATUS_JSON:
            return PROCESS_CAP_NET_RAW;
        case VFS_EVENT_BLOCK_DIR:
        case VFS_EVENT_BLOCK_CHANGE:
        case VFS_EVENT_BLOCK_CHANGE_JSON:
            return PROCESS_CAP_RAW_BLOCK;
        case VFS_EVENT_SECURITY_DIR:
        case VFS_EVENT_SECURITY_CAPABILITY:
        case VFS_EVENT_SECURITY_CAPABILITY_JSON:
            return PROCESS_CAP_DEBUG;
        default:
            return 0u;
    }
}

static uint32_t fs_service_procfs_required_capability(
    const struct vfs_node *node) {
    if (node == 0 || node->mount_kind != VFS_MOUNT_PROCFS) {
        return 0u;
    }
    switch (node->aux_index) {
        case VFS_PROC_KMSG:
        case VFS_PROC_MEMINFO:
        case VFS_PROC_DRIVERS:
        case VFS_PROC_INTERRUPTS:
            return PROCESS_CAP_DEBUG;
        case VFS_PROC_BLOCK:
        case VFS_PROC_PARTITIONS:
            return PROCESS_CAP_RAW_BLOCK;
        case VFS_PROC_FB:
            return PROCESS_CAP_DISPLAY;
        default:
            return 0u;
    }
}

static uint32_t fs_service_open_required_capability(
    const struct vfs_node *node) {
    uint32_t cap;

    cap = fs_service_devfs_required_capability(node);
    if (cap != 0u) {
        return cap;
    }
    cap = fs_service_eventfs_required_capability(node);
    if (cap != 0u) {
        return cap;
    }
    return fs_service_procfs_required_capability(node);
}

static int fs_service_can_open_node(struct process *proc,
                                    const struct vfs_node *node) {
    uint32_t cap = fs_service_open_required_capability(node);

    return cap == 0u || fs_service_process_has_capability(proc, cap);
}

static void fs_service_set_file_access_flags(struct file *file,
                                             uint32_t syscall_flags) {
    if (file == 0) {
        return;
    }
    file->flags &= ~(KERNEL_FILE_ACCESS_READ | KERNEL_FILE_ACCESS_WRITE);
    file->flags |= fs_service_map_file_access_flags(syscall_flags);
}

static int fs_service_parse_disk_part_label(const char *text, uint32_t *disk_index_out, uint32_t *part_index_out) {
    uint32_t disk_index;
    uint32_t part_number;
    const char *rest;

    if (disk_index_out == 0 || part_index_out == 0) {
        return 0;
    }
    text = skip_spaces(text);
    if (starts_with(text, "/dev/disk")) {
        text += 9;
    } else if (starts_with(text, "disk")) {
        text += 4;
    } else {
        return 0;
    }
    if (!fs_service_is_digit(*text)) {
        return 0;
    }
    disk_index = 0;
    while (fs_service_is_digit(*text)) {
        disk_index = disk_index * 10u + (uint32_t)(*text - '0');
        text++;
    }
    rest = text;
    if (*skip_spaces(rest) == '\0') {
        *disk_index_out = disk_index;
        *part_index_out = VFS_PARTITION_RAW;
        return 1;
    }
    if (*rest != 'p') {
        return 0;
    }
    rest++;
    if (!fs_service_is_digit(*rest)) {
        return 0;
    }
    part_number = 0;
    while (fs_service_is_digit(*rest)) {
        part_number = part_number * 10u + (uint32_t)(*rest - '0');
        rest++;
    }
    if (part_number == 0) {
        return 0;
    }
    if (*skip_spaces(rest) != '\0') {
        return 0;
    }
    *disk_index_out = disk_index;
    *part_index_out = part_number - 1u;
    return 1;
}

static int fs_service_parse_disk_part_pair(const char *text, uint32_t *disk_index_out, uint32_t *part_index_out) {
    char disk_text[16];
    char part_text[16];
    uint32_t disk_index;
    uint32_t part_number;
    const char *rest;

    if (disk_index_out == 0 || part_index_out == 0 || !parse_token(text, disk_text, sizeof(disk_text))) {
        return 0;
    }
    rest = fs_service_skip_token(skip_spaces(text));
    rest = skip_spaces(rest);
    if (!parse_token(rest, part_text, sizeof(part_text))) {
        return 0;
    }
    rest = fs_service_skip_token(rest);
    if (*skip_spaces(rest) != '\0') {
        return 0;
    }
    if (!parse_u32(disk_text, &disk_index) || !parse_u32(part_text, &part_number) || part_number == 0) {
        return 0;
    }
    *disk_index_out = disk_index;
    *part_index_out = part_number - 1u;
    return 1;
}

static int fs_service_parse_block_target(const char *text, uint32_t *disk_index_out, uint32_t *part_index_out) {
    return fs_service_parse_disk_part_label(text, disk_index_out, part_index_out) ||
           fs_service_parse_disk_part_pair(text, disk_index_out, part_index_out);
}

static int fs_service_valid_path_request(struct vfs *vfs, const char *path) {
    return vfs != 0 && path != 0;
}

static void *fs_service_process_tty_handle(struct process *proc) {
    void *tty_handle;

    if (proc == 0) {
        return 0;
    }
    tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDIN]);
    if (tty_handle != 0) {
        return tty_handle;
    }
    tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDOUT]);
    if (tty_handle != 0) {
        return tty_handle;
    }
    tty_handle = file_tty_private_handle(&proc->files[SYS_FD_STDERR]);
    if (tty_handle != 0) {
        return tty_handle;
    }
    return proc->console_handle;
}

static int fs_service_stdio_aux_to_fd(uint32_t aux_index) {
    if (aux_index == VFS_DEV_STDIN) {
        return SYS_FD_STDIN;
    }
    if (aux_index == VFS_DEV_STDOUT) {
        return SYS_FD_STDOUT;
    }
    if (aux_index == VFS_DEV_STDERR) {
        return SYS_FD_STDERR;
    }
    return -1;
}

static uint64_t fs_service_open_stdio_alias(struct process *proc,
                                            uint32_t src_fd,
                                            uint32_t flags) {
    struct file *src;
    struct file *dst;
    uint32_t fd;

    if (proc == 0 || src_fd >= PROCESS_FILE_MAX) {
        return (uint64_t)-1;
    }
    src = file_table_active(proc->files, PROCESS_FILE_MAX, src_fd);
    if (src == 0) {
        return (uint64_t)-1;
    }
    dst = file_table_alloc(proc->files, PROCESS_FILE_MAX, 3u, &fd);
    if (dst == 0) {
        return (uint64_t)-1;
    }
    if (!file_clone(dst, src)) {
        file_discard(dst);
        return (uint64_t)-1;
    }
    fs_service_set_file_access_flags(dst, flags);
    return fd;
}

static uint64_t fs_service_open_node(struct process *proc,
                                     const struct vfs_node *node,
                                     const char *path,
                                     struct file **handle_out) {
    uint32_t fd;
    void *console_handle;

    if (proc == 0 || node == 0) {
        return (uint64_t)-1;
    }
    console_handle = fs_service_process_tty_handle(proc);
    if (!file_table_open_vfs(proc->files,
                             PROCESS_FILE_MAX,
                             3u,
                             node,
                             path,
                             console_handle,
                             &fd,
                             handle_out)) {
        return (uint64_t)-1;
    }
    return fd;
}

uint64_t fs_service_mkdir(struct process *proc, struct vfs *vfs, const char *path) {
    struct vfs_node node;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    if (!fs_service_can_access_parent_dir(proc,
                                          vfs,
                                          path,
                                          FS_SERVICE_NXFS_PERM_WRITE |
                                              FS_SERVICE_NXFS_PERM_EXEC)) {
        return fs_service_access_denied_path(path,
                                             0,
                                             FS_SERVICE_NXFS_PERM_WRITE |
                                                 FS_SERVICE_NXFS_PERM_EXEC);
    }
    if (vfs_mkdir(vfs, path) != 0) {
        return (uint64_t)-1;
    }
    if (vfs_opendir(vfs, path, &node) == 0) {
        (void)fs_service_apply_created_nxfs_metadata(
            proc, vfs, &node, FS_SERVICE_NXFS_DIR_DEFAULT_MODE);
    }
    return 0u;
}

uint64_t fs_service_rmdir(struct process *proc, struct vfs *vfs, const char *path) {
    struct vfs_node node;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    if (vfs_opendir(vfs, path, &node) != 0) {
        return (uint64_t)-1;
    }
    if (!fs_service_can_access_parent_dir(proc,
                                          vfs,
                                          path,
                                          FS_SERVICE_NXFS_PERM_WRITE |
                                              FS_SERVICE_NXFS_PERM_EXEC)) {
        return fs_service_access_denied_path(path,
                                             &node,
                                             FS_SERVICE_NXFS_PERM_WRITE |
                                                 FS_SERVICE_NXFS_PERM_EXEC);
    }
    return vfs_rmdir(vfs, path) == 0 ? 0u : (uint64_t)-1;
}

uint64_t fs_service_remove(struct process *proc, struct vfs *vfs, const char *path) {
    struct vfs_node node;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    if (file_pipe_backend_unlink_named(path)) {
        return 0;
    }
    if (vfs_open(vfs, path, 0, &node) != 0) {
        return (uint64_t)-1;
    }
    if (!fs_service_can_access_nxfs_node(proc,
                                         &node,
                                         FS_SERVICE_NXFS_PERM_WRITE) ||
        !fs_service_can_access_parent_dir(proc,
                                          vfs,
                                          path,
                                          FS_SERVICE_NXFS_PERM_WRITE |
                                              FS_SERVICE_NXFS_PERM_EXEC)) {
        return fs_service_access_denied_path(path,
                                             &node,
                                             FS_SERVICE_NXFS_PERM_WRITE |
                                                 FS_SERVICE_NXFS_PERM_EXEC);
    }
    return vfs_unlink(vfs, path) == 0 ? 0u : (uint64_t)-1;
}

uint64_t fs_service_mkfifo(struct vfs *vfs, const char *path) {
    struct vfs_node existing;

    if (!fs_service_valid_path_request(vfs, path) ||
        file_pipe_backend_named_exists(path) ||
        vfs_open(vfs, path, 0, &existing) == 0) {
        return (uint64_t)-1;
    }
    return file_pipe_backend_create_named(path) ? 0u : (uint64_t)-1;
}

uint64_t fs_service_chmod(struct process *proc,
                          struct vfs *vfs,
                          const char *path,
                          uint32_t mode) {
    struct vfs_node node;
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path) ||
        (mode & ~07777u) != 0u) {
        return (uint64_t)-1;
    }
    if (!fs_service_lookup_nxfs_node(vfs, path, &node)) {
        return (uint64_t)-1;
    }
    if (process_uid(proc) != 0u &&
        process_uid(proc) != node.handle.nxfs_inode.uid) {
        return fs_service_access_denied_path(path, &node, 0u);
    }
    if (!vfs_get_mount_instance(vfs,
                                VFS_MOUNT_NXFS,
                                node.mount_slot,
                                &mount)) {
        return (uint64_t)-1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    return nxfs_set_inode_metadata(nxfs,
                                   node.aux_index,
                                   &node.handle.nxfs_inode,
                                   (node.handle.nxfs_inode.mode &
                                    ~FS_SERVICE_NXFS_MODE_PERM_MASK) |
                                       (mode & FS_SERVICE_NXFS_MODE_PERM_MASK),
                                   node.handle.nxfs_inode.uid,
                                   node.handle.nxfs_inode.gid) == 0
        ? 0u
        : (uint64_t)-1;
}

uint64_t fs_service_chown(struct process *proc,
                          struct vfs *vfs,
                          const char *path,
                          uint32_t uid,
                          uint32_t gid) {
    struct vfs_node node;
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    if (process_uid(proc) != 0u) {
        return fs_service_access_denied_path(path, 0, 0u);
    }
    if (!fs_service_lookup_nxfs_node(vfs, path, &node)) {
        return (uint64_t)-1;
    }
    if (!vfs_get_mount_instance(vfs,
                                VFS_MOUNT_NXFS,
                                node.mount_slot,
                                &mount)) {
        return (uint64_t)-1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    return nxfs_set_inode_metadata(nxfs,
                                   node.aux_index,
                                   &node.handle.nxfs_inode,
                                   node.handle.nxfs_inode.mode,
                                   uid,
                                   gid) == 0 ? 0u : (uint64_t)-1;
}

uint64_t fs_service_setcap(struct process *proc,
                           struct vfs *vfs,
                           const char *path,
                           uint32_t caps) {
    struct vfs_node node;
    struct vfs_mount_instance mount;
    struct nxfs_volume *nxfs;
    uint32_t mode;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path) ||
        (caps & ~PROCESS_CAP_SYS_ADMIN) != 0u) {
        return (uint64_t)-1;
    }
    if (process_uid(proc) != 0u) {
        return fs_service_access_denied_path(path, 0, 0u);
    }
    if (!fs_service_lookup_nxfs_node(vfs, path, &node) ||
        node.kind != VFS_NODE_FILE) {
        return (uint64_t)-1;
    }
    if (!vfs_get_mount_instance(vfs,
                                VFS_MOUNT_NXFS,
                                node.mount_slot,
                                &mount)) {
        return (uint64_t)-1;
    }
    nxfs = (struct nxfs_volume *)mount.fs_data;
    mode = (node.handle.nxfs_inode.mode & FS_SERVICE_NXFS_MODE_PERM_MASK) |
           FS_SERVICE_NXFS_CAP_POLICY_PRESENT |
           ((caps & PROCESS_CAP_SYS_ADMIN) << FS_SERVICE_NXFS_CAP_MODE_SHIFT);
    return nxfs_set_inode_metadata(nxfs,
                                   node.aux_index,
                                   &node.handle.nxfs_inode,
                                   mode,
                                   node.handle.nxfs_inode.uid,
                                   node.handle.nxfs_inode.gid) == 0
        ? 0u
        : (uint64_t)-1;
}

uint64_t fs_service_mount(struct vfs *vfs, const char *source, const char *target, uint32_t syscall_kind) {
    uint8_t vfs_kind;
    uint32_t disk_index;
    uint32_t part_index;
    int mount_rc;

    if (vfs == 0 || source == 0 || target == 0 || !fs_service_map_mount_kind(syscall_kind, &vfs_kind)) {
        return fs_service_mount_error(SYS_MOUNT_ERR_BAD_ARGS);
    }
    if (!fs_service_parse_block_target(source, &disk_index, &part_index)) {
        return fs_service_mount_error(SYS_MOUNT_ERR_INVALID_SOURCE);
    }
    mount_rc = vfs_mount_fs(vfs, vfs_kind, disk_index, part_index, target);
    return mount_rc == 0 ? 0u : (uint64_t)(int64_t)mount_rc;
}

uint64_t fs_service_mount_boot(struct vfs *vfs,
                               const char *target,
                               uint32_t syscall_kind,
                               uint32_t partition_lba,
                               uint32_t partition_sectors) {
    uint8_t vfs_kind;
    uint32_t disk_index;
    int mount_rc;

    if (vfs == 0 || target == 0 || !fs_service_map_mount_kind(syscall_kind, &vfs_kind)) {
        return fs_service_mount_error(SYS_MOUNT_ERR_BAD_ARGS);
    }
    if (!vfs_find_disk_by_boot_partition(partition_lba, partition_sectors, &disk_index)) {
        return fs_service_mount_error(SYS_MOUNT_ERR_PARTITION_NOT_FOUND);
    }
    mount_rc = vfs_mount_fs_at_lba(vfs, vfs_kind, disk_index, partition_lba, partition_sectors, target);
    return mount_rc == 0 ? 0u : (uint64_t)(int64_t)mount_rc;
}

uint64_t fs_service_umount(struct vfs *vfs, const char *target) {
    int umount_rc;

    if (vfs == 0 || target == 0) {
        return fs_service_mount_error(SYS_MOUNT_ERR_BAD_ARGS);
    }
    umount_rc = vfs_umount(vfs, target);
    return umount_rc == 0 ? 0u : (uint64_t)(int64_t)umount_rc;
}

uint64_t fs_service_switch_root(struct vfs *vfs, const char *target) {
    uint32_t disk_index;
    uint32_t part_index;
    int rc;

    if (vfs == 0 || target == 0) {
        return fs_service_mount_error(SYS_MOUNT_ERR_BAD_ARGS);
    }
    if (streq(target, "/dev/root") || streq(target, "root")) {
        rc = vfs_switch_root_to_first_kind(vfs, VFS_MOUNT_NXFS);
        return rc == 0 ? 0u : (uint64_t)(int64_t)rc;
    }
    if (fs_service_parse_block_target(target, &disk_index, &part_index)) {
        rc = vfs_switch_root_to_source(vfs, disk_index, part_index);
        return rc == 0 ? 0u : (uint64_t)(int64_t)rc;
    }
    rc = vfs_set_root_mount(vfs, target);
    return rc == 0 ? 0u : fs_service_mount_error(SYS_MOUNT_ERR_TARGET_NOT_FOUND);
}

uint64_t fs_service_open(struct process *proc, struct vfs *vfs, const char *path, uint32_t flags) {
    struct file *opened_file = 0;
    struct vfs_node node;
    uint64_t fd;
    uint32_t initial_offset = 0;
    uint32_t vfs_flags;
    uint32_t nxfs_required;
    int created = 0;
    int stdio_fd;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    if (file_pipe_backend_named_exists(path)) {
        uint32_t access = flags & (SYS_OPEN_READ | SYS_OPEN_WRITE);
        uint32_t named_fd;

        if (access != SYS_OPEN_READ && access != SYS_OPEN_WRITE) {
            return (uint64_t)-1;
        }
        if (!file_table_open_named_pipe(proc->files,
                                        PROCESS_FILE_MAX,
                                        3u,
                                        path,
                                        access == SYS_OPEN_WRITE,
                                        &named_fd)) {
            return (uint64_t)-1;
        }
        return named_fd;
    }
    vfs_flags = fs_service_map_open_flags(flags);
    if (vfs_open(vfs, path, 0, &node) != 0) {
        if ((vfs_flags & VFS_OPEN_CREATE) == 0u) {
            return (uint64_t)-1;
        }
        if (!fs_service_can_access_parent_dir(proc,
                                              vfs,
                                              path,
                                              FS_SERVICE_NXFS_PERM_WRITE |
                                                  FS_SERVICE_NXFS_PERM_EXEC)) {
            return fs_service_access_denied_path(path,
                                                 0,
                                                 FS_SERVICE_NXFS_PERM_WRITE |
                                                     FS_SERVICE_NXFS_PERM_EXEC);
        }
        if (vfs_open(vfs, path, vfs_flags, &node) != 0) {
            return (uint64_t)-1;
        }
        created = 1;
    }
    if (node.kind != VFS_NODE_FILE) {
        return (uint64_t)-1;
    }
    if (!fs_service_can_open_node(proc, &node)) {
        return fs_service_access_denied_path(path, &node, 0u);
    }
    if (created &&
        !fs_service_apply_created_nxfs_metadata(
            proc, vfs, &node, FS_SERVICE_NXFS_FILE_DEFAULT_MODE)) {
        return (uint64_t)-1;
    }
    nxfs_required = fs_service_open_required_nxfs_permissions(flags);
    if (!fs_service_can_access_parent_dir(proc,
                                          vfs,
                                          path,
                                          FS_SERVICE_NXFS_PERM_EXEC) ||
        !fs_service_can_access_nxfs_node(proc, &node, nxfs_required)) {
        return fs_service_access_denied_path(path,
                                             &node,
                                             nxfs_required | FS_SERVICE_NXFS_PERM_EXEC);
    }
    if (vfs_prepare_opened_node(vfs, &node, path, vfs_flags, &initial_offset) != 0) {
        return (uint64_t)-1;
    }
    if (node.mount_kind == VFS_MOUNT_DEVFS) {
        stdio_fd = fs_service_stdio_aux_to_fd(node.aux_index);
        if (stdio_fd >= 0) {
            return fs_service_open_stdio_alias(proc, (uint32_t)stdio_fd, flags);
        }
    }
    fd = fs_service_open_node(proc, &node, path, &opened_file);
    if (fd == (uint64_t)-1) {
        return (uint64_t)-1;
    }
    fs_service_set_file_access_flags(opened_file, flags);
    if (initial_offset != 0u) {
        file_set_offset(opened_file, initial_offset);
    }
    return fd;
}

uint64_t fs_service_opendir(struct process *proc, struct vfs *vfs, const char *path) {
    struct vfs_node node;
    uint64_t fd;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    if (vfs_opendir(vfs, path, &node) != 0) {
        return (uint64_t)-1;
    }
    if (!fs_service_can_open_node(proc, &node)) {
        return fs_service_access_denied_path(path, &node, 0u);
    }
    if (!fs_service_can_access_parent_dir(proc,
                                          vfs,
                                          path,
                                          FS_SERVICE_NXFS_PERM_EXEC) ||
        !fs_service_can_access_nxfs_node(proc,
                                         &node,
                                         FS_SERVICE_NXFS_PERM_READ |
                                             FS_SERVICE_NXFS_PERM_EXEC)) {
        return fs_service_access_denied_path(path,
                                             &node,
                                             FS_SERVICE_NXFS_PERM_READ |
                                                 FS_SERVICE_NXFS_PERM_EXEC);
    }
    fd = fs_service_open_node(proc, &node, path, 0);
    if (fd != (uint64_t)-1) {
        proc->files[fd].flags |= KERNEL_FILE_ACCESS_READ;
    }
    return fd;
}
