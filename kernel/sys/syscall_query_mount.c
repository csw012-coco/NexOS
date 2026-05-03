#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/fs/fs_service_mount_query_internal.h"

static void syscall_mount_copy_name(char *dst, uint32_t dst_size, const char *src) {
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

static int syscall_prepare_user_output(uint64_t user_info_addr, uint32_t size) {
    if (!syscall_user_writable(user_info_addr, size)) {
        return 0;
    }
    return 1;
}

static uint64_t syscall_finish_user_output(uint64_t user_info_addr, const void *info, uint32_t size) {
    if (!syscall_copy_to_user(user_info_addr, info, size)) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}

static void syscall_init_mount_info(struct syscall_mount_info *info) {
    if (info == 0) {
        return;
    }
    info->kind = SYS_MOUNT_INFO_NONE;
    info->disk_index = 0;
    info->part_index = 0;
    info->source_known = 0;
    info->target[0] = '\0';
    info->space_known = 0;
    info->block_size = 0;
    info->total_blocks = 0;
    info->free_blocks = 0;
}

uint64_t syscall_handle_mount_query(uint32_t index, uint64_t user_info_addr) {
    struct syscall_mount_info info;
    struct vfs_mount_info mount;
    uint32_t offset = 1u;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    syscall_init_mount_info(&info);

    if (!fs_service_fill_builtin_mount_info(g_syscall_vfs, index, &info, &offset)) {
        if (index < offset || !fs_service_get_mount_info(g_syscall_vfs, index - offset, &mount)) {
            return 0;
        }
        info.kind = mount.kind == VFS_MOUNT_FAT32 ? SYS_MOUNT_INFO_FAT32 : SYS_MOUNT_INFO_NXFS;
        info.disk_index = mount.disk_index;
        info.part_index = mount.part_index;
        info.source_known = 1;
        syscall_mount_copy_name(info.target, sizeof(info.target), mount.name);
        fs_service_fill_dynamic_space(g_syscall_vfs, index - offset, &info);
    }
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}
