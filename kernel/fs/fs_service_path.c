#include "kernel/internal/fs/fs_service_path_internal.h"
#include "kernel/internal/fs/file_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "fs/vfs.h"
#include "lib/parse.h"
#include "lib/string.h"

static uint64_t fs_service_mount_error(uint32_t code) {
    return (uint64_t)(-(int64_t)code);
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

static uint64_t fs_service_open_node(struct process *proc,
                                     const struct vfs_node *node,
                                     const char *path,
                                     struct file **handle_out) {
    uint32_t fd;

    if (proc == 0 || node == 0) {
        return (uint64_t)-1;
    }
    if (!file_table_open_vfs(proc->files,
                             PROCESS_FILE_MAX,
                             3u,
                             node,
                             path,
                             proc->console_handle,
                             &fd,
                             handle_out)) {
        return (uint64_t)-1;
    }
    return fd;
}

uint64_t fs_service_mkdir(struct vfs *vfs, const char *path) {
    if (!fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    return vfs_mkdir(vfs, path) == 0 ? 0u : (uint64_t)-1;
}

uint64_t fs_service_rmdir(struct vfs *vfs, const char *path) {
    if (!fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    return vfs_rmdir(vfs, path) == 0 ? 0u : (uint64_t)-1;
}

uint64_t fs_service_remove(struct vfs *vfs, const char *path) {
    if (!fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    return vfs_unlink(vfs, path) == 0 ? 0u : (uint64_t)-1;
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

    if (vfs == 0 || target == 0) {
        return (uint64_t)-1;
    }
    if (fs_service_parse_block_target(target, &disk_index, &part_index)) {
        return vfs_switch_root_to_source(vfs, disk_index, part_index) == 0 ? 0u : (uint64_t)-1;
    }
    return vfs_set_root_mount(vfs, target) == 0 ? 0u : (uint64_t)-1;
}

uint64_t fs_service_open(struct process *proc, struct vfs *vfs, const char *path, uint32_t flags) {
    struct file *opened_file = 0;
    struct vfs_node node;
    uint64_t fd;
    uint32_t initial_offset = 0;

    if (proc == 0 || !fs_service_valid_path_request(vfs, path)) {
        return (uint64_t)-1;
    }
    if (vfs_open(vfs, path, flags, &node) != 0 || node.kind != VFS_NODE_FILE) {
        return (uint64_t)-1;
    }
    if (vfs_prepare_opened_node(vfs, &node, path, flags, &initial_offset) != 0) {
        return (uint64_t)-1;
    }
    fd = fs_service_open_node(proc, &node, path, &opened_file);
    if (fd == (uint64_t)-1) {
        return (uint64_t)-1;
    }
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
    fd = fs_service_open_node(proc, &node, path, 0);
    return fd;
}
