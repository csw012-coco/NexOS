#include "block/blockdev.h"
#include "fs/fat32.h"
#include "fs/nxfs.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/fs/vfs_types.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "lib/string.h"

uint32_t syscall_compat32_chdir(struct syscall_compat32_context *ctx,
                                 uint32_t user_path) {
    char path[NOS_PATH_BUFFER_SIZE];

    if (ctx == 0 || ctx->chdir == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(path, user_path, sizeof(path))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->chdir(ctx->vfs, path);
}

uint32_t syscall_compat32_getcwd(struct syscall_compat32_context *ctx,
                                  uint32_t user_buffer,
                                  uint32_t size) {
    char path[NOS_PATH_BUFFER_SIZE];
    int32_t result;
    uint32_t length = 0u;

    if (ctx == 0 || ctx->getcwd == 0 ||
        size == 0u || size > sizeof(path)) {
        return (uint32_t)-1;
    }
    result = ctx->getcwd(path, size);
    if (result < 0) {
        return (uint32_t)result;
    }
    while (length < sizeof(path) && path[length] != '\0') {
        length++;
    }
    if (length == sizeof(path) || length + 1u > size) {
        return (uint32_t)-1;
    }
    return arch_copy_to_user(user_buffer, path, length + 1u)
        ? length
        : (uint32_t)-1;
}

uint32_t syscall_compat32_opendir(struct syscall_compat32_context *ctx,
                                   uint32_t user_path) {
    char path[NOS_PATH_BUFFER_SIZE];

    if (ctx == 0 || ctx->opendir == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(path, user_path, sizeof(path))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->opendir(ctx->vfs, path);
}

uint32_t syscall_compat32_readdir(struct syscall_compat32_context *ctx,
                                   uint32_t fd,
                                   uint32_t user_entry) {
    struct syscall_dirent entry;
    int32_t result;

    if (ctx == 0 || ctx->readdir == 0 || ctx->vfs == 0) {
        return (uint32_t)-1;
    }
    result = ctx->readdir(ctx->vfs, fd, &entry);
    if (result <= 0) {
        return (uint32_t)result;
    }
    return arch_copy_to_user(user_entry, &entry, sizeof(entry))
        ? (uint32_t)result
        : (uint32_t)-1;
}

uint32_t syscall_compat32_pipe(struct syscall_compat32_context *ctx,
                                uint32_t user_pair) {
    uint32_t pair[2];

    if (ctx == 0 || ctx->pipe == 0 || ctx->pipe(pair) != 0) {
        return (uint32_t)-1;
    }
    return arch_copy_to_user(user_pair, pair, sizeof(pair)) ? 0u : (uint32_t)-1;
}

uint32_t syscall_compat32_mkdir(struct syscall_compat32_context *ctx,
                                 uint32_t user_path) {
    char path[NOS_PATH_BUFFER_SIZE];

    if (ctx == 0 || ctx->mkdir == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(path, user_path, sizeof(path))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->mkdir(ctx->vfs, path);
}

uint32_t syscall_compat32_rmdir(struct syscall_compat32_context *ctx,
                                 uint32_t user_path) {
    char path[NOS_PATH_BUFFER_SIZE];

    if (ctx == 0 || ctx->rmdir == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(path, user_path, sizeof(path))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->rmdir(ctx->vfs, path);
}

uint32_t syscall_compat32_remove(struct syscall_compat32_context *ctx,
                                  uint32_t user_path) {
    char path[NOS_PATH_BUFFER_SIZE];

    if (ctx == 0 || ctx->remove == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(path, user_path, sizeof(path))) {
        return (uint32_t)-1;
    }
    return (uint32_t)ctx->remove(ctx->vfs, path);
}

static int compat32_is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static int compat32_parse_mount_source(const char *text,
                                        uint32_t *disk_index_out,
                                        uint32_t *part_index_out) {
    uint32_t disk_index = 0u;
    uint32_t part_number = 0u;

    if (text == 0 || disk_index_out == 0 || part_index_out == 0) {
        return 0;
    }
    if (starts_with(text, "/dev/disk")) {
        text += 9;
    } else if (starts_with(text, "disk")) {
        text += 4;
    } else {
        return 0;
    }
    if (!compat32_is_digit(*text)) {
        return 0;
    }
    while (compat32_is_digit(*text)) {
        disk_index = disk_index * 10u + (uint32_t)(*text - '0');
        text++;
    }
    if (*text != 'p') {
        return 0;
    }
    text++;
    if (!compat32_is_digit(*text)) {
        return 0;
    }
    while (compat32_is_digit(*text)) {
        part_number = part_number * 10u + (uint32_t)(*text - '0');
        text++;
    }
    if (*text != '\0' || part_number == 0u) {
        return 0;
    }
    *disk_index_out = disk_index;
    *part_index_out = part_number - 1u;
    return 1;
}

static int compat32_mount_target_name(const char *target,
                                       char *name,
                                       uint32_t name_size) {
    uint32_t i = 0u;

    if (target == 0 ||
        name == 0 ||
        name_size == 0u ||
        target[0] != '/' ||
        target[1] == '\0') {
        return 0;
    }
    target++;
    while (target[i] != '\0') {
        if (target[i] == '/' || i + 1u >= name_size) {
            return 0;
        }
        name[i] = target[i];
        i++;
    }
    name[i] = '\0';
    return 1;
}

static int compat32_find_free_mount_slot(struct vfs *vfs, uint32_t *slot_out) {
    if (vfs == 0 || slot_out == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (!vfs->mounts[i].used) {
            *slot_out = i;
            return 1;
        }
    }
    return 0;
}

static int compat32_mount_kind(uint32_t syscall_kind, uint8_t *kind_out) {
    if (kind_out == 0) {
        return 0;
    }
    if (syscall_kind == SYS_MOUNT_AUTO) {
        *kind_out = VFS_MOUNT_NONE;
        return 1;
    }
    if (syscall_kind == SYS_MOUNT_FAT32) {
        *kind_out = VFS_MOUNT_FAT32;
        return 1;
    }
    if (syscall_kind == SYS_MOUNT_NXFS) {
        *kind_out = VFS_MOUNT_NXFS;
        return 1;
    }
    return 0;
}

static uint32_t compat32_mount_error(uint32_t code) {
    return (uint32_t)(-(int32_t)code);
}

static int compat32_detect_mount_kind(struct block_device *disk,
                                       uint32_t partition_lba,
                                       uint8_t *kind_out) {
    struct nxfs_volume nxfs_probe;
    struct fat32_volume fat32_probe;

    if (disk == 0 || kind_out == 0) {
        return 0;
    }
    if (nxfs_mount(&nxfs_probe, disk, partition_lba) == 0) {
        *kind_out = VFS_MOUNT_NXFS;
        return 1;
    }
    if (fat32_mount(&fat32_probe, disk, partition_lba) == 0) {
        *kind_out = VFS_MOUNT_FAT32;
        return 1;
    }
    return 0;
}

uint32_t syscall_compat32_mount(struct syscall_compat32_context *ctx,
                                 uint32_t user_source,
                                 uint32_t user_target,
                                 uint32_t kind) {
    char source[NOS_PATH_BUFFER_SIZE];
    char target[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];
    struct block_device *disk;
    struct blockdev_partition partition;
    uint32_t disk_index;
    uint32_t part_index;
    uint32_t slot;
    uint8_t vfs_kind;

    if (ctx == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(source, user_source, sizeof(source)) ||
        !arch_copy_user_cstr(target, user_target, sizeof(target))) {
        return (uint32_t)-1;
    }
    if (!compat32_parse_mount_source(source, &disk_index, &part_index)) {
        return compat32_mount_error(SYS_MOUNT_ERR_INVALID_SOURCE);
    }
    if (!compat32_mount_kind(kind, &vfs_kind)) {
        return compat32_mount_error(SYS_MOUNT_ERR_UNSUPPORTED_KIND);
    }
    if (!compat32_mount_target_name(target, name, sizeof(name))) {
        return compat32_mount_error(SYS_MOUNT_ERR_INVALID_TARGET);
    }
    if (streq(name, "dev") ||
        streq(name, "proc") ||
        streq(name, "boot") ||
        streq(name, "fat") ||
        streq(name, "nxfs")) {
        return compat32_mount_error(SYS_MOUNT_ERR_RESERVED_TARGET);
    }
    if (vfs_find_dynamic_mount(ctx->vfs, name, &slot)) {
        return compat32_mount_error(SYS_MOUNT_ERR_TARGET_EXISTS);
    }
    if (!compat32_find_free_mount_slot(ctx->vfs, &slot)) {
        return compat32_mount_error(SYS_MOUNT_ERR_NO_SLOTS);
    }
    disk = blockdev_get(disk_index);
    if (disk == 0) {
        return compat32_mount_error(SYS_MOUNT_ERR_DISK_NOT_FOUND);
    }
    if (blockdev_partition_get(disk, part_index, &partition) != 0) {
        return compat32_mount_error(SYS_MOUNT_ERR_PARTITION_NOT_FOUND);
    }
    if (vfs_kind == VFS_MOUNT_NONE &&
        !compat32_detect_mount_kind(disk,
                                     (uint32_t)partition.start_lba,
                                     &vfs_kind)) {
        return compat32_mount_error(SYS_MOUNT_ERR_FS_DETECT);
    }
    if (vfs_kind == VFS_MOUNT_FAT32 &&
        fat32_mount(&ctx->vfs->mounts[slot].fat32,
                    disk,
                    (uint32_t)partition.start_lba) != 0) {
        return compat32_mount_error(SYS_MOUNT_ERR_FS_MOUNT);
    }
    if (vfs_kind == VFS_MOUNT_NXFS &&
        nxfs_mount(&ctx->vfs->mounts[slot].nxfs,
                   disk,
                   (uint32_t)partition.start_lba) != 0) {
        return compat32_mount_error(SYS_MOUNT_ERR_FS_MOUNT);
    }
    if (vfs_kind != VFS_MOUNT_FAT32 && vfs_kind != VFS_MOUNT_NXFS) {
        return compat32_mount_error(SYS_MOUNT_ERR_UNSUPPORTED_KIND);
    }
    ctx->vfs->mounts[slot].used = 1u;
    ctx->vfs->mounts[slot].kind = vfs_kind;
    ctx->vfs->mounts[slot].disk_index = disk_index;
    ctx->vfs->mounts[slot].part_index = part_index;
    vfs_copy_name(ctx->vfs->mounts[slot].name,
                  sizeof(ctx->vfs->mounts[slot].name),
                  name);
    return 0u;
}

uint32_t syscall_compat32_umount(struct syscall_compat32_context *ctx,
                                  uint32_t user_target) {
    char target[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t slot;
    struct block_device *disk;

    if (ctx == 0 || ctx->vfs == 0 ||
        !arch_copy_user_cstr(target, user_target, sizeof(target))) {
        return (uint32_t)-1;
    }
    if (!compat32_mount_target_name(target, name, sizeof(name))) {
        return compat32_mount_error(SYS_MOUNT_ERR_INVALID_TARGET);
    }
    if (!vfs_find_dynamic_mount(ctx->vfs, name, &slot)) {
        return compat32_mount_error(SYS_MOUNT_ERR_TARGET_NOT_FOUND);
    }
    if (ctx->vfs->root_kind == ctx->vfs->mounts[slot].kind &&
        ctx->vfs->root_slot == slot + 1u) {
        return compat32_mount_error(SYS_MOUNT_ERR_TARGET_BUSY);
    }
    disk = ctx->vfs->mounts[slot].kind == VFS_MOUNT_NXFS
        ? ctx->vfs->mounts[slot].nxfs.bdev
        : ctx->vfs->mounts[slot].fat32.bdev;
    if (blockdev_flush(disk) != 0) {
        return compat32_mount_error(SYS_MOUNT_ERR_TARGET_BUSY);
    }
    ctx->vfs->mounts[slot].fat32.mounted = 0;
    ctx->vfs->mounts[slot].nxfs.mounted = 0;
    ctx->vfs->mounts[slot].fat32.bdev = 0;
    ctx->vfs->mounts[slot].nxfs.bdev = 0;
    ctx->vfs->mounts[slot].fat32.partition_lba = 0u;
    ctx->vfs->mounts[slot].nxfs.partition_lba = 0u;
    ctx->vfs->mounts[slot].used = 0u;
    ctx->vfs->mounts[slot].kind = VFS_MOUNT_NONE;
    ctx->vfs->mounts[slot].disk_index = 0u;
    ctx->vfs->mounts[slot].part_index = 0u;
    ctx->vfs->mounts[slot].name[0] = '\0';
    return 0u;
}
