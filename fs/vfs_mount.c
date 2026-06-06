#include "fs/vfs_internal.h"
#include "lib/string.h"

static int vfs_mount_fail(enum vfs_mount_error error) {
    return -(int)error;
}

static int vfs_lba_to_u32(uint64_t lba, uint32_t *out) {
    if (out == 0 || lba > 0xffffffffull) {
        return -1;
    }
    *out = (uint32_t)lba;
    return 0;
}

static int vfs_mount_root_from_source(struct vfs *vfs,
                                      uint8_t mount_kind,
                                      struct block_device *dev,
                                      uint32_t partition_lba) {
    const struct vfs_builtin_mount_provider *provider;

    if (vfs == 0) {
        return -1;
    }
    provider = vfs_builtin_mount_provider(mount_kind);
    if (provider == 0 || provider->mount_builtin == 0 || provider->root_target == 0) {
        return -1;
    }
    if (provider->mount_builtin(vfs, dev, partition_lba) != 0) {
        return -1;
    }
    return vfs_set_root_mount(vfs, provider->root_target);
}

static int vfs_resolve_mount_source(uint32_t disk_index,
                                    uint32_t part_index,
                                    struct block_device **dev_out,
                                    uint32_t *partition_lba_out) {
    struct block_device *dev;
    struct blockdev_partition part;

    if (dev_out == 0 || partition_lba_out == 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_BAD_ARGS);
    }
    *dev_out = 0;
    *partition_lba_out = 0;
    dev = blockdev_get(disk_index);
    if (dev == 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_DISK_NOT_FOUND);
    }
    if (part_index != VFS_PARTITION_RAW) {
        if (blockdev_partition_get(dev, part_index, &part) != 0) {
            return vfs_mount_fail(VFS_MOUNT_ERR_PARTITION_NOT_FOUND);
        }
        if (vfs_lba_to_u32(part.start_lba, partition_lba_out) != 0) {
            return vfs_mount_fail(VFS_MOUNT_ERR_PARTITION_NOT_FOUND);
        }
    }
    *dev_out = dev;
    return 0;
}

static int vfs_detect_mount_kind_from_source(struct block_device *dev,
                                             uint32_t partition_lba,
                                             uint8_t *kind_out) {
    if (kind_out == 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_BAD_ARGS);
    }
    *kind_out = VFS_MOUNT_NONE;
    if (dev == 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_DISK_NOT_FOUND);
    }
    {
        static const uint8_t probe_order[] = {VFS_MOUNT_NXFS, VFS_MOUNT_FAT32};

        for (uint32_t i = 0; i < (uint32_t)(sizeof(probe_order) / sizeof(probe_order[0])); i++) {
            const struct vfs_builtin_mount_provider *provider = vfs_builtin_mount_provider(probe_order[i]);

            if (provider != 0 && provider->probe_source != 0 &&
                provider->probe_source(dev, partition_lba) == 0) {
                *kind_out = provider->kind;
                return 0;
            }
        }
    }
    return vfs_mount_fail(VFS_MOUNT_ERR_FS_DETECT);
}

static int vfs_source_has_root_program(uint8_t kind, struct block_device *dev, uint32_t partition_lba) {
    if (kind == VFS_MOUNT_NXFS) {
        struct nxfs_volume probe;
        struct nxfs_inode inode;
        uint32_t inode_index;

        if (nxfs_mount(&probe, dev, partition_lba) != 0) {
            return 0;
        }
        return nxfs_lookup_path(&probe, "/cmd/ush", &inode_index, &inode) == 0 &&
               inode.type == NXFS_TYPE_FILE;
    }
    return 1;
}

static int vfs_has_mounted_kind(const struct vfs *vfs, uint8_t mount_kind) {
    if (vfs == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (vfs->mounts[i].used && vfs->mounts[i].kind == mount_kind) {
            return 1;
        }
    }
    return 0;
}

static int vfs_dynamic_mount_slot_valid(const struct vfs *vfs, uint32_t mount_slot, uint8_t kind) {
    return vfs != 0 && mount_slot != 0 && mount_slot - 1u < VFS_MOUNT_SLOT_MAX &&
           vfs->mounts[mount_slot - 1u].used && vfs->mounts[mount_slot - 1u].kind == kind;
}

static int vfs_find_free_mount_slot(const struct vfs *vfs, uint32_t *slot_out) {
    if (vfs == 0 || slot_out == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (!vfs->mounts[i].used) {
            *slot_out = i;
            return 1;
        }
    }
    return 0;
}

static void vfs_store_mount_entry(struct vfs *vfs,
                                  uint32_t slot,
                                  uint8_t kind,
                                  uint32_t disk_index,
                                  uint32_t part_index,
                                  const char *name) {
    vfs->mounts[slot].used = 1;
    vfs->mounts[slot].kind = kind;
    vfs->mounts[slot].disk_index = disk_index;
    vfs->mounts[slot].part_index = part_index;
    vfs_copy_name(vfs->mounts[slot].name, sizeof(vfs->mounts[slot].name), name);
}

int vfs_mount_ready(const struct vfs *vfs, uint8_t mount_kind) {
    const struct vfs_builtin_mount_provider *provider;

    if (vfs == 0) {
        return 0;
    }
    if (mount_kind == VFS_MOUNT_DEVFS ||
        mount_kind == VFS_MOUNT_PROCFS ||
        mount_kind == VFS_MOUNT_EVENTFS) {
        return 1;
    }
    provider = vfs_builtin_mount_provider(mount_kind);
    if (provider != 0 && provider->is_builtin_mounted != 0 && provider->is_builtin_mounted(vfs)) {
        return 1;
    }
    if (provider != 0) {
        return vfs_has_mounted_kind(vfs, mount_kind);
    }
    return 1;
}

int vfs_get_builtin_mount_info(const struct vfs *vfs, uint32_t index, struct vfs_builtin_mount_info *out) {
    uint32_t offset = 0;

    if (vfs == 0 || out == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < vfs_builtin_mount_provider_count(); i++) {
        const struct vfs_builtin_mount_provider *provider = vfs_builtin_mount_provider_at(i);

        if (provider == 0 || provider->is_builtin_mounted == 0 || provider->fill_builtin_info == 0 ||
            !provider->is_builtin_mounted(vfs)) {
            continue;
        }
        if (index == offset) {
            return provider->fill_builtin_info(vfs, out);
        }
        offset++;
    }
    return 0;
}

static int vfs_partition_matches(struct block_device *dev,
                                 uint32_t part_slot,
                                 uint32_t partition_lba,
                                 uint32_t *part_index_out) {
    struct blockdev_partition part;

    if (dev == 0 || part_index_out == 0 || blockdev_partition_get(dev, part_slot, &part) != 0) {
        return 0;
    }
    if (part.start_lba != partition_lba) {
        return 0;
    }
    *part_index_out = part.index;
    return 1;
}

static int vfs_find_cached_partition_by_lba(struct block_device *dev,
                                            uint32_t partition_lba,
                                            uint32_t partition_sectors,
                                            uint32_t *part_index_out) {
    struct blockdev_partition part;

    if (dev == 0 || part_index_out == 0) {
        return 0;
    }
    for (uint32_t part_slot = 0; part_slot < blockdev_partition_count(dev); part_slot++) {
        if (blockdev_partition_get(dev, part_slot, &part) != 0 || part.start_lba != partition_lba) {
            continue;
        }
        if (partition_sectors != 0u && part.sector_count != partition_sectors) {
            continue;
        }
        *part_index_out = part.index;
        return 1;
    }
    return 0;
}

static int vfs_find_block_source(struct block_device *dev,
                                 uint32_t partition_lba,
                                 uint32_t *disk_index_out,
                                 uint32_t *part_index_out) {
    if (dev == 0 || disk_index_out == 0 || part_index_out == 0) {
        return 0;
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *current = blockdev_get(disk_index);

        if (current != dev) {
            continue;
        }
        for (uint32_t part_slot = 0; part_slot < blockdev_partition_count(current); part_slot++) {
            if (vfs_partition_matches(current, part_slot, partition_lba, part_index_out)) {
                *disk_index_out = disk_index;
                return 1;
            }
        }
        if (partition_lba == 0u) {
            uint8_t kind;

            if (vfs_detect_mount_kind_from_source(current, 0u, &kind) == 0) {
                *disk_index_out = disk_index;
                *part_index_out = VFS_PARTITION_RAW;
                return 1;
            }
        }
    }
    return 0;
}

static int vfs_find_raw_mount_source(uint32_t partition_sectors,
                                     uint32_t *disk_index_out,
                                     uint32_t *part_index_out) {
    uint8_t kind;

    if (disk_index_out == 0 || part_index_out == 0) {
        return 0;
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);

        if (dev == 0 || blockdev_partition_count(dev) != 0) {
            continue;
        }
        if (partition_sectors != 0u && dev->block_count != (uint64_t)partition_sectors) {
            continue;
        }
        if (vfs_detect_mount_kind_from_source(dev, 0u, &kind) != 0) {
            continue;
        }
        *disk_index_out = disk_index;
        *part_index_out = VFS_PARTITION_RAW;
        return 1;
    }
    return 0;
}

int vfs_find_disk_by_boot_partition(uint32_t partition_lba,
                                    uint32_t partition_sectors,
                                    uint32_t *disk_index_out) {
    struct blockdev_partition part;
    uint8_t kind;

    if (disk_index_out == 0) {
        return 0;
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);

        if (dev == 0) {
            continue;
        }
        for (uint32_t part_slot = 0; part_slot < blockdev_partition_count(dev); part_slot++) {
            if (blockdev_partition_get(dev, part_slot, &part) != 0 ||
                part.start_lba != partition_lba ||
                (partition_sectors != 0u && part.sector_count != partition_sectors)) {
                continue;
            }
            if (vfs_detect_mount_kind_from_source(dev, part.start_lba, &kind) != 0) {
                continue;
            }
            *disk_index_out = disk_index;
            return 1;
        }
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);

        if (dev == 0 || (uint64_t)partition_lba >= dev->block_count) {
            continue;
        }
        if (partition_sectors != 0u &&
            (uint64_t)partition_sectors > dev->block_count - (uint64_t)partition_lba) {
            continue;
        }
        if (vfs_detect_mount_kind_from_source(dev, partition_lba, &kind) != 0) {
            continue;
        }
        *disk_index_out = disk_index;
        return 1;
    }
    return 0;
}

int vfs_find_source_by_partition_lba(uint32_t partition_lba,
                                     uint32_t *disk_index_out,
                                     uint32_t *part_index_out) {
    struct blockdev_partition part;
    uint8_t kind;

    if (disk_index_out == 0 || part_index_out == 0) {
        return 0;
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);

        if (dev == 0) {
            continue;
        }
        for (uint32_t part_slot = 0; part_slot < blockdev_partition_count(dev); part_slot++) {
            if (blockdev_partition_get(dev, part_slot, &part) != 0 || part.start_lba != partition_lba) {
                continue;
            }
            if (vfs_detect_mount_kind_from_source(dev, part.start_lba, &kind) != 0) {
                continue;
            }
            *disk_index_out = disk_index;
            *part_index_out = part.index;
            return 1;
        }
    }
    return 0;
}

int vfs_find_source_by_boot_partition(uint32_t partition_lba,
                                      uint32_t partition_sectors,
                                      uint32_t *disk_index_out,
                                      uint32_t *part_index_out) {
    struct blockdev_partition part;
    uint8_t kind;

    if (disk_index_out == 0 || part_index_out == 0) {
        return 0;
    }
    if (partition_sectors == 0u) {
        return vfs_find_source_by_partition_lba(partition_lba, disk_index_out, part_index_out) ||
               (partition_lba == 0u && vfs_find_raw_mount_source(0u, disk_index_out, part_index_out));
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);

        if (dev == 0) {
            continue;
        }
        for (uint32_t part_slot = 0; part_slot < blockdev_partition_count(dev); part_slot++) {
            if (blockdev_partition_get(dev, part_slot, &part) != 0 ||
                part.start_lba != partition_lba ||
                part.sector_count != partition_sectors) {
                continue;
            }
            if (vfs_detect_mount_kind_from_source(dev, part.start_lba, &kind) != 0) {
                continue;
            }
            *disk_index_out = disk_index;
            *part_index_out = part.index;
            return 1;
        }
    }
    return vfs_find_source_by_partition_lba(partition_lba, disk_index_out, part_index_out) ||
           (partition_lba == 0u &&
            (vfs_find_raw_mount_source(partition_sectors, disk_index_out, part_index_out) ||
             vfs_find_raw_mount_source(0u, disk_index_out, part_index_out)));
}

int vfs_get_mount_instance(struct vfs *vfs,
                           uint8_t mount_kind,
                           uint32_t mount_slot,
                           struct vfs_mount_instance *out) {
    struct block_device *dev;

    if (vfs == 0 || out == 0) {
        return 0;
    }
    out->kind = mount_kind;
    out->mount_slot = mount_slot;
    out->fs_data = 0;
    out->bdev = 0;
    out->partition_lba = 0;

    if (mount_kind == VFS_MOUNT_FAT32) {
        if (mount_slot == 0) {
            if (!vfs->fat32.mounted) {
                return 0;
            }
            out->fs_data = &vfs->fat32;
            out->bdev = vfs->fat32.bdev;
            out->partition_lba = vfs->fat32.partition_lba;
            return 1;
        }
        if (!vfs_dynamic_mount_slot_valid(vfs, mount_slot, VFS_MOUNT_FAT32)) {
            return 0;
        }
        dev = blockdev_get(vfs->mounts[mount_slot - 1u].disk_index);
        if (dev == 0) {
            return 0;
        }
        /* The volume already caches its partition LBA from mount time.  Keep
           normal file I/O off the disk's MBR sector; some USB MSC devices
           transiently reject that read after root switch/unit attention. */
        vfs->mounts[mount_slot - 1u].fat32.bdev = dev;
        out->fs_data = &vfs->mounts[mount_slot - 1u].fat32;
        out->bdev = vfs->mounts[mount_slot - 1u].fat32.bdev;
        out->partition_lba = vfs->mounts[mount_slot - 1u].fat32.partition_lba;
        return 1;
    }
    if (mount_kind == VFS_MOUNT_NXFS) {
        if (mount_slot == 0) {
            if (!vfs->nxfs.mounted) {
                return 0;
            }
            out->fs_data = &vfs->nxfs;
            out->bdev = vfs->nxfs.bdev;
            out->partition_lba = vfs->nxfs.partition_lba;
            return 1;
        }
        if (!vfs_dynamic_mount_slot_valid(vfs, mount_slot, VFS_MOUNT_NXFS)) {
            return 0;
        }
        dev = blockdev_get(vfs->mounts[mount_slot - 1u].disk_index);
        if (dev == 0) {
            return 0;
        }
        vfs->mounts[mount_slot - 1u].nxfs.bdev = dev;
        out->fs_data = &vfs->mounts[mount_slot - 1u].nxfs;
        out->bdev = vfs->mounts[mount_slot - 1u].nxfs.bdev;
        out->partition_lba = vfs->mounts[mount_slot - 1u].nxfs.partition_lba;
        return 1;
    }
    return 0;
}

int vfs_get_root_mount_instance(struct vfs *vfs, struct vfs_mount_instance *out) {
    uint8_t root_kind;
    uint32_t root_slot;

    if (vfs == 0 || out == 0) {
        return 0;
    }
    root_kind = vfs->root_kind == VFS_MOUNT_NONE ? VFS_MOUNT_FAT32 : vfs->root_kind;
    root_slot = vfs->root_kind == VFS_MOUNT_NONE ? 0u : vfs->root_slot;
    return vfs_get_mount_instance(vfs, root_kind, root_slot, out);
}

uint32_t vfs_builtin_mount_count(const struct vfs *vfs) {
    uint32_t count = 3u;
    struct vfs_builtin_mount_info builtin;

    if (vfs == 0) {
        return 3u;
    }
    while (vfs_get_builtin_mount_info(vfs, count - 3u, &builtin)) {
        count++;
    }
    return count;
}

int vfs_get_builtin_mount(const struct vfs *vfs,
                          uint32_t index,
                          struct vfs_mount_info *out,
                          uint32_t *source_known_out) {
    struct vfs_builtin_mount_info builtin;

    if (out == 0 || source_known_out == 0) {
        return 0;
    }

    out->kind = VFS_MOUNT_NONE;
    out->disk_index = 0;
    out->part_index = 0;
    out->name[0] = '\0';
    *source_known_out = 0;

    if (index == 0u) {
        out->kind = VFS_MOUNT_DEVFS;
        vfs_copy_name(out->name, sizeof(out->name), "dev");
        return 1;
    }
    if (index == 1u) {
        out->kind = VFS_MOUNT_PROCFS;
        vfs_copy_name(out->name, sizeof(out->name), "proc");
        return 1;
    }
    if (index == 2u) {
        out->kind = VFS_MOUNT_EVENTFS;
        vfs_copy_name(out->name, sizeof(out->name), "event");
        return 1;
    }
    if (vfs == 0 || !vfs_get_builtin_mount_info(vfs, index - 3u, &builtin)) {
        return 0;
    }

    out->kind = builtin.kind;
    vfs_copy_name(out->name, sizeof(out->name), builtin.name);
    *source_known_out =
        vfs_find_block_source(builtin.bdev, builtin.partition_lba, &out->disk_index, &out->part_index);
    return 1;
}

int vfs_detect_mount_kind(uint32_t disk_index, uint32_t part_index, uint8_t *kind_out) {
    struct block_device *dev;
    uint32_t partition_lba;

    if (vfs_resolve_mount_source(disk_index, part_index, &dev, &partition_lba) != 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_FS_DETECT);
    }
    return vfs_detect_mount_kind_from_source(dev, partition_lba, kind_out);
}

int vfs_set_root_mount(struct vfs *vfs, const char *target) {
    struct vfs_path parsed;
    const struct vfs_builtin_mount_provider *provider;

    if (vfs == 0 || target == 0) {
        return -1;
    }
    if (streq(target, "/")) {
        vfs->root_kind = VFS_MOUNT_NONE;
        vfs->root_slot = 0;
        return 0;
    }
    if (!vfs_parse_path_for_vfs(vfs, target, &parsed) || parsed.root_dir || !parsed.child_is_root) {
        return -1;
    }
    provider = vfs_builtin_mount_provider(parsed.mount_kind);
    if (provider == 0) {
        return -1;
    }
    vfs->root_kind = provider->kind;
    vfs->root_slot = parsed.mount_slot;
    return 0;
}

static int vfs_prepare_dynamic_mount_target(struct vfs *vfs,
                                            const char *target,
                                            char *name,
                                            uint32_t name_size,
                                            uint32_t *slot_out) {
    if (vfs == 0 || target == 0 || target[0] != '/') {
        return vfs_mount_fail(VFS_MOUNT_ERR_INVALID_TARGET);
    }
    if (name == 0 || slot_out == 0 || !vfs_resolve_mount_target_name(vfs, target, name, name_size)) {
        return vfs_mount_fail(VFS_MOUNT_ERR_INVALID_TARGET);
    }
    if (streq(name, "dev") || streq(name, "proc") || streq(name, "fat") || streq(name, "nxfs")) {
        return vfs_mount_fail(VFS_MOUNT_ERR_RESERVED_TARGET);
    }
    if (vfs_find_dynamic_mount(vfs, name, slot_out)) {
        return vfs_mount_fail(VFS_MOUNT_ERR_TARGET_EXISTS);
    }
    if (!vfs_find_free_mount_slot(vfs, slot_out)) {
        return vfs_mount_fail(VFS_MOUNT_ERR_NO_SLOTS);
    }
    return 0;
}

int vfs_mount_fs(struct vfs *vfs, uint8_t kind, uint32_t disk_index, uint32_t part_index, const char *target) {
    struct block_device *dev;
    uint32_t partition_lba = 0;
    uint32_t slot = 0;
    char name[NOS_NAME_BUFFER_SIZE];
    const struct vfs_builtin_mount_provider *provider;
    int target_rc;

    target_rc = vfs_prepare_dynamic_mount_target(vfs, target, name, sizeof(name), &slot);
    if (target_rc != 0) {
        return target_rc;
    }
    {
        int source_rc = vfs_resolve_mount_source(disk_index, part_index, &dev, &partition_lba);

        if (source_rc != 0) {
            return source_rc;
        }
    }
    if (kind == VFS_MOUNT_NONE) {
        if (vfs_detect_mount_kind_from_source(dev, partition_lba, &kind) != 0) {
            return vfs_mount_fail(VFS_MOUNT_ERR_FS_DETECT);
        }
    }
    provider = vfs_builtin_mount_provider(kind);
    if (provider == 0 || provider->mount_dynamic == 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_UNSUPPORTED_KIND);
    }
    if (provider->requires_partition && part_index == VFS_PARTITION_RAW) {
        return vfs_mount_fail(VFS_MOUNT_ERR_PARTITION_REQUIRED);
    }
    if (provider->mount_dynamic(vfs, slot, dev, partition_lba) != 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_FS_MOUNT);
    }
    vfs_store_mount_entry(vfs, slot, kind, disk_index, part_index, name);
    return 0;
}

int vfs_mount_fs_at_lba(struct vfs *vfs,
                        uint8_t kind,
                        uint32_t disk_index,
                        uint32_t partition_lba,
                        uint32_t partition_sectors,
                        const char *target) {
    struct block_device *dev;
    uint32_t slot = 0;
    uint32_t stored_part_index = VFS_PARTITION_RAW;
    char name[NOS_NAME_BUFFER_SIZE];
    const struct vfs_builtin_mount_provider *provider;
    int target_rc;

    target_rc = vfs_prepare_dynamic_mount_target(vfs, target, name, sizeof(name), &slot);
    if (target_rc != 0) {
        return target_rc;
    }
    dev = blockdev_get(disk_index);
    if (dev == 0 || (uint64_t)partition_lba >= dev->block_count) {
        return vfs_mount_fail(VFS_MOUNT_ERR_DISK_NOT_FOUND);
    }
    (void)vfs_find_cached_partition_by_lba(dev, partition_lba, partition_sectors, &stored_part_index);
    if (kind == VFS_MOUNT_NONE) {
        if (vfs_detect_mount_kind_from_source(dev, partition_lba, &kind) != 0) {
            return vfs_mount_fail(VFS_MOUNT_ERR_FS_DETECT);
        }
    }
    provider = vfs_builtin_mount_provider(kind);
    if (provider == 0 || provider->mount_dynamic == 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_UNSUPPORTED_KIND);
    }
    if (provider->mount_dynamic(vfs, slot, dev, partition_lba) != 0) {
        return vfs_mount_fail(VFS_MOUNT_ERR_FS_MOUNT);
    }
    vfs_store_mount_entry(vfs, slot, kind, disk_index, stored_part_index, name);
    return 0;
}

int vfs_umount(struct vfs *vfs, const char *target) {
    uint32_t slot;
    char name[NOS_NAME_BUFFER_SIZE];

    if (vfs == 0 || target == 0 || target[0] != '/') {
        return vfs_mount_fail(VFS_MOUNT_ERR_INVALID_TARGET);
    }
    if (!vfs_resolve_mount_target_name(vfs, target, name, sizeof(name))) {
        return vfs_mount_fail(VFS_MOUNT_ERR_INVALID_TARGET);
    }
    if (!vfs_find_dynamic_mount(vfs, name, &slot)) {
        return vfs_mount_fail(VFS_MOUNT_ERR_TARGET_NOT_FOUND);
    }
    if (vfs->root_kind == vfs->mounts[slot].kind && vfs->root_slot == slot + 1u) {
        return vfs_mount_fail(VFS_MOUNT_ERR_TARGET_BUSY);
    }
    vfs->mounts[slot].fat32.mounted = 0;
    vfs->mounts[slot].nxfs.mounted = 0;
    vfs->mounts[slot].fat32.bdev = 0;
    vfs->mounts[slot].nxfs.bdev = 0;
    vfs->mounts[slot].nxfs.partition_lba = 0;
    vfs->mounts[slot].fat32.partition_lba = 0;
    vfs->mounts[slot].used = 0;
    vfs->mounts[slot].kind = VFS_MOUNT_NONE;
    vfs->mounts[slot].disk_index = 0;
    vfs->mounts[slot].part_index = 0;
    vfs->mounts[slot].name[0] = '\0';
    return 0;
}

int vfs_switch_root_to_source(struct vfs *vfs, uint32_t disk_index, uint32_t part_index) {
    struct block_device *dev;
    uint32_t partition_lba = 0;
    uint8_t kind = VFS_MOUNT_NONE;
    int source_rc;

    source_rc = vfs_resolve_mount_source(disk_index, part_index, &dev, &partition_lba);
    if (source_rc != 0) {
        return -1;
    }
    if (vfs_detect_mount_kind_from_source(dev, partition_lba, &kind) != 0) {
        return -1;
    }
    return vfs_mount_root_from_source(vfs, kind, dev, partition_lba);
}

int vfs_switch_root_to_first_kind(struct vfs *vfs, uint8_t kind) {
    if (vfs == 0 || kind == VFS_MOUNT_NONE) {
        return -1;
    }
    for (uint32_t disk_index = 0; disk_index < blockdev_count(); disk_index++) {
        struct block_device *dev = blockdev_get(disk_index);
        uint32_t part_count;

        if (dev == 0) {
            continue;
        }
        part_count = blockdev_partition_count(dev);
        for (uint32_t part_slot = 0; part_slot < part_count; part_slot++) {
            struct blockdev_partition part;
            uint32_t partition_lba;
            uint8_t detected_kind;

            if (blockdev_partition_get(dev, part_slot, &part) != 0 ||
                vfs_lba_to_u32(part.start_lba, &partition_lba) != 0 ||
                vfs_detect_mount_kind_from_source(dev, partition_lba, &detected_kind) != 0 ||
                detected_kind != kind ||
                !vfs_source_has_root_program(detected_kind, dev, partition_lba)) {
                continue;
            }
            return vfs_mount_root_from_source(vfs, detected_kind, dev, partition_lba);
        }
        if (part_count == 0u) {
            uint8_t detected_kind;

            if (vfs_detect_mount_kind_from_source(dev, 0u, &detected_kind) == 0 &&
                detected_kind == kind &&
                vfs_source_has_root_program(detected_kind, dev, 0u)) {
                return vfs_mount_root_from_source(vfs, detected_kind, dev, 0u);
            }
        }
    }
    return -1;
}

uint32_t vfs_mount_count(const struct vfs *vfs) {
    uint32_t count = 0;

    if (vfs == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (vfs->mounts[i].used) {
            count++;
        }
    }
    return count;
}

int vfs_get_mount(const struct vfs *vfs, uint32_t index, struct vfs_mount_info *out) {
    uint32_t found = 0;

    if (vfs == 0 || out == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < VFS_MOUNT_SLOT_MAX; i++) {
        if (!vfs->mounts[i].used) {
            continue;
        }
        if (found == index) {
            out->kind = vfs->mounts[i].kind;
            out->disk_index = vfs->mounts[i].disk_index;
            out->part_index = vfs->mounts[i].part_index;
            vfs_copy_name(out->name, sizeof(out->name), vfs->mounts[i].name);
            return 0;
        }
        found++;
    }
    return -1;
}
