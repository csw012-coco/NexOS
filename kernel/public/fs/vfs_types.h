#pragma once

#include <stdint.h>
#include "kernel/public/fs/fat32_types.h"
#include "kernel/public/fs/nxfs_types.h"
#include "kernel/public/sys/system_limits.h"

enum vfs_mount_kind {
    VFS_MOUNT_NONE = 0,
    VFS_MOUNT_ROOT = 1,
    VFS_MOUNT_FAT32 = 2,
    VFS_MOUNT_NXFS = 3,
    VFS_MOUNT_DEVFS = 4,
    VFS_MOUNT_PROCFS = 5,
    VFS_MOUNT_EVENTFS = 6
};

enum vfs_node_kind {
    VFS_NODE_NONE = 0,
    VFS_NODE_DIR = 1,
    VFS_NODE_FILE = 2
};

struct vfs_node {
    uint8_t kind;
    uint8_t mount_kind;
    uint16_t reserved;
    uint32_t mount_slot;
    uint32_t dev_major;
    uint32_t dev_minor;
    uint32_t aux_index;
    uint32_t aux_data;
    union {
        struct fat32_file fat32_file;
        struct nxfs_inode nxfs_inode;
    } handle;
};

struct vfs_dirent {
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t size;
    uint8_t attributes;
};

struct vfs_mount_info {
    uint8_t kind;
    uint32_t disk_index;
    uint32_t part_index;
    char name[NOS_NAME_BUFFER_SIZE];
};
