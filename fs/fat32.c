#include "fs/fat32_internal.h"

int fat32_mount(struct fat32_volume *vol, struct block_device *bdev, uint32_t partition_lba) {
    struct fat32_bpb *bpb;
    uint32_t total_sectors;
    uint64_t fat_region_sectors;
    uint32_t data_sectors;
    uint32_t cluster_count;

    if (vol == 0) {
        return -1;
    }
    mem_set(vol, 0, sizeof(*vol));
    if (bdev == 0 || bdev->block_size != 512 || bdev->block_count == 0u ||
        (uint64_t)partition_lba >= bdev->block_count) {
        return -1;
    }
    if (blockdev_read(bdev, partition_lba, 1, vol->sector_buffer) != 0) {
        fat32_log_probe_failure(partition_lba, "read", 0, 0);
        return -1;
    }

    bpb = (struct fat32_bpb *)vol->sector_buffer;
    if (vol->sector_buffer[510] != 0x55u || vol->sector_buffer[511] != 0xaau) {
        fat32_log_probe_failure(partition_lba, "signature", bpb, vol->sector_buffer);
        return -1;
    }
    if (bpb->jump[0] != 0xebu && bpb->jump[0] != 0xe9u) {
        fat32_log_probe_failure(partition_lba, "jump", bpb, vol->sector_buffer);
        return -1;
    }
    if (bpb->bytes_per_sector != 512 ||
        !fat32_is_power_of_two(bpb->sectors_per_cluster) ||
        bpb->sectors_per_cluster > 128u ||
        bpb->reserved_sector_count == 0u ||
        bpb->table_count == 0u ||
        bpb->table_count > 2u ||
        bpb->root_entry_count != 0u ||
        bpb->table_size_16 != 0u ||
        bpb->table_size_32 == 0u) {
        fat32_log_probe_failure(partition_lba, "bpb", bpb, vol->sector_buffer);
        return -1;
    }

    total_sectors = bpb->total_sectors_16 != 0 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    fat_region_sectors = (uint64_t)bpb->reserved_sector_count +
                         (uint64_t)bpb->table_count * (uint64_t)bpb->table_size_32;
    if (total_sectors == 0u ||
        total_sectors > bdev->block_count - (uint64_t)partition_lba ||
        fat_region_sectors >= (uint64_t)total_sectors ||
        (uint64_t)partition_lba + (uint64_t)total_sectors > 0x100000000ull) {
        fat32_log_probe_failure(partition_lba, "geometry", bpb, vol->sector_buffer);
        return -1;
    }
    data_sectors = total_sectors - (uint32_t)fat_region_sectors;
    cluster_count = data_sectors / bpb->sectors_per_cluster;
    if (cluster_count == 0u ||
        (uint64_t)bpb->table_size_32 * 128u < (uint64_t)cluster_count + 2u) {
        fat32_log_probe_failure(partition_lba, "clusters", bpb, vol->sector_buffer);
        return -1;
    }
    vol->cluster_count = cluster_count;
    if (!fat32_cluster_is_data(vol, bpb->root_cluster)) {
        fat32_log_probe_failure(partition_lba, "root_cluster", bpb, vol->sector_buffer);
        mem_set(vol, 0, sizeof(*vol));
        return -1;
    }

    vol->bdev = bdev;
    vol->partition_lba = partition_lba;
    vol->fat_start_lba = partition_lba + bpb->reserved_sector_count;
    vol->data_start_lba = partition_lba + (uint32_t)fat_region_sectors;
    vol->sectors_per_cluster = bpb->sectors_per_cluster;
    vol->sectors_per_fat = bpb->table_size_32;
    vol->root_cluster = bpb->root_cluster;
    vol->total_sectors = total_sectors;
    vol->table_count = bpb->table_count;
    vol->mounted = 1;
    return 0;
}

void fat32_get_root_dir(struct fat32_volume *vol, struct fat32_file *out) {
    if (out == 0) {
        return;
    }
    fat32_file_reset(out);
    out->name[0] = '/';
    out->name[1] = '\0';
    out->first_cluster = vol != 0 ? vol->root_cluster : 0;
    out->size = 0;
    out->attributes = FAT32_ATTR_DIRECTORY;
}

int fat32_list_root(struct fat32_volume *vol, struct fat32_file *entries, uint32_t max_entries, uint32_t *entry_count) {
    return fat32_list_directory(vol, 0, entries, max_entries, entry_count);
}

int fat32_find_root(struct fat32_volume *vol, const char *name, struct fat32_file *out) {
    return fat32_find_in_directory(vol, 0, name, out);
}


int fat32_create_root(struct fat32_volume *vol, const char *name, struct fat32_file *out) {
    if (vol == 0 || !vol->mounted || name == 0 || out == 0) {
        return -1;
    }
    return fat32_create_entry_in_directory(vol, 0, name, 0, 0u, out);
}

int fat32_create_path(struct fat32_volume *vol, const char *path, struct fat32_file *out) {
    struct fat32_file parent_dir;
    struct fat32_file existing;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];

    if (vol == 0 || !vol->mounted || path == 0 || out == 0) {
        return -1;
    }
    if (fat32_split_parent_child_path(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return -1;
    }
    if (name[0] == '\0' ||
        (name[0] == '.' && name[1] == '\0') ||
        (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        return -1;
    }
    if (parent_path[0] == '\0') {
        fat32_get_root_dir(vol, &parent_dir);
    } else if (fat32_find_path(vol, parent_path, &parent_dir) != 0 ||
               (parent_dir.attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }
    if (fat32_find_in_directory(vol, &parent_dir, name, &existing) == 0) {
        return -1;
    }
    if (fat32_create_entry_in_directory(vol, &parent_dir, name, 0, 0u, &existing) != 0) {
        return -1;
    }
    *out = existing;
    return 0;
}

int fat32_get_root_entry(struct fat32_volume *vol, uint32_t index, struct fat32_file *out) {
    return fat32_get_dir_entry(vol, 0, index, out);
}

int fat32_get_dir_entry(struct fat32_volume *vol, const struct fat32_file *dir, uint32_t index, struct fat32_file *out) {
    uint32_t cluster;
    uint32_t current = 0;
    uint32_t raw_index = 0;
    struct fat32_lfn_state lfn_state;
    uint8_t dir_sector[512];

    if (vol == 0 || !vol->mounted || out == 0) {
        return -1;
    }
    if (dir != 0 && (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    fat32_lfn_state_reset(&lfn_state);
    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);
        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            struct fat32_dirent *dirents;

            if (fat32_read_sector(vol, cluster_lba + sec, dir_sector) != 0) {
                return -1;
            }
            dirents = (struct fat32_dirent *)dir_sector;
            for (uint32_t i = 0; i < 16; i++) {
                struct fat32_file entry;

                if (fat32_is_end_of_dirent(&dirents[i])) {
                    return -1;
                }
                if (fat32_is_deleted_dirent(&dirents[i])) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (fat32_is_lfn_dirent(&dirents[i])) {
                    (void)fat32_lfn_capture(&lfn_state, (const struct fat32_lfn_dirent *)&dirents[i]);
                    raw_index++;
                    continue;
                }
                if (!fat32_fill_file_from_dirent(&dirents[i], &lfn_state, &entry)) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (current == index) {
                    *out = entry;
                    out->dirent_index = raw_index;
                    out->dirent_lba = cluster_lba + sec;
                    out->dirent_offset = i * sizeof(struct fat32_dirent);
                    return 0;
                }
                fat32_lfn_state_reset(&lfn_state);
                raw_index++;
                current++;
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

int fat32_find_in_dir(struct fat32_volume *vol, const struct fat32_file *dir, const char *name, struct fat32_file *out) {
    return fat32_find_in_directory(vol, dir, name, out);
}

int fat32_find_path(struct fat32_volume *vol, const char *path, struct fat32_file *out) {
    struct fat32_file current;
    char segment[NOS_NAME_BUFFER_SIZE];
    int rc;

    if (vol == 0 || !vol->mounted || path == 0 || out == 0) {
        return -1;
    }
    fat32_get_root_dir(vol, &current);
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        *out = current;
        return 0;
    }
    for (;;) {
        rc = fat32_path_next_segment(&path, segment);
        if (rc <= 0) {
            return rc == 0 ? 0 : -1;
        }
        if (fat32_find_in_directory(vol, &current, segment, out) != 0) {
            return -1;
        }
        if (*path == '\0') {
            return 0;
        }
        if ((out->attributes & FAT32_ATTR_DIRECTORY) == 0) {
            return -1;
        }
        current = *out;
    }
}

int fat32_mkdir_path(struct fat32_volume *vol, const char *path, struct fat32_file *out) {
    struct fat32_file parent_dir;
    struct fat32_file existing;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t dir_cluster;

    if (vol == 0 || !vol->mounted || path == 0) {
        return -1;
    }
    if (fat32_split_parent_child_path(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return -1;
    }
    if (name[0] == '\0' ||
        (name[0] == '.' && name[1] == '\0') ||
        (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        return -1;
    }
    if (parent_path[0] == '\0') {
        fat32_get_root_dir(vol, &parent_dir);
    } else if (fat32_find_path(vol, parent_path, &parent_dir) != 0 ||
               (parent_dir.attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }
    if (fat32_find_in_directory(vol, &parent_dir, name, &existing) == 0) {
        return -1;
    }
    if (fat32_alloc_cluster(vol, &dir_cluster) != 0) {
        return -1;
    }
    if (fat32_init_directory_cluster(vol,
                                     dir_cluster,
                                     fat32_cluster_is_data(vol, parent_dir.first_cluster) ? parent_dir.first_cluster : vol->root_cluster) != 0) {
        (void)fat32_free_cluster_chain(vol, dir_cluster);
        return -1;
    }
    if (fat32_create_entry_in_directory(vol, &parent_dir, name, FAT32_ATTR_DIRECTORY, dir_cluster, &existing) != 0) {
        (void)fat32_free_cluster_chain(vol, dir_cluster);
        return -1;
    }
    if (out != 0) {
        *out = existing;
    }
    return 0;
}

int fat32_rmdir_path(struct fat32_volume *vol, const char *path) {
    struct fat32_file dir;
    struct fat32_file parent_dir;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];

    if (vol == 0 || !vol->mounted || path == 0) {
        return -1;
    }
    if (fat32_find_path(vol, path, &dir) != 0) {
        return -1;
    }
    if ((dir.attributes & FAT32_ATTR_DIRECTORY) == 0 || dir.dirent_lba == 0) {
        return -1;
    }
    if (!fat32_is_directory_empty(vol, &dir)) {
        return -1;
    }
    if (fat32_free_cluster_chain(vol, dir.first_cluster) != 0) {
        return -1;
    }
    if (fat32_split_parent_child_path(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return -1;
    }
    (void)name;
    if (parent_path[0] == '\0') {
        fat32_get_root_dir(vol, &parent_dir);
    } else if (fat32_find_path(vol, parent_path, &parent_dir) != 0 ||
               (parent_dir.attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }
    return fat32_delete_entry_chain(vol, &parent_dir, &dir);
}

int fat32_unlink_path(struct fat32_volume *vol, const char *path) {
    struct fat32_file file;
    struct fat32_file parent_dir;
    char parent_path[NOS_PATH_BUFFER_SIZE];
    char name[NOS_NAME_BUFFER_SIZE];

    if (vol == 0 || !vol->mounted || path == 0) {
        return -1;
    }
    if (fat32_find_path(vol, path, &file) != 0) {
        return -1;
    }
    if ((file.attributes & FAT32_ATTR_DIRECTORY) != 0 || file.dirent_lba == 0) {
        return -1;
    }
    if (fat32_cluster_is_data(vol, file.first_cluster) &&
        fat32_free_cluster_chain(vol, file.first_cluster) != 0) {
        return -1;
    }
    if (fat32_split_parent_child_path(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return -1;
    }
    (void)name;
    if (parent_path[0] == '\0') {
        fat32_get_root_dir(vol, &parent_dir);
    } else if (fat32_find_path(vol, parent_path, &parent_dir) != 0 ||
               (parent_dir.attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }
    return fat32_delete_entry_chain(vol, &parent_dir, &file);
}
