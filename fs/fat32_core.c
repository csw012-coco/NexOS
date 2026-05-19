#include "fs/fat32_internal.h"

void mem_copy(void *dest, const void *src, uint32_t size) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

void mem_set(void *dest, uint8_t value, uint32_t size) {
    uint8_t *d = (uint8_t *)dest;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = value;
    }
}

char to_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }
    return ch;
}

char to_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

int fat32_is_end_of_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && dirent->name[0] == 0x00;
}

int fat32_is_deleted_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && dirent->name[0] == 0xe5;
}

int fat32_is_lfn_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && dirent->attr == FAT32_ATTR_LFN;
}

int fat32_is_volume_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && (dirent->attr & FAT32_ATTR_VOLUME_ID) != 0;
}

void fat32_log_probe_failure(uint32_t partition_lba,
                                    const char *reason,
                                    const struct fat32_bpb *bpb,
                                    const uint8_t *sector_data) {
    if (!kprint_is_ready()) {
        return;
    }
    if (bpb == 0) {
        kprint("fat32: probe lba=%u failed: %s\n", partition_lba, reason);
        return;
    }
    kprint("fat32: probe lba=%u failed: %s sig=%x%x jump=%x bps=%u spc=%u rsv=%u fats=%u rootent=%u tot16=%u tot32=%u spf16=%u spf32=%u root=%u label=%c%c%c%c%c%c%c%c\n",
           partition_lba,
           reason,
           sector_data != 0 ? (uint32_t)sector_data[511] : 0u,
           sector_data != 0 ? (uint32_t)sector_data[510] : 0u,
           (uint32_t)bpb->jump[0],
           (uint32_t)bpb->bytes_per_sector,
           (uint32_t)bpb->sectors_per_cluster,
           (uint32_t)bpb->reserved_sector_count,
           (uint32_t)bpb->table_count,
           (uint32_t)bpb->root_entry_count,
           (uint32_t)bpb->total_sectors_16,
           bpb->total_sectors_32,
           (uint32_t)bpb->table_size_16,
           bpb->table_size_32,
           bpb->root_cluster,
           (char)bpb->fat_type_label[0],
           (char)bpb->fat_type_label[1],
           (char)bpb->fat_type_label[2],
           (char)bpb->fat_type_label[3],
           (char)bpb->fat_type_label[4],
           (char)bpb->fat_type_label[5],
           (char)bpb->fat_type_label[6],
           (char)bpb->fat_type_label[7]);
}

int fat32_is_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

int fat32_lba_in_volume(const struct fat32_volume *vol, uint32_t lba) {
    uint64_t volume_end;

    if (vol == 0 || vol->total_sectors == 0u) {
        return 0;
    }
    volume_end = (uint64_t)vol->partition_lba + (uint64_t)vol->total_sectors;
    return (uint64_t)lba >= (uint64_t)vol->partition_lba && (uint64_t)lba < volume_end;
}

void fat32_log_io_failure(const struct fat32_volume *vol,
                                 const char *op,
                                 uint32_t lba,
                                 const char *reason) {
    if (!kprint_is_ready()) {
        return;
    }
    kprint("fat32: %s lba=%u failed: %s partition=%u sectors=%u data=%u clusters=%u\n",
           op,
           lba,
           reason,
           vol != 0 ? vol->partition_lba : 0u,
           vol != 0 ? vol->total_sectors : 0u,
           vol != 0 ? vol->data_start_lba : 0u,
           vol != 0 ? vol->cluster_count : 0u);
}

void fat32_log_file_failure(const struct fat32_volume *vol,
                                   const struct fat32_file *file,
                                   const char *op,
                                   const char *reason) {
    if (!kprint_is_ready()) {
        return;
    }
    kprint("fat32: %s file=%s failed: %s first_cluster=%u size=%u partition=%u data=%u clusters=%u\n",
           op,
           file != 0 ? file->name : "?",
           reason,
           file != 0 ? file->first_cluster : 0u,
           file != 0 ? file->size : 0u,
           vol != 0 ? vol->partition_lba : 0u,
           vol != 0 ? vol->data_start_lba : 0u,
           vol != 0 ? vol->cluster_count : 0u);
}

int fat32_read_sector(struct fat32_volume *vol, uint32_t lba, void *buffer) {
    if (vol == 0 || buffer == 0 || !fat32_lba_in_volume(vol, lba)) {
        fat32_log_io_failure(vol, "read", lba, "bounds");
        return -1;
    }
    if (blockdev_read(vol->bdev, lba, 1, buffer) != 0) {
        fat32_log_io_failure(vol, "read", lba, "block");
        return -1;
    }
    return 0;
}

int fat32_write_sector(struct fat32_volume *vol, uint32_t lba, const void *buffer) {
    if (vol == 0 || buffer == 0 || !fat32_lba_in_volume(vol, lba)) {
        fat32_log_io_failure(vol, "write", lba, "bounds");
        return -1;
    }
    if (blockdev_write(vol->bdev, lba, 1, buffer) != 0) {
        fat32_log_io_failure(vol, "write", lba, "block");
        return -1;
    }
    return 0;
}

int fat32_cluster_is_data(const struct fat32_volume *vol, uint32_t cluster) {
    return vol != 0 && cluster >= 2u && cluster - 2u < vol->cluster_count;
}

uint32_t fat32_cluster_lba(struct fat32_volume *vol, uint32_t cluster) {
    return vol->data_start_lba + (cluster - 2u) * vol->sectors_per_cluster;
}

int fat32_next_cluster(struct fat32_volume *vol, uint32_t cluster, uint32_t *next) {
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t fat_index;
    uint32_t *entries;

    if (!fat32_cluster_is_data(vol, cluster) || next == 0) {
        return -1;
    }
    fat_offset = cluster * 4u;
    fat_sector = vol->fat_start_lba + (fat_offset / 512u);
    fat_index = (fat_offset % 512u) / 4u;
    if (fat32_read_sector(vol, fat_sector, vol->sector_buffer) != 0) {
        return -1;
    }
    entries = (uint32_t *)vol->sector_buffer;
    *next = entries[fat_index] & 0x0fffffffu;
    if (*next != 0u && *next < FAT32_CLUSTER_END && !fat32_cluster_is_data(vol, *next)) {
        return -1;
    }
    return 0;
}

int fat32_read_fat_entry(struct fat32_volume *vol, uint32_t cluster, uint32_t *value) {
    return fat32_next_cluster(vol, cluster, value);
}

uint32_t fat32_min_u32(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

int fat32_calc_sector_window(uint32_t sector_start,
                                    uint32_t sector_end,
                                    uint32_t data_limit,
                                    uint32_t offset,
                                    uint32_t request_end,
                                    uint32_t *copy_start_out,
                                    uint32_t *copy_size_out) {
    uint32_t copy_start;
    uint32_t copy_end;

    if (copy_start_out == 0 || copy_size_out == 0) {
        return 0;
    }
    if (sector_start >= data_limit) {
        return 0;
    }
    sector_end = fat32_min_u32(sector_end, data_limit);
    if (offset >= sector_end) {
        return 0;
    }

    copy_start = offset > sector_start ? offset : sector_start;
    copy_end = fat32_min_u32(sector_end, request_end);
    if (copy_end <= copy_start) {
        return 0;
    }
    *copy_start_out = copy_start;
    *copy_size_out = copy_end - copy_start;
    return 1;
}

int fat32_write_fat_entry(struct fat32_volume *vol, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset;
    uint32_t fat_index;
    uint32_t table_count;

    if (!fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }
    if (value != 0u && value < FAT32_CLUSTER_END && !fat32_cluster_is_data(vol, value)) {
        return -1;
    }
    fat_offset = cluster * 4u;
    fat_index = (fat_offset % 512u) / 4u;
    table_count = vol->table_count == 0 ? 1u : vol->table_count;
    for (uint32_t table = 0; table < table_count; table++) {
        uint32_t fat_sector = vol->fat_start_lba + table * vol->sectors_per_fat + (fat_offset / 512u);
        uint32_t *entries;

        if (fat32_read_sector(vol, fat_sector, vol->sector_buffer) != 0) {
            return -1;
        }
        entries = (uint32_t *)vol->sector_buffer;
        entries[fat_index] = (entries[fat_index] & 0xf0000000u) | (value & 0x0fffffffu);
        if (fat32_write_sector(vol, fat_sector, vol->sector_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

int fat32_zero_cluster(struct fat32_volume *vol, uint32_t cluster) {
    uint32_t cluster_lba;

    if (!fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }
    cluster_lba = fat32_cluster_lba(vol, cluster);
    mem_set(vol->sector_buffer, 0, sizeof(vol->sector_buffer));
    for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
        if (fat32_write_sector(vol, cluster_lba + sec, vol->sector_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

int fat32_alloc_cluster(struct fat32_volume *vol, uint32_t *cluster_out) {
    uint32_t value;

    if (cluster_out == 0) {
        return -1;
    }
    for (uint32_t cluster = 2; cluster < vol->cluster_count + 2u; cluster++) {
        if (fat32_read_fat_entry(vol, cluster, &value) != 0) {
            return -1;
        }
        if (value != 0) {
            continue;
        }
        if (fat32_write_fat_entry(vol, cluster, FAT32_CLUSTER_END) != 0) {
            return -1;
        }
        if (fat32_zero_cluster(vol, cluster) != 0) {
            return -1;
        }
        *cluster_out = cluster;
        return 0;
    }
    return -1;
}

int fat32_space_info(struct fat32_volume *vol,
                     uint32_t *block_size_out,
                     uint64_t *total_blocks_out,
                     uint64_t *free_blocks_out) {
    uint64_t free_clusters = 0;
    uint32_t value;

    if (vol == 0 || !vol->mounted || vol->bdev == 0 ||
        block_size_out == 0 || total_blocks_out == 0 || free_blocks_out == 0) {
        return -1;
    }
    for (uint32_t cluster = 2; cluster < vol->cluster_count + 2u; cluster++) {
        if (fat32_read_fat_entry(vol, cluster, &value) != 0) {
            return -1;
        }
        if (value == 0u) {
            free_clusters++;
        }
    }
    *block_size_out = vol->bdev->block_size * vol->sectors_per_cluster;
    *total_blocks_out = vol->cluster_count;
    *free_blocks_out = free_clusters;
    return 0;
}
