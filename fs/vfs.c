#include "fs/vfs_internal.h"
#include "lib/string.h"

static uint32_t vfs_copy_literal(char *dst, uint32_t pos, uint32_t dst_size, const char *text) {
    while (dst != 0 && text != 0 && *text != '\0' && pos + 1u < dst_size) {
        dst[pos++] = *text++;
    }
    if (dst != 0 && dst_size != 0 && pos < dst_size) {
        dst[pos] = '\0';
    }
    return pos;
}

static void vfs_set_mount_node(struct vfs_node *node,
                               uint8_t kind,
                               uint8_t mount_kind,
                               uint32_t mount_slot) {
    vfs_node_reset(node);
    node->kind = kind;
    node->mount_kind = mount_kind;
    node->mount_slot = mount_slot;
}

static void vfs_reset_mount_slot(struct vfs *vfs, uint32_t slot) {
    if (vfs == 0 || slot >= VFS_MOUNT_SLOT_MAX) {
        return;
    }
    vfs->mounts[slot].used = 0;
    vfs->mounts[slot].kind = VFS_MOUNT_NONE;
    vfs->mounts[slot].disk_index = 0;
    vfs->mounts[slot].part_index = 0;
    vfs->mounts[slot].name[0] = '\0';
    vfs->mounts[slot].fat32.mounted = 0;
    vfs->mounts[slot].nxfs.mounted = 0;
}

int vfs_has_root_override(const struct vfs *vfs) {
    return vfs != 0 && vfs->root_kind != VFS_MOUNT_NONE;
}

void vfs_copy_name(char *dst, uint32_t dst_size, const char *src) {
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

static uint32_t vfs_append_u32(char *dst, uint32_t pos, uint32_t dst_size, uint32_t value) {
    char digits_local[10];
    uint32_t count = 0;

    if (dst == 0 || dst_size == 0 || pos >= dst_size) {
        return pos;
    }
    do {
        digits_local[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && count < sizeof(digits_local));

    while (count > 0 && pos + 1u < dst_size) {
        dst[pos++] = digits_local[--count];
    }
    dst[pos] = '\0';
    return pos;
}

void vfs_format_disk_node_name(char *dst, uint32_t dst_size, uint32_t disk_index) {
    uint32_t pos = 0;

    if (dst == 0 || dst_size < 5u) {
        return;
    }
    pos = vfs_copy_literal(dst, pos, dst_size, "disk");
    vfs_append_u32(dst, pos, dst_size, disk_index);
}

void vfs_format_partition_node_name(char *dst,
                                    uint32_t dst_size,
                                    uint32_t disk_index,
                                    uint32_t part_index) {
    uint32_t pos = 0;

    if (dst == 0 || dst_size < 8u) {
        return;
    }
    pos = vfs_copy_literal(dst, pos, dst_size, "disk");
    pos = vfs_append_u32(dst, pos, dst_size, disk_index);
    if (pos + 2u >= dst_size) {
        return;
    }
    dst[pos++] = 'p';
    dst[pos] = '\0';
    vfs_append_u32(dst, pos, dst_size, part_index + 1u);
}

int vfs_find_dynamic_mount(const struct vfs *vfs, const char *name, uint32_t *slot_out) {
    if (vfs == 0 || name == 0 || slot_out == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (vfs->mounts[i].used && streq(vfs->mounts[i].name, name)) {
            *slot_out = i;
            return 1;
        }
    }
    return 0;
}

void vfs_set_dir_node(struct vfs_node *node, uint8_t mount_kind) {
    vfs_set_mount_node(node, VFS_NODE_DIR, mount_kind, 0);
}

void vfs_set_fat32_dir_node(struct vfs_node *node,
                            uint32_t mount_slot,
                            const struct fat32_file *dir) {
    vfs_set_mount_node(node, VFS_NODE_DIR, VFS_MOUNT_FAT32, mount_slot);
    if (dir != 0) {
        node->handle.fat32_file = *dir;
    }
}

void vfs_set_fat32_file_node(struct vfs_node *node,
                             uint32_t mount_slot,
                             const struct fat32_file *file) {
    vfs_set_mount_node(node, VFS_NODE_FILE, VFS_MOUNT_FAT32, mount_slot);
    if (file != 0) {
        node->handle.fat32_file = *file;
    }
}

void vfs_set_nxfs_file_node(struct vfs_node *node,
                            uint32_t mount_slot,
                            uint32_t inode_index,
                            const struct nxfs_inode *inode) {
    vfs_set_mount_node(node,
                       inode != 0 && inode->type == NXFS_TYPE_DIR ? VFS_NODE_DIR : VFS_NODE_FILE,
                       VFS_MOUNT_NXFS,
                       mount_slot);
    node->aux_index = inode_index;
    if (inode != 0) {
        node->handle.nxfs_inode = *inode;
    }
}

void vfs_set_devfs_node(struct vfs_node *node, uint8_t kind, uint32_t dev_id) {
    vfs_set_mount_node(node, kind, VFS_MOUNT_DEVFS, 0);
    node->aux_index = dev_id;
}

void vfs_set_procfs_node(struct vfs_node *node, uint8_t kind, uint32_t proc_id, uint32_t aux_data) {
    vfs_set_mount_node(node, kind, VFS_MOUNT_PROCFS, 0);
    node->aux_index = proc_id;
    node->aux_data = aux_data;
}

void vfs_set_eventfs_node(struct vfs_node *node, uint8_t kind, uint32_t event_id) {
    vfs_set_mount_node(node, kind, VFS_MOUNT_EVENTFS, 0);
    node->aux_index = event_id;
}

void vfs_set_node_device_numbers(struct vfs_node *node, uint32_t major, uint32_t minor) {
    if (node == 0) {
        return;
    }
    node->dev_major = major;
    node->dev_minor = minor;
}

uint32_t vfs_node_native_id(const struct vfs_node *node) {
    if (node == 0) {
        return 0;
    }
    if (node->mount_kind == VFS_MOUNT_FAT32) {
        return node->handle.fat32_file.first_cluster;
    }
    if (node->mount_kind == VFS_MOUNT_NXFS) {
        return node->aux_index;
    }
    return 0;
}

static int vfs_probe_fat32_source(struct block_device *dev, uint32_t partition_lba) {
    struct fat32_volume probe;

    return fat32_mount(&probe, dev, partition_lba);
}

static int vfs_probe_nxfs_source(struct block_device *dev, uint32_t partition_lba) {
    struct nxfs_volume probe;

    return nxfs_mount(&probe, dev, partition_lba);
}

static int vfs_mount_builtin_fat32(struct vfs *vfs, struct block_device *dev, uint32_t partition_lba) {
    if (vfs == 0) {
        return -1;
    }
    return fat32_mount(&vfs->fat32, dev, partition_lba);
}

static int vfs_mount_builtin_nxfs(struct vfs *vfs, struct block_device *dev, uint32_t partition_lba) {
    if (vfs == 0) {
        return -1;
    }
    return nxfs_mount(&vfs->nxfs, dev, partition_lba);
}

static int vfs_mount_dynamic_fat32(struct vfs *vfs,
                                   uint32_t slot,
                                   struct block_device *dev,
                                   uint32_t partition_lba) {
    if (vfs == 0 || slot >= VFS_MOUNT_SLOT_MAX) {
        return -1;
    }
    return fat32_mount(&vfs->mounts[slot].fat32, dev, partition_lba);
}

static int vfs_mount_dynamic_nxfs(struct vfs *vfs,
                                  uint32_t slot,
                                  struct block_device *dev,
                                  uint32_t partition_lba) {
    if (vfs == 0 || slot >= VFS_MOUNT_SLOT_MAX) {
        return -1;
    }
    return nxfs_mount(&vfs->mounts[slot].nxfs, dev, partition_lba);
}

static int vfs_builtin_fat32_mounted(const struct vfs *vfs) {
    return vfs != 0 && vfs->fat32.mounted;
}

static int vfs_builtin_nxfs_mounted(const struct vfs *vfs) {
    return vfs != 0 && vfs->nxfs.mounted;
}

static int vfs_fill_builtin_fat32_info(const struct vfs *vfs, struct vfs_builtin_mount_info *out) {
    if (!vfs_builtin_fat32_mounted(vfs) || out == 0) {
        return 0;
    }
    out->kind = VFS_MOUNT_FAT32;
    out->name = "fat";
    out->bdev = vfs->fat32.bdev;
    out->partition_lba = vfs->fat32.partition_lba;
    return 1;
}

static int vfs_fill_builtin_nxfs_info(const struct vfs *vfs, struct vfs_builtin_mount_info *out) {
    if (!vfs_builtin_nxfs_mounted(vfs) || out == 0) {
        return 0;
    }
    out->kind = VFS_MOUNT_NXFS;
    out->name = "nxfs";
    out->bdev = vfs->nxfs.bdev;
    out->partition_lba = vfs->nxfs.partition_lba;
    return 1;
}

static const struct vfs_mount_ops g_vfs_fat32_ops = {
    .open_file = vfs_open_fat32,
    .open_dir = vfs_opendir_fat32,
    .open_mount_root = vfs_opendir_mount_root,
    .mkdir_path = vfs_mkdir_fat32,
    .rmdir_path = vfs_rmdir_fat32,
    .unlink_path = vfs_unlink_fat32,
    .read_file = vfs_read_from_fat32,
    .write_file = vfs_write_to_fat32,
    .read_dir = vfs_read_dir_fat32,
    .prepare_opened_node = vfs_prepare_fat32_opened_node,
};

static const struct vfs_mount_ops g_vfs_nxfs_ops = {
    .open_file = vfs_open_nxfs,
    .open_dir = vfs_opendir_nxfs,
    .open_mount_root = vfs_opendir_mount_root,
    .mkdir_path = vfs_mkdir_nxfs,
    .rmdir_path = vfs_rmdir_nxfs,
    .unlink_path = vfs_unlink_nxfs,
    .read_file = vfs_read_from_nxfs,
    .write_file = vfs_write_to_nxfs,
    .read_dir = vfs_read_dir_nxfs,
    .prepare_opened_node = vfs_prepare_nxfs_opened_node,
};

static const struct vfs_builtin_mount_provider g_vfs_builtin_mount_providers[] = {
    {
        .kind = VFS_MOUNT_FAT32,
        .requires_partition = 0,
        .name = "fat",
        .root_target = "/fat",
        .probe_source = vfs_probe_fat32_source,
        .mount_builtin = vfs_mount_builtin_fat32,
        .mount_dynamic = vfs_mount_dynamic_fat32,
        .is_builtin_mounted = vfs_builtin_fat32_mounted,
        .fill_builtin_info = vfs_fill_builtin_fat32_info,
    },
    {
        .kind = VFS_MOUNT_NXFS,
        .requires_partition = 1,
        .name = "nxfs",
        .root_target = "/nxfs",
        .probe_source = vfs_probe_nxfs_source,
        .mount_builtin = vfs_mount_builtin_nxfs,
        .mount_dynamic = vfs_mount_dynamic_nxfs,
        .is_builtin_mounted = vfs_builtin_nxfs_mounted,
        .fill_builtin_info = vfs_fill_builtin_nxfs_info,
    },
};

uint32_t vfs_builtin_mount_provider_count(void) {
    return (uint32_t)(sizeof(g_vfs_builtin_mount_providers) / sizeof(g_vfs_builtin_mount_providers[0]));
}

const struct vfs_builtin_mount_provider *vfs_builtin_mount_provider_at(uint32_t index) {
    if (index >= vfs_builtin_mount_provider_count()) {
        return 0;
    }
    return &g_vfs_builtin_mount_providers[index];
}

const struct vfs_builtin_mount_provider *vfs_builtin_mount_provider(uint8_t kind) {
    for (uint32_t i = 0; i < vfs_builtin_mount_provider_count(); i++) {
        if (g_vfs_builtin_mount_providers[i].kind == kind) {
            return &g_vfs_builtin_mount_providers[i];
        }
    }
    return 0;
}

const struct vfs_mount_ops *vfs_mount_ops(uint8_t mount_kind) {
    if (mount_kind == VFS_MOUNT_FAT32) {
        return &g_vfs_fat32_ops;
    }
    if (mount_kind == VFS_MOUNT_NXFS) {
        return &g_vfs_nxfs_ops;
    }
    return 0;
}

void vfs_init(struct vfs *vfs) {
    if (vfs == 0) {
        return;
    }
    vfs->fat32.bdev = 0;
    vfs->fat32.partition_lba = 0;
    vfs->fat32.fat_start_lba = 0;
    vfs->fat32.data_start_lba = 0;
    vfs->fat32.sectors_per_cluster = 0;
    vfs->fat32.sectors_per_fat = 0;
    vfs->fat32.root_cluster = 0;
    vfs->fat32.total_sectors = 0;
    vfs->fat32.cluster_count = 0;
    vfs->fat32.table_count = 0;
    vfs->fat32.mounted = 0;
    vfs->nxfs.bdev = 0;
    vfs->nxfs.partition_lba = 0;
    vfs->nxfs.super.magic = 0;
    vfs->nxfs.super.total_blocks = 0;
    vfs->nxfs.super.bitmap_start = 0;
    vfs->nxfs.super.inode_start = 0;
    vfs->nxfs.super.data_start = 0;
    vfs->nxfs.mounted = 0;
    vfs->root_kind = VFS_MOUNT_NONE;
    vfs->root_slot = 0;
    for (uint32_t i = 0; i < VFS_MOUNT_SLOT_MAX; i++) {
        vfs_reset_mount_slot(vfs, i);
    }
}

int vfs_root_ready(const struct vfs *vfs) {
    struct vfs_builtin_mount_info builtin;

    if (vfs == 0) {
        return 0;
    }
    if (vfs->root_kind == VFS_MOUNT_NONE) {
        return vfs_get_builtin_mount_info(vfs, 0, &builtin);
    }
    if (vfs->root_kind == VFS_MOUNT_DEVFS ||
        vfs->root_kind == VFS_MOUNT_PROCFS ||
        vfs->root_kind == VFS_MOUNT_EVENTFS) {
        return 1;
    }
    return vfs_mount_ready(vfs, vfs->root_kind);
}

void vfs_node_reset(struct vfs_node *node) {
    if (node == 0) {
        return;
    }
    node->kind = VFS_NODE_NONE;
    node->mount_kind = VFS_MOUNT_NONE;
    node->mount_slot = 0;
    node->dev_major = 0;
    node->dev_minor = 0;
    node->aux_index = 0;
    node->aux_data = 0;
    node->handle.fat32_file.name[0] = '\0';
    node->handle.fat32_file.first_cluster = 0;
    node->handle.fat32_file.size = 0;
    node->handle.fat32_file.attributes = 0;
    node->handle.fat32_file.dirent_lba = 0;
    node->handle.fat32_file.dirent_offset = 0;
}
