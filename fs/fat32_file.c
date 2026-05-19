#include "fs/fat32_internal.h"

int fat32_truncate_file(struct fat32_volume *vol, struct fat32_file *file) {
    if (vol == 0 || !vol->mounted || file == 0 || (file->attributes & FAT32_ATTR_DIRECTORY) != 0) {
        return -1;
    }
    file->size = 0;
    return fat32_update_dirent(vol, file);
}

int fat32_read_file(struct fat32_volume *vol, const struct fat32_file *file, void *buffer, uint32_t buffer_size, uint32_t *bytes_read) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t cluster;
    uint32_t written = 0;

    if (vol == 0 || !vol->mounted || file == 0 || buffer == 0 || bytes_read == 0) {
        return -1;
    }
    if (file->size > buffer_size || (file->attributes & FAT32_ATTR_DIRECTORY) != 0) {
        return -1;
    }
    if (file->size != 0u && !fat32_cluster_is_data(vol, file->first_cluster)) {
        fat32_log_file_failure(vol, file, "read", "bad first cluster");
        return -1;
    }

    cluster = file->first_cluster;
    while (fat32_cluster_is_data(vol, cluster) && written < file->size) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);
        for (uint32_t sec = 0; sec < vol->sectors_per_cluster && written < file->size; sec++) {
            uint32_t remaining = file->size - written;
            uint32_t copy_size = remaining < 512 ? remaining : 512;
            if (fat32_read_sector(vol, cluster_lba + sec, vol->sector_buffer) != 0) {
                return -1;
            }
            mem_copy(out + written, vol->sector_buffer, copy_size);
            written += copy_size;
        }
        if (written >= file->size) {
            break;
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            fat32_log_file_failure(vol, file, "read", "bad cluster chain");
            return -1;
        }
    }

    *bytes_read = written;
    return written == file->size ? 0 : -1;
}

int fat32_read_file_range(struct fat32_volume *vol,
                          const struct fat32_file *file,
                          uint32_t offset,
                          void *buffer,
                          uint32_t buffer_size,
                          uint32_t *bytes_read) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t cluster;
    uint32_t file_offset = 0;
    uint32_t written = 0;

    if (vol == 0 || !vol->mounted || file == 0 || buffer == 0 || bytes_read == 0) {
        return -1;
    }
    if ((file->attributes & FAT32_ATTR_DIRECTORY) != 0 || offset > file->size) {
        return -1;
    }
    if (offset == file->size || buffer_size == 0) {
        *bytes_read = 0;
        return 0;
    }
    if (!fat32_cluster_is_data(vol, file->first_cluster)) {
        fat32_log_file_failure(vol, file, "read", "bad first cluster");
        return -1;
    }

    cluster = file->first_cluster;
    while (fat32_cluster_is_data(vol, cluster) && file_offset < file->size && written < buffer_size) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);
        for (uint32_t sec = 0; sec < vol->sectors_per_cluster && file_offset < file->size && written < buffer_size; sec++) {
            uint32_t sector_start = file_offset;
            uint32_t sector_end = sector_start + 512u;
            uint32_t sector_copy_start;
            uint32_t copy_size;

            if (fat32_read_sector(vol, cluster_lba + sec, vol->sector_buffer) != 0) {
                return -1;
            }
            if (!fat32_calc_sector_window(sector_start,
                                          sector_end,
                                          file->size,
                                          offset,
                                          offset + buffer_size,
                                          &sector_copy_start,
                                          &copy_size)) {
                file_offset += 512u;
                continue;
            }
            mem_copy(out + written,
                     vol->sector_buffer + (sector_copy_start - sector_start),
                     copy_size);
            written += copy_size;
            file_offset += 512u;
        }
        if (written >= buffer_size) {
            break;
        }
        if (file_offset >= file->size) {
            break;
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            fat32_log_file_failure(vol, file, "read", "bad cluster chain");
            return -1;
        }
    }

    *bytes_read = written;
    return 0;
}

int fat32_ensure_file_first_cluster(struct fat32_volume *vol, struct fat32_file *file) {
    uint32_t new_cluster = 0u;

    if (fat32_cluster_is_data(vol, file->first_cluster)) {
        return 0;
    }
    if (file->size != 0u) {
        fat32_log_file_failure(vol, file, "write", "bad first cluster");
        return -1;
    }
    if (file->first_cluster != 0u) {
        fat32_log_file_failure(vol, file, "write", "invalid empty first cluster");
        return -1;
    }
    if (fat32_alloc_cluster(vol, &new_cluster) != 0) {
        fat32_log_file_failure(vol, file, "write", "cluster alloc");
        return -1;
    }
    file->first_cluster = new_cluster;
    if (fat32_update_dirent(vol, file) != 0) {
        (void)fat32_free_cluster_chain(vol, new_cluster);
        file->first_cluster = 0u;
        fat32_log_file_failure(vol, file, "write", "dirent update");
        return -1;
    }
    return 0;
}

int fat32_write_file_range(struct fat32_volume *vol,
                           struct fat32_file *file,
                           uint32_t offset,
                           const void *buffer,
                           uint32_t buffer_size,
                           uint32_t *bytes_written) {
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t cluster;
    uint32_t file_offset = 0;
    uint32_t written = 0;
    uint32_t limit;
    uint32_t new_size;

    if (vol == 0 || !vol->mounted || file == 0 || buffer == 0 || bytes_written == 0) {
        return -1;
    }
    if ((file->attributes & FAT32_ATTR_DIRECTORY) != 0 || offset > file->size) {
        return -1;
    }
    if (buffer_size == 0) {
        *bytes_written = 0;
        return 0;
    }
    if (fat32_ensure_file_first_cluster(vol, file) != 0) {
        return -1;
    }

    limit = offset + buffer_size;
    if (limit < offset) {
        return -1;
    }

    cluster = file->first_cluster;
    while (fat32_cluster_is_data(vol, cluster) && file_offset < limit) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);
        for (uint32_t sec = 0; sec < vol->sectors_per_cluster && file_offset < limit; sec++) {
            uint32_t sector_start = file_offset;
            uint32_t sector_end = sector_start + 512u;
            uint32_t sector_copy_start;
            uint32_t copy_size;

            if (fat32_read_sector(vol, cluster_lba + sec, vol->sector_buffer) != 0) {
                return -1;
            }
            if (!fat32_calc_sector_window(sector_start,
                                          sector_end,
                                          limit,
                                          offset,
                                          limit,
                                          &sector_copy_start,
                                          &copy_size)) {
                file_offset += 512u;
                continue;
            }
            mem_copy(vol->sector_buffer + (sector_copy_start - sector_start),
                     in + written,
                     copy_size);
            if (fat32_write_sector(vol, cluster_lba + sec, vol->sector_buffer) != 0) {
                return -1;
            }
            written += copy_size;
            file_offset += 512u;
        }
        if (written >= buffer_size || file_offset >= limit) {
            break;
        }
        {
            uint32_t next_cluster;

            if (fat32_next_cluster(vol, cluster, &next_cluster) != 0) {
                fat32_log_file_failure(vol, file, "write", "bad cluster chain");
                return -1;
            }
            if (next_cluster >= FAT32_CLUSTER_END) {
                if (fat32_append_file_cluster(vol, cluster, &next_cluster) != 0) {
                    fat32_log_file_failure(vol, file, "write", "append cluster");
                    return -1;
                }
            } else if (!fat32_cluster_is_data(vol, next_cluster)) {
                fat32_log_file_failure(vol, file, "write", "invalid next cluster");
                return -1;
            }
            cluster = next_cluster;
        }
        if (!fat32_cluster_is_data(vol, cluster)) {
            fat32_log_file_failure(vol, file, "write", "invalid cluster");
            return -1;
        }
    }

    *bytes_written = written;
    new_size = offset + written;
    if (new_size > file->size) {
        file->size = new_size;
        if (fat32_update_dirent(vol, file) != 0) {
            fat32_log_file_failure(vol, file, "write", "dirent update");
            return -1;
        }
    }
    return 0;
}
