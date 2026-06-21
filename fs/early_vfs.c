#include "block/blockdev.h"
#include "fs/early_vfs.h"
#include "lib/string.h"

void early_vfs_init(struct early_vfs *vfs) {
    if (vfs == 0) {
        return;
    }
    memset(vfs, 0, sizeof(*vfs));
}

int early_vfs_mount_fat32(struct early_vfs *vfs,
                          uint32_t disk_index,
                          uint32_t partition_index) {
    struct block_device *device;
    struct blockdev_partition partition;

    if (vfs == 0) {
        return -1;
    }
    device = blockdev_get(disk_index);
    if (device == 0 ||
        blockdev_partition_get(device, partition_index, &partition) != 0 ||
        partition.start_lba > 0xffffffffull) {
        return -1;
    }
    if (fat32_mount(&vfs->fat32, device, (uint32_t)partition.start_lba) != 0) {
        return -1;
    }
    vfs->mounted = 1u;
    return 0;
}

int early_vfs_open(struct early_vfs *vfs,
                   const char *path,
                   struct early_vfs_node *node) {
    if (vfs == 0 || !vfs->mounted || path == 0 || node == 0) {
        return -1;
    }
    if (fat32_find_path(&vfs->fat32, path, &node->file) != 0 ||
        (node->file.attributes & 0x10u) != 0u) {
        return -1;
    }
    return 0;
}

int early_vfs_read(struct early_vfs *vfs,
                   const struct early_vfs_node *node,
                   uint32_t offset,
                   void *buffer,
                   uint32_t size,
                   uint32_t *bytes_read) {
    if (vfs == 0 || !vfs->mounted || node == 0) {
        return -1;
    }
    return fat32_read_file_range(&vfs->fat32,
                                 &node->file,
                                 offset,
                                 buffer,
                                 size,
                                 bytes_read);
}

uint32_t early_vfs_file_size(const struct early_vfs_node *node) {
    return node != 0 ? node->file.size : 0u;
}
