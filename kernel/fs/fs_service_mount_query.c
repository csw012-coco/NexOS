#include "kernel/internal/fs/fs_service_mount_query_internal.h"
static void fs_service_copy_name(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int fs_service_get_mount_info(struct vfs *vfs, uint32_t index, struct vfs_mount_info *info_out) {
    if (vfs == 0 || info_out == 0) {
        return 0;
    }
    return vfs_get_mount(vfs, index, info_out) == 0;
}

int fs_service_fill_builtin_mount_info(struct vfs *vfs,
                                       uint32_t index,
                                       struct syscall_mount_info *info,
                                       uint32_t *offset_out) {
    struct vfs_mount_info builtin;
    uint32_t source_known = 0;

    if (info == 0 || offset_out == 0) {
        return 0;
    }
    if (!vfs_get_builtin_mount(vfs, index, &builtin, &source_known)) {
        *offset_out = vfs_builtin_mount_count(vfs);
        return 0;
    }
    if (builtin.kind == VFS_MOUNT_DEVFS) {
        info->kind = SYS_MOUNT_INFO_DEVFS;
    } else if (builtin.kind == VFS_MOUNT_PROCFS) {
        info->kind = SYS_MOUNT_INFO_PROCFS;
    } else if (builtin.kind == VFS_MOUNT_EVENTFS) {
        info->kind = SYS_MOUNT_INFO_EVENTFS;
    } else {
        info->kind = builtin.kind == VFS_MOUNT_FAT32 ? SYS_MOUNT_INFO_FAT32 : SYS_MOUNT_INFO_NXFS;
    }
    fs_service_copy_name(info->target, sizeof(info->target), builtin.name);
    info->disk_index = builtin.disk_index;
    info->part_index = builtin.part_index;
    info->source_known = source_known;
    return 1;
}
