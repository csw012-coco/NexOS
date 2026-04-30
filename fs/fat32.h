#pragma once

#include <stdint.h>
#include "block/blockdev.h"
#include "kernel/public/fs/fat32_types.h"
#include "kernel/public/sys/system_limits.h"

struct fat32_volume {
    struct block_device *bdev;
    uint32_t partition_lba;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t sectors_per_cluster;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint32_t cluster_count;
    uint8_t table_count;
    uint8_t mounted;
};

int fat32_mount(struct fat32_volume *vol, struct block_device *bdev, uint32_t partition_lba);
void fat32_get_root_dir(struct fat32_volume *vol, struct fat32_file *out);
int fat32_list_root(struct fat32_volume *vol, struct fat32_file *entries, uint32_t max_entries, uint32_t *entry_count);
int fat32_find_root(struct fat32_volume *vol, const char *name83, struct fat32_file *out);
int fat32_create_root(struct fat32_volume *vol, const char *name83, struct fat32_file *out);
int fat32_create_path(struct fat32_volume *vol, const char *path, struct fat32_file *out);
int fat32_get_dir_entry(struct fat32_volume *vol, const struct fat32_file *dir, uint32_t index, struct fat32_file *out);
int fat32_find_in_dir(struct fat32_volume *vol, const struct fat32_file *dir, const char *name83, struct fat32_file *out);
int fat32_find_path(struct fat32_volume *vol, const char *path, struct fat32_file *out);
void fat32_debug_lookup_path(struct fat32_volume *vol, const char *path);
int fat32_mkdir_path(struct fat32_volume *vol, const char *path, struct fat32_file *out);
int fat32_rmdir_path(struct fat32_volume *vol, const char *path);
int fat32_unlink_path(struct fat32_volume *vol, const char *path);
int fat32_truncate_file(struct fat32_volume *vol, struct fat32_file *file);
int fat32_read_file(struct fat32_volume *vol, const struct fat32_file *file, void *buffer, uint32_t buffer_size, uint32_t *bytes_read);
int fat32_get_root_entry(struct fat32_volume *vol, uint32_t index, struct fat32_file *out);
int fat32_read_file_range(struct fat32_volume *vol,
                          const struct fat32_file *file,
                          uint32_t offset,
                          void *buffer,
                          uint32_t buffer_size,
                          uint32_t *bytes_read);
int fat32_write_file_range(struct fat32_volume *vol,
                           struct fat32_file *file,
                           uint32_t offset,
                           const void *buffer,
                           uint32_t buffer_size,
                           uint32_t *bytes_written);
