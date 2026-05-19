#ifndef NEXOS_FS_FAT32_INTERNAL_H
#define NEXOS_FS_FAT32_INTERNAL_H

#include "fs/fat32.h"
#include "kernel/public/core/kprint.h"

enum {
    FAT32_ATTR_VOLUME_ID = 0x08,
    FAT32_ATTR_DIRECTORY = 0x10,
    FAT32_ATTR_LFN = 0x0f,
    FAT32_CLUSTER_END = 0x0ffffff8u
};

struct fat32_bpb {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t table_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t table_size_16;
    uint16_t sectors_per_track;
    uint16_t head_side_count;
    uint32_t hidden_sector_count;
    uint32_t total_sectors_32;
    uint32_t table_size_32;
    uint16_t extended_flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fat_info;
    uint16_t backup_bs_sector;
    uint8_t reserved_0[12];
    uint8_t drive_number;
    uint8_t reserved_1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fat_type_label[8];
} __attribute__((packed));

struct fat32_dirent {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct fat32_lfn_dirent {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster_low;
    uint16_t name3[2];
} __attribute__((packed));

struct fat32_lfn_state {
    char name[NOS_NAME_BUFFER_SIZE];
    uint8_t valid;
    uint8_t checksum;
    uint8_t next_order;
    uint8_t total_entries;
};


void mem_copy(void *dest, const void *src, uint32_t size);
void mem_set(void *dest, uint8_t value, uint32_t size);
char to_upper(char ch);
char to_lower(char ch);
int fat32_is_end_of_dirent(const struct fat32_dirent *dirent);
int fat32_is_deleted_dirent(const struct fat32_dirent *dirent);
int fat32_is_lfn_dirent(const struct fat32_dirent *dirent);
int fat32_is_volume_dirent(const struct fat32_dirent *dirent);
void fat32_log_probe_failure(uint32_t partition_lba,
                             const char *reason,
                             const struct fat32_bpb *bpb,
                             const uint8_t *sector_data);
int fat32_is_power_of_two(uint32_t value);
int fat32_lba_in_volume(const struct fat32_volume *vol, uint32_t lba);
void fat32_log_io_failure(const struct fat32_volume *vol,
                          const char *op,
                          uint32_t lba,
                          const char *reason);
void fat32_log_file_failure(const struct fat32_volume *vol,
                            const struct fat32_file *file,
                            const char *op,
                            const char *reason);
int fat32_read_sector(struct fat32_volume *vol, uint32_t lba, void *buffer);
int fat32_write_sector(struct fat32_volume *vol, uint32_t lba, const void *buffer);
int fat32_cluster_is_data(const struct fat32_volume *vol, uint32_t cluster);
uint32_t fat32_cluster_lba(struct fat32_volume *vol, uint32_t cluster);
int fat32_next_cluster(struct fat32_volume *vol, uint32_t cluster, uint32_t *next);
int fat32_read_fat_entry(struct fat32_volume *vol, uint32_t cluster, uint32_t *value);
uint32_t fat32_min_u32(uint32_t lhs, uint32_t rhs);
int fat32_calc_sector_window(uint32_t sector_start,
                             uint32_t sector_end,
                             uint32_t data_limit,
                             uint32_t offset,
                             uint32_t request_end,
                             uint32_t *copy_start_out,
                             uint32_t *copy_size_out);
int fat32_write_fat_entry(struct fat32_volume *vol, uint32_t cluster, uint32_t value);
int fat32_zero_cluster(struct fat32_volume *vol, uint32_t cluster);
int fat32_alloc_cluster(struct fat32_volume *vol, uint32_t *cluster_out);
int fat32_update_dirent(struct fat32_volume *vol, const struct fat32_file *file);
void fat32_format_name83(const uint8_t raw[11], char *out);
int fat32_ascii_casecmp(const char *lhs, const char *rhs);
int fat32_is_space_or_dot(const char *text);
int fat32_short_name_char_allowed(char ch);
char fat32_sanitize_short_char(char ch);
void fat32_normalize_name83(const char *input, char out[12]);
int fat32_name_fits_short_exact(const char *input);
uint8_t fat32_short_name_checksum(const uint8_t raw[11]);
void fat32_lfn_state_reset(struct fat32_lfn_state *state);
int fat32_lfn_append_char(char *dst, uint32_t dst_size, uint32_t index, uint16_t value);
int fat32_lfn_capture(struct fat32_lfn_state *state, const struct fat32_lfn_dirent *lfn);
void fat32_file_reset(struct fat32_file *file);
void fat32_copy_name(char *dst, uint32_t dst_size, const char *src);
int fat32_fill_file_from_dirent(const struct fat32_dirent *dirent,
                                const struct fat32_lfn_state *lfn_state,
                                struct fat32_file *out);
int fat32_match_dirent_name(const struct fat32_dirent *dirent,
                            const struct fat32_lfn_state *lfn_state,
                            const char *input);
void fat32_make_short_alias(const char *input, uint8_t out[11], uint32_t serial);
uint32_t fat32_dir_first_cluster(struct fat32_volume *vol, const struct fat32_file *dir);
int fat32_list_directory(struct fat32_volume *vol,
                         const struct fat32_file *dir,
                         struct fat32_file *entries,
                         uint32_t max_entries,
                         uint32_t *entry_count);
int fat32_find_in_directory(struct fat32_volume *vol,
                            const struct fat32_file *dir,
                            const char *name,
                            struct fat32_file *out);
uint32_t fat32_name_length(const char *text);
int fat32_name_needs_lfn(const char *name);
uint32_t fat32_lfn_entry_count_for_name(const char *name);
int fat32_short_name_exists(struct fat32_volume *vol,
                            const struct fat32_file *dir,
                            const uint8_t raw_name[11]);
int fat32_build_unique_short_name(struct fat32_volume *vol,
                                  const struct fat32_file *dir,
                                  const char *name,
                                  uint8_t raw_name[11]);
int fat32_locate_free_dirent_span(struct fat32_volume *vol,
                                  const struct fat32_file *dir,
                                  uint32_t needed,
                                  uint32_t lbas_out[],
                                  uint32_t offsets_out[],
                                  uint32_t *raw_index_out);
int fat32_append_directory_cluster(struct fat32_volume *vol, const struct fat32_file *dir);
int fat32_append_file_cluster(struct fat32_volume *vol,
                              uint32_t cluster,
                              uint32_t *new_cluster_out);
int fat32_write_raw_dirent(struct fat32_volume *vol,
                           uint32_t lba,
                           uint32_t offset,
                           const void *dirent,
                           uint32_t size);
void fat32_populate_lfn_dirent(struct fat32_lfn_dirent *lfn,
                               const char *name,
                               uint32_t total_entries,
                               uint32_t order,
                               uint8_t checksum);
void fat32_set_dirent_cluster(struct fat32_dirent *dirent, uint32_t cluster);
int fat32_create_entry_in_directory(struct fat32_volume *vol,
                                    const struct fat32_file *dir,
                                    const char *name,
                                    uint8_t attributes,
                                    uint32_t first_cluster,
                                    struct fat32_file *out);
int fat32_raw_entry_position(struct fat32_volume *vol,
                             const struct fat32_file *dir,
                             uint32_t raw_index,
                             uint32_t *lba_out,
                             uint32_t *offset_out);
int fat32_delete_entry_chain(struct fat32_volume *vol,
                             const struct fat32_file *dir,
                             const struct fat32_file *file);
int fat32_init_directory_cluster(struct fat32_volume *vol, uint32_t self_cluster, uint32_t parent_cluster);
int fat32_free_cluster_chain(struct fat32_volume *vol, uint32_t first_cluster);
int fat32_split_parent_child_path(const char *path,
                                  char *parent,
                                  uint32_t parent_size,
                                  char *name,
                                  uint32_t name_size);
int fat32_is_directory_empty(struct fat32_volume *vol, const struct fat32_file *dir);
int fat32_path_next_segment(const char **path_io, char segment[NOS_NAME_BUFFER_SIZE]);
int fat32_ensure_file_first_cluster(struct fat32_volume *vol, struct fat32_file *file);

#endif
