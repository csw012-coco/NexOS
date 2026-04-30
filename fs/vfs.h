#pragma once

#include <stdint.h>
#include "kernel/public/fs/vfs_types.h"
#include "kernel/public/sys/system_limits.h"

enum vfs_mount_error {
    VFS_MOUNT_ERR_BAD_ARGS = 1,
    VFS_MOUNT_ERR_INVALID_TARGET = 3,
    VFS_MOUNT_ERR_RESERVED_TARGET = 4,
    VFS_MOUNT_ERR_TARGET_EXISTS = 5,
    VFS_MOUNT_ERR_NO_SLOTS = 6,
    VFS_MOUNT_ERR_DISK_NOT_FOUND = 7,
    VFS_MOUNT_ERR_PARTITION_NOT_FOUND = 8,
    VFS_MOUNT_ERR_FS_DETECT = 9,
    VFS_MOUNT_ERR_UNSUPPORTED_KIND = 10,
    VFS_MOUNT_ERR_FS_MOUNT = 11,
    VFS_MOUNT_ERR_PARTITION_REQUIRED = 12,
    VFS_MOUNT_ERR_TARGET_BUSY = 13,
    VFS_MOUNT_ERR_TARGET_NOT_FOUND = 14
};

enum {
    VFS_MOUNT_SLOT_MAX = NOS_MOUNT_SLOT_MAX,
    VFS_PARTITION_RAW = 0xffffffffu
};

struct vfs;

void vfs_init(struct vfs *vfs);
int vfs_set_root_mount(struct vfs *vfs, const char *target);
int vfs_root_ready(const struct vfs *vfs);
void vfs_node_reset(struct vfs_node *node);
int vfs_detect_mount_kind(uint32_t disk_index, uint32_t part_index, uint8_t *kind_out);
int vfs_find_source_by_partition_lba(uint32_t partition_lba,
                                     uint32_t *disk_index_out,
                                     uint32_t *part_index_out);
int vfs_find_source_by_boot_partition(uint32_t partition_lba,
                                      uint32_t partition_sectors,
                                      uint32_t *disk_index_out,
                                      uint32_t *part_index_out);
int vfs_find_disk_by_boot_partition(uint32_t partition_lba,
                                    uint32_t partition_sectors,
                                    uint32_t *disk_index_out);
int vfs_mount_fs(struct vfs *vfs, uint8_t kind, uint32_t disk_index, uint32_t part_index, const char *target);
int vfs_mount_fs_at_lba(struct vfs *vfs,
                        uint8_t kind,
                        uint32_t disk_index,
                        uint32_t partition_lba,
                        uint32_t partition_sectors,
                        const char *target);
int vfs_umount(struct vfs *vfs, const char *target);
int vfs_switch_root_to_source(struct vfs *vfs, uint32_t disk_index, uint32_t part_index);
int vfs_prepare_opened_node(struct vfs *vfs,
                            struct vfs_node *node,
                            const char *path,
                            uint32_t flags,
                            uint32_t *offset_out);
int vfs_read_file_all(struct vfs *vfs,
                      const char *path,
                      struct vfs_node *node_out,
                      void *buffer,
                      uint32_t buffer_size,
                      uint32_t *bytes_read_out);
uint32_t vfs_mount_count(const struct vfs *vfs);
uint32_t vfs_builtin_mount_count(const struct vfs *vfs);
int vfs_get_builtin_mount(const struct vfs *vfs,
                          uint32_t index,
                          struct vfs_mount_info *out,
                          uint32_t *source_known_out);
int vfs_get_mount(const struct vfs *vfs, uint32_t index, struct vfs_mount_info *out);
int vfs_opendir_root_mount(struct vfs *vfs, struct vfs_node *out);
int vfs_open(struct vfs *vfs, const char *path, uint32_t flags, struct vfs_node *out);
int vfs_opendir(struct vfs *vfs, const char *path, struct vfs_node *out);
int vfs_mkdir(struct vfs *vfs, const char *path);
int vfs_rmdir(struct vfs *vfs, const char *path);
int vfs_unlink(struct vfs *vfs, const char *path);
int64_t vfs_read(struct vfs *vfs,
                 struct vfs_node *node,
                 uint32_t *offset_io,
                 void *buffer,
                 uint32_t size,
                 uint32_t flags);
int64_t vfs_write(struct vfs *vfs,
                  struct vfs_node *node,
                  uint32_t *offset_io,
                  const void *buffer,
                  uint32_t size,
                  const char *opened_path);
int64_t vfs_readdir(struct vfs *vfs,
                    struct vfs_node *node,
                    uint32_t *index_io,
                    struct vfs_dirent *entry);
uint32_t vfs_node_file_size(const struct vfs_node *node);
uint32_t vfs_node_native_id(const struct vfs_node *node);
