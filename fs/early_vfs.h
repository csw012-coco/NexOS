#pragma once

#include <stdint.h>

#include "fs/fat32.h"

struct early_vfs {
    struct fat32_volume fat32;
    uint8_t mounted;
};

struct early_vfs_node {
    struct fat32_file file;
};

void early_vfs_init(struct early_vfs *vfs);
int early_vfs_mount_fat32(struct early_vfs *vfs,
                          uint32_t disk_index,
                          uint32_t partition_index);
int early_vfs_open(struct early_vfs *vfs,
                   const char *path,
                   struct early_vfs_node *node);
int early_vfs_read(struct early_vfs *vfs,
                   const struct early_vfs_node *node,
                   uint32_t offset,
                   void *buffer,
                   uint32_t size,
                   uint32_t *bytes_read);
uint32_t early_vfs_file_size(const struct early_vfs_node *node);
