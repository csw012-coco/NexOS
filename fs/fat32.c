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

static uint8_t sector_buffer[512];

static void mem_copy(void *dest, const void *src, uint32_t size) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void mem_set(void *dest, uint8_t value, uint32_t size) {
    uint8_t *d = (uint8_t *)dest;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = value;
    }
}

static char to_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }
    return ch;
}

static char to_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int fat32_is_end_of_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && dirent->name[0] == 0x00;
}

static int fat32_is_deleted_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && dirent->name[0] == 0xe5;
}

static int fat32_is_lfn_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && dirent->attr == FAT32_ATTR_LFN;
}

static int fat32_is_volume_dirent(const struct fat32_dirent *dirent) {
    return dirent != 0 && (dirent->attr & FAT32_ATTR_VOLUME_ID) != 0;
}

static void fat32_log_probe_failure(uint32_t partition_lba,
                                    const char *reason,
                                    const struct fat32_bpb *bpb) {
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
           (uint32_t)sector_buffer[511],
           (uint32_t)sector_buffer[510],
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

static int fat32_is_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static int fat32_lba_in_volume(const struct fat32_volume *vol, uint32_t lba) {
    uint64_t volume_end;

    if (vol == 0 || vol->total_sectors == 0u) {
        return 0;
    }
    volume_end = (uint64_t)vol->partition_lba + (uint64_t)vol->total_sectors;
    return (uint64_t)lba >= (uint64_t)vol->partition_lba && (uint64_t)lba < volume_end;
}

static void fat32_log_io_failure(const struct fat32_volume *vol,
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

static void fat32_log_file_failure(const struct fat32_volume *vol,
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

static int fat32_read_sector(struct fat32_volume *vol, uint32_t lba, void *buffer) {
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

static int fat32_write_sector(struct fat32_volume *vol, uint32_t lba, const void *buffer) {
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

static int fat32_cluster_is_data(const struct fat32_volume *vol, uint32_t cluster) {
    return vol != 0 && cluster >= 2u && cluster - 2u < vol->cluster_count;
}

static uint32_t fat32_cluster_lba(struct fat32_volume *vol, uint32_t cluster) {
    return vol->data_start_lba + (cluster - 2u) * vol->sectors_per_cluster;
}

static int fat32_next_cluster(struct fat32_volume *vol, uint32_t cluster, uint32_t *next) {
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
    if (fat32_read_sector(vol, fat_sector, sector_buffer) != 0) {
        return -1;
    }
    entries = (uint32_t *)sector_buffer;
    *next = entries[fat_index] & 0x0fffffffu;
    if (*next != 0u && *next < FAT32_CLUSTER_END && !fat32_cluster_is_data(vol, *next)) {
        return -1;
    }
    return 0;
}

static int fat32_read_fat_entry(struct fat32_volume *vol, uint32_t cluster, uint32_t *value) {
    return fat32_next_cluster(vol, cluster, value);
}

static uint32_t fat32_min_u32(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

static int fat32_calc_sector_window(uint32_t sector_start,
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

static int fat32_write_fat_entry(struct fat32_volume *vol, uint32_t cluster, uint32_t value) {
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

        if (fat32_read_sector(vol, fat_sector, sector_buffer) != 0) {
            return -1;
        }
        entries = (uint32_t *)sector_buffer;
        entries[fat_index] = (entries[fat_index] & 0xf0000000u) | (value & 0x0fffffffu);
        if (fat32_write_sector(vol, fat_sector, sector_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

static int fat32_zero_cluster(struct fat32_volume *vol, uint32_t cluster) {
    uint32_t cluster_lba;

    if (!fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }
    cluster_lba = fat32_cluster_lba(vol, cluster);
    mem_set(sector_buffer, 0, sizeof(sector_buffer));
    for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
        if (fat32_write_sector(vol, cluster_lba + sec, sector_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

static int fat32_alloc_cluster(struct fat32_volume *vol, uint32_t *cluster_out) {
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

static int fat32_update_dirent(struct fat32_volume *vol, const struct fat32_file *file) {
    struct fat32_dirent *dirent;

    if (vol == 0 || file == 0 || file->dirent_lba == 0 || file->dirent_offset + sizeof(struct fat32_dirent) > sizeof(sector_buffer)) {
        return -1;
    }
    if (fat32_read_sector(vol, file->dirent_lba, sector_buffer) != 0) {
        return -1;
    }
    dirent = (struct fat32_dirent *)(sector_buffer + file->dirent_offset);
    dirent->first_cluster_high = (uint16_t)(file->first_cluster >> 16);
    dirent->first_cluster_low = (uint16_t)(file->first_cluster & 0xffffu);
    dirent->file_size = file->size;
    dirent->attr = file->attributes;
    return fat32_write_sector(vol, file->dirent_lba, sector_buffer);
}

static void fat32_format_name83(const uint8_t raw[11], char *out) {
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 8 && raw[i] != ' '; i++) {
        out[pos++] = (char)raw[i];
    }
    if (raw[8] != ' ') {
        out[pos++] = '.';
        for (uint32_t i = 8; i < 11 && raw[i] != ' '; i++) {
            out[pos++] = (char)raw[i];
        }
    }
    out[pos] = '\0';
}

static int fat32_ascii_casecmp(const char *lhs, const char *rhs) {
    uint32_t i = 0;

    if (lhs == 0 || rhs == 0) {
        return lhs == rhs ? 0 : 1;
    }
    for (;;) {
        char a = to_lower(lhs[i]);
        char b = to_lower(rhs[i]);

        if (a != b) {
            return (int)((unsigned char)a - (unsigned char)b);
        }
        if (lhs[i] == '\0') {
            return 0;
        }
        i++;
    }
}

static int fat32_is_space_or_dot(const char *text) {
    uint32_t i = 0;

    if (text == 0 || text[0] == '\0') {
        return 1;
    }
    while (text[i] != '\0') {
        if (text[i] != ' ' && text[i] != '.') {
            return 0;
        }
        i++;
    }
    return 1;
}

static int fat32_short_name_char_allowed(char ch) {
    ch = to_upper(ch);
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '$' || ch == '%' || ch == '\'' || ch == '-' ||
           ch == '_' || ch == '@' || ch == '~' || ch == '`' ||
           ch == '!' || ch == '(' || ch == ')' || ch == '{' ||
           ch == '}' || ch == '^' || ch == '#' || ch == '&';
}

static char fat32_sanitize_short_char(char ch) {
    ch = to_upper(ch);
    return fat32_short_name_char_allowed(ch) ? ch : '_';
}

static void fat32_normalize_name83(const char *input, char out[12]) {
    uint32_t name_pos = 0;
    uint32_t ext_pos = 8;
    int in_ext = 0;

    for (uint32_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    out[11] = '\0';

    while (input != 0 && *input != '\0') {
        char ch = *input++;

        if (ch == '.') {
            if (in_ext) {
                continue;
            }
            in_ext = 1;
            continue;
        }
        if (!in_ext) {
            if (name_pos < 8u) {
                out[name_pos++] = fat32_sanitize_short_char(ch);
            }
        } else if (ext_pos < 11u) {
            out[ext_pos++] = fat32_sanitize_short_char(ch);
        }
    }
}

static int fat32_name_fits_short_exact(const char *input) {
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    int in_ext = 0;
    int saw_char = 0;

    if (input == 0 || input[0] == '\0' || fat32_is_space_or_dot(input)) {
        return 0;
    }
    while (*input != '\0') {
        char ch = *input++;

        if (ch == '.') {
            if (in_ext) {
                return 0;
            }
            in_ext = 1;
            continue;
        }
        if (!fat32_short_name_char_allowed(ch)) {
            return 0;
        }
        saw_char = 1;
        if (!in_ext) {
            if (++base_len > 8u) {
                return 0;
            }
        } else {
            if (++ext_len > 3u) {
                return 0;
            }
        }
    }
    return saw_char && base_len != 0;
}

static uint8_t fat32_short_name_checksum(const uint8_t raw[11]) {
    uint8_t sum = 0;

    for (uint32_t i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + raw[i]);
    }
    return sum;
}

static void fat32_lfn_state_reset(struct fat32_lfn_state *state) {
    if (state == 0) {
        return;
    }
    mem_set(state->name, 0, sizeof(state->name));
    state->valid = 0;
    state->checksum = 0;
    state->next_order = 0;
    state->total_entries = 0;
}

static int fat32_lfn_append_char(char *dst, uint32_t dst_size, uint32_t index, uint16_t value) {
    char ch;

    if (dst == 0 || dst_size == 0 || index + 1u >= dst_size) {
        return 0;
    }
    if (value == 0x0000u) {
        dst[index] = '\0';
        return 1;
    }
    if (value == 0xffffu) {
        if (dst[index] == '\0') {
            return 1;
        }
        dst[index] = '\0';
        return 1;
    }
    ch = (value & 0xff00u) == 0 ? (char)(value & 0x00ffu) : '?';
    if (ch == '\0') {
        dst[index] = '\0';
        return 1;
    }
    dst[index] = ch;
    dst[index + 1u] = '\0';
    return 1;
}

static int fat32_lfn_capture(struct fat32_lfn_state *state, const struct fat32_lfn_dirent *lfn) {
    uint8_t order;
    uint32_t base;

    if (state == 0 || lfn == 0 || lfn->attr != FAT32_ATTR_LFN) {
        fat32_lfn_state_reset(state);
        return 0;
    }
    order = (uint8_t)(lfn->order & 0x1fu);
    if (order == 0u || order > 20u || lfn->type != 0u) {
        fat32_lfn_state_reset(state);
        return 0;
    }
    if ((lfn->order & 0x40u) != 0u) {
        fat32_lfn_state_reset(state);
        state->valid = 1u;
        state->checksum = lfn->checksum;
        state->next_order = order;
        state->total_entries = order;
    }
    if (!state->valid || state->checksum != lfn->checksum || state->next_order != order) {
        fat32_lfn_state_reset(state);
        return 0;
    }

    base = (uint32_t)(order - 1u) * 13u;
    for (uint32_t i = 0; i < 5u; i++) {
        if (!fat32_lfn_append_char(state->name, sizeof(state->name), base + i, lfn->name1[i])) {
            fat32_lfn_state_reset(state);
            return 0;
        }
    }
    for (uint32_t i = 0; i < 6u; i++) {
        if (!fat32_lfn_append_char(state->name, sizeof(state->name), base + 5u + i, lfn->name2[i])) {
            fat32_lfn_state_reset(state);
            return 0;
        }
    }
    for (uint32_t i = 0; i < 2u; i++) {
        if (!fat32_lfn_append_char(state->name, sizeof(state->name), base + 11u + i, lfn->name3[i])) {
            fat32_lfn_state_reset(state);
            return 0;
        }
    }

    if (state->next_order > 0u) {
        state->next_order--;
    }
    return 1;
}

static void fat32_file_reset(struct fat32_file *file) {
    if (file == 0) {
        return;
    }
    file->name[0] = '\0';
    file->first_cluster = 0;
    file->size = 0;
    file->attributes = 0;
    file->lfn_entry_count = 0;
    file->reserved = 0;
    file->dirent_index = 0;
    file->dirent_lba = 0;
    file->dirent_offset = 0;
}

static void fat32_copy_name(char *dst, uint32_t dst_size, const char *src) {
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

static int fat32_fill_file_from_dirent(const struct fat32_dirent *dirent,
                                       const struct fat32_lfn_state *lfn_state,
                                       struct fat32_file *out) {
    uint32_t cluster;

    if (dirent == 0 || out == 0 || fat32_is_end_of_dirent(dirent) ||
        fat32_is_deleted_dirent(dirent) || fat32_is_lfn_dirent(dirent) || fat32_is_volume_dirent(dirent)) {
        return 0;
    }

    fat32_file_reset(out);
    cluster = ((uint32_t)dirent->first_cluster_high << 16) | dirent->first_cluster_low;
    if (lfn_state != 0 && lfn_state->valid && lfn_state->next_order == 0u &&
        lfn_state->name[0] != '\0' && lfn_state->checksum == fat32_short_name_checksum(dirent->name)) {
        mem_copy(out->name, lfn_state->name, sizeof(out->name));
        out->lfn_entry_count = lfn_state->total_entries;
    } else {
        fat32_format_name83(dirent->name, out->name);
        out->lfn_entry_count = 0;
    }
    out->first_cluster = cluster;
    out->size = dirent->file_size;
    out->attributes = dirent->attr;
    return 1;
}

static int fat32_match_dirent_name(const struct fat32_dirent *dirent,
                                   const struct fat32_lfn_state *lfn_state,
                                   const char *input) {
    struct fat32_file file;

    if (input == 0 || !fat32_fill_file_from_dirent(dirent, lfn_state, &file)) {
        return 0;
    }
    if (fat32_ascii_casecmp(file.name, input) == 0) {
        return 1;
    }
    fat32_format_name83(dirent->name, file.name);
    return fat32_ascii_casecmp(file.name, input) == 0;
}

static void fat32_make_short_alias(const char *input, uint8_t out[11], uint32_t serial) {
    char base[9];
    char ext[4];
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    const char *last_dot = 0;
    const char *cursor = input;
    char serial_text[8];
    uint32_t serial_digits = 0;
    uint32_t serial_value = serial;

    for (uint32_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    while (cursor != 0 && *cursor != '\0') {
        if (*cursor == '.') {
            last_dot = cursor;
        }
        cursor++;
    }
    cursor = input;
    while (cursor != 0 && *cursor != '\0' && cursor != last_dot) {
        char ch = fat32_sanitize_short_char(*cursor++);

        if (ch != ' ' && ch != '.' && base_len + 1u < sizeof(base)) {
            base[base_len++] = ch;
        }
    }
    base[base_len] = '\0';
    if (last_dot != 0 && last_dot[1] != '\0') {
        cursor = last_dot + 1;
        while (*cursor != '\0' && ext_len + 1u < sizeof(ext)) {
            char ch = fat32_sanitize_short_char(*cursor++);

            if (ch != ' ' && ch != '.') {
                ext[ext_len++] = ch;
            }
        }
    }
    ext[ext_len] = '\0';

    if (serial == 0u && fat32_name_fits_short_exact(input)) {
        char normalized[12];

        fat32_normalize_name83(input, normalized);
        mem_copy(out, normalized, 11u);
        return;
    }

    if (base_len == 0u) {
        base[base_len++] = '_';
        base[base_len] = '\0';
    }
    do {
        serial_text[serial_digits++] = (char)('0' + (serial_value % 10u));
        serial_value /= 10u;
    } while (serial_value != 0u && serial_digits < sizeof(serial_text));

    {
        uint32_t prefix_len = 8u - (serial_digits + 1u);
        uint32_t pos = 0;

        if (prefix_len > base_len) {
            prefix_len = base_len;
        }
        while (pos < prefix_len) {
            out[pos] = (uint8_t)base[pos];
            pos++;
        }
        out[pos++] = '~';
        while (serial_digits > 0u && pos < 8u) {
            out[pos++] = (uint8_t)serial_text[--serial_digits];
        }
    }
    for (uint32_t i = 0; i < ext_len && i < 3u; i++) {
        out[8u + i] = (uint8_t)ext[i];
    }
}

static uint32_t fat32_dir_first_cluster(struct fat32_volume *vol, const struct fat32_file *dir) {
    if (vol == 0) {
        return 0;
    }
    if (dir == 0 || !fat32_cluster_is_data(vol, dir->first_cluster)) {
        return vol->root_cluster;
    }
    return dir->first_cluster;
}

static int fat32_list_directory(struct fat32_volume *vol,
                                const struct fat32_file *dir,
                                struct fat32_file *entries,
                                uint32_t max_entries,
                                uint32_t *entry_count) {
    uint32_t cluster;
    uint32_t out_count = 0;
    uint32_t raw_index = 0;
    struct fat32_lfn_state lfn_state;
    uint8_t dir_sector[512];

    if (vol == 0 || !vol->mounted || entries == 0 || entry_count == 0) {
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
                if (fat32_is_end_of_dirent(&dirents[i])) {
                    *entry_count = out_count;
                    return 0;
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
                if (fat32_is_volume_dirent(&dirents[i])) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (out_count < max_entries &&
                    fat32_fill_file_from_dirent(&dirents[i], &lfn_state, &entries[out_count])) {
                    entries[out_count].dirent_index = raw_index;
                    entries[out_count].dirent_lba = cluster_lba + sec;
                    entries[out_count].dirent_offset = i * sizeof(struct fat32_dirent);
                    out_count++;
                }
                fat32_lfn_state_reset(&lfn_state);
                raw_index++;
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }

    *entry_count = out_count;
    return 0;
}

static int fat32_find_in_directory(struct fat32_volume *vol,
                                   const struct fat32_file *dir,
                                   const char *name,
                                   struct fat32_file *out) {
    uint32_t cluster;
    uint32_t raw_index = 0;
    struct fat32_lfn_state lfn_state;
    uint8_t dir_sector[512];

    if (vol == 0 || !vol->mounted || name == 0 || out == 0) {
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
                if (fat32_is_volume_dirent(&dirents[i])) {
                    fat32_lfn_state_reset(&lfn_state);
                    raw_index++;
                    continue;
                }
                if (fat32_match_dirent_name(&dirents[i], &lfn_state, name)) {
                    fat32_fill_file_from_dirent(&dirents[i], &lfn_state, out);
                    out->dirent_index = raw_index;
                    out->dirent_lba = cluster_lba + sec;
                    out->dirent_offset = i * sizeof(struct fat32_dirent);
                    return 0;
                }
                fat32_lfn_state_reset(&lfn_state);
                raw_index++;
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

static uint32_t fat32_name_length(const char *text) {
    uint32_t len = 0;

    while (text != 0 && text[len] != '\0') {
        len++;
    }
    return len;
}

static int fat32_name_needs_lfn(const char *name) {
    uint32_t i = 0;

    if (!fat32_name_fits_short_exact(name)) {
        return 1;
    }
    while (name != 0 && name[i] != '\0') {
        if (name[i] >= 'a' && name[i] <= 'z') {
            return 1;
        }
        i++;
    }
    return 0;
}

static uint32_t fat32_lfn_entry_count_for_name(const char *name) {
    uint32_t len = fat32_name_length(name);

    return len == 0u ? 0u : (len + 12u) / 13u;
}

static int fat32_short_name_exists(struct fat32_volume *vol,
                                   const struct fat32_file *dir,
                                   const uint8_t raw_name[11]) {
    uint32_t cluster;

    if (vol == 0 || !vol->mounted || raw_name == 0) {
        return 0;
    }
    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);

        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            struct fat32_dirent *dirents;

            if (fat32_read_sector(vol, cluster_lba + sec, sector_buffer) != 0) {
                return 0;
            }
            dirents = (struct fat32_dirent *)sector_buffer;
            for (uint32_t i = 0; i < 16u; i++) {
                if (fat32_is_end_of_dirent(&dirents[i])) {
                    return 0;
                }
                if (fat32_is_deleted_dirent(&dirents[i]) || fat32_is_lfn_dirent(&dirents[i]) ||
                    fat32_is_volume_dirent(&dirents[i])) {
                    continue;
                }
                {
                    uint32_t match = 1u;

                    for (uint32_t j = 0; j < 11u; j++) {
                        if (dirents[i].name[j] != raw_name[j]) {
                            match = 0u;
                            break;
                        }
                    }
                    if (match) {
                        return 1;
                    }
                }
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return 0;
        }
    }
    return 0;
}

static int fat32_build_unique_short_name(struct fat32_volume *vol,
                                         const struct fat32_file *dir,
                                         const char *name,
                                         uint8_t raw_name[11]) {
    for (uint32_t serial = 0; serial < 1000000u; serial++) {
        fat32_make_short_alias(name, raw_name, serial);
        if (!fat32_short_name_exists(vol, dir, raw_name)) {
            return 0;
        }
    }
    return -1;
}

static int fat32_locate_free_dirent_span(struct fat32_volume *vol,
                                         const struct fat32_file *dir,
                                         uint32_t needed,
                                         uint32_t lbas_out[],
                                         uint32_t offsets_out[],
                                         uint32_t *raw_index_out) {
    uint32_t cluster;
    uint32_t raw_index = 0;
    uint32_t run = 0;

    if (vol == 0 || !vol->mounted || needed == 0u || lbas_out == 0 || offsets_out == 0 || raw_index_out == 0) {
        return -1;
    }
    if (dir != 0 && (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);

        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            struct fat32_dirent *dirents;

            if (fat32_read_sector(vol, cluster_lba + sec, sector_buffer) != 0) {
                return -1;
            }
            dirents = (struct fat32_dirent *)sector_buffer;
            for (uint32_t i = 0; i < 16u; i++, raw_index++) {
                if (fat32_is_end_of_dirent(&dirents[i]) || fat32_is_deleted_dirent(&dirents[i])) {
                    lbas_out[run] = cluster_lba + sec;
                    offsets_out[run] = i * sizeof(struct fat32_dirent);
                    run++;
                    if (run >= needed) {
                        *raw_index_out = raw_index - needed + 1u;
                        return 0;
                    }
                    continue;
                }
                run = 0;
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }

    return -1;
}

static int fat32_append_directory_cluster(struct fat32_volume *vol, const struct fat32_file *dir) {
    uint32_t cluster;
    uint32_t next = FAT32_CLUSTER_END;
    uint32_t new_cluster = 0;

    if (vol == 0 || !vol->mounted) {
        return -1;
    }
    if (dir != 0 && (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return -1;
    }

    cluster = fat32_dir_first_cluster(vol, dir);
    if (!fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }

    for (;;) {
        if (fat32_next_cluster(vol, cluster, &next) != 0) {
            return -1;
        }
        if (next >= FAT32_CLUSTER_END) {
            break;
        }
        if (!fat32_cluster_is_data(vol, next)) {
            return -1;
        }
        cluster = next;
    }

    if (fat32_alloc_cluster(vol, &new_cluster) != 0) {
        return -1;
    }
    if (fat32_write_fat_entry(vol, cluster, new_cluster) != 0) {
        (void)fat32_write_fat_entry(vol, new_cluster, 0u);
        return -1;
    }
    return 0;
}

static int fat32_append_file_cluster(struct fat32_volume *vol,
                                     uint32_t cluster,
                                     uint32_t *new_cluster_out) {
    uint32_t new_cluster = 0u;

    if (vol == 0 || !vol->mounted || new_cluster_out == 0) {
        return -1;
    }
    if (!fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }
    if (fat32_alloc_cluster(vol, &new_cluster) != 0) {
        return -1;
    }
    if (fat32_write_fat_entry(vol, cluster, new_cluster) != 0) {
        (void)fat32_write_fat_entry(vol, new_cluster, 0u);
        return -1;
    }
    *new_cluster_out = new_cluster;
    return 0;
}

static int fat32_write_raw_dirent(struct fat32_volume *vol,
                                  uint32_t lba,
                                  uint32_t offset,
                                  const void *dirent,
                                  uint32_t size) {
    if (vol == 0 || dirent == 0 || offset + size > sizeof(sector_buffer)) {
        return -1;
    }
    if (fat32_read_sector(vol, lba, sector_buffer) != 0) {
        return -1;
    }
    mem_copy(sector_buffer + offset, dirent, size);
    return fat32_write_sector(vol, lba, sector_buffer);
}

static void fat32_populate_lfn_dirent(struct fat32_lfn_dirent *lfn,
                                      const char *name,
                                      uint32_t total_entries,
                                      uint32_t order,
                                      uint8_t checksum) {
    uint32_t start;
    uint32_t len = fat32_name_length(name);

    mem_set(lfn, 0xff, sizeof(*lfn));
    lfn->order = (uint8_t)order;
    if (order == total_entries) {
        lfn->order |= 0x40u;
    }
    lfn->attr = FAT32_ATTR_LFN;
    lfn->type = 0u;
    lfn->checksum = checksum;
    lfn->first_cluster_low = 0u;

    start = (order - 1u) * 13u;
    for (uint32_t i = 0; i < 13u; i++) {
        uint32_t name_index = start + i;
        uint16_t value;

        if (name_index < len) {
            value = (uint16_t)(uint8_t)name[name_index];
        } else if (name_index == len) {
            value = 0x0000u;
        } else {
            value = 0xffffu;
        }
        if (i < 5u) {
            lfn->name1[i] = value;
        } else if (i < 11u) {
            lfn->name2[i - 5u] = value;
        } else {
            lfn->name3[i - 11u] = value;
        }
    }
}

static void fat32_set_dirent_cluster(struct fat32_dirent *dirent, uint32_t cluster) {
    dirent->first_cluster_high = (uint16_t)(cluster >> 16);
    dirent->first_cluster_low = (uint16_t)(cluster & 0xffffu);
}

static int fat32_create_entry_in_directory(struct fat32_volume *vol,
                                           const struct fat32_file *dir,
                                           const char *name,
                                           uint8_t attributes,
                                           uint32_t first_cluster,
                                           struct fat32_file *out) {
    uint8_t raw_short_name[11];
    struct fat32_dirent dirent;
    struct fat32_lfn_dirent lfn_dirent;
    uint32_t lbas[21];
    uint32_t offsets[21];
    uint32_t raw_index = 0;
    uint32_t lfn_count;
    uint32_t needed;
    uint8_t checksum;
    int span_rc;

    if (vol == 0 || !vol->mounted || name == 0 || out == 0) {
        return -1;
    }
    if (fat32_find_in_directory(vol, dir, name, out) == 0) {
        return -1;
    }
    if (fat32_build_unique_short_name(vol, dir, name, raw_short_name) != 0) {
        return -1;
    }
    lfn_count = fat32_name_needs_lfn(name) ? fat32_lfn_entry_count_for_name(name) : 0u;
    needed = lfn_count + 1u;
    if (lfn_count > 20u) {
        return -1;
    }
    span_rc = fat32_locate_free_dirent_span(vol, dir, needed, lbas, offsets, &raw_index);
    if (span_rc != 0) {
        if (fat32_append_directory_cluster(vol, dir) != 0 ||
            fat32_locate_free_dirent_span(vol, dir, needed, lbas, offsets, &raw_index) != 0) {
            return -1;
        }
    }
    mem_set(&dirent, 0, sizeof(dirent));
    mem_copy(dirent.name, raw_short_name, sizeof(dirent.name));
    fat32_set_dirent_cluster(&dirent, first_cluster);
    dirent.file_size = 0;
    dirent.attr = attributes;
    checksum = fat32_short_name_checksum(raw_short_name);

    for (uint32_t i = 0; i < lfn_count; i++) {
        uint32_t order = lfn_count - i;

        fat32_populate_lfn_dirent(&lfn_dirent, name, lfn_count, order, checksum);
        if (fat32_write_raw_dirent(vol, lbas[i], offsets[i], &lfn_dirent, sizeof(lfn_dirent)) != 0) {
            return -1;
        }
    }
    if (fat32_write_raw_dirent(vol, lbas[lfn_count], offsets[lfn_count], &dirent, sizeof(dirent)) != 0) {
        return -1;
    }
    fat32_file_reset(out);
    if (lfn_count != 0u) {
        fat32_copy_name(out->name, sizeof(out->name), name);
        out->lfn_entry_count = (uint8_t)lfn_count;
    } else {
        fat32_format_name83(raw_short_name, out->name);
    }
    out->first_cluster = first_cluster;
    out->size = 0;
    out->attributes = attributes;
    out->dirent_index = raw_index + lfn_count;
    out->dirent_lba = lbas[lfn_count];
    out->dirent_offset = offsets[lfn_count];
    return 0;
}

static int fat32_raw_entry_position(struct fat32_volume *vol,
                                    const struct fat32_file *dir,
                                    uint32_t raw_index,
                                    uint32_t *lba_out,
                                    uint32_t *offset_out) {
    uint32_t cluster;
    uint32_t current = 0;

    if (vol == 0 || !vol->mounted || lba_out == 0 || offset_out == 0) {
        return -1;
    }
    cluster = fat32_dir_first_cluster(vol, dir);
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t cluster_lba = fat32_cluster_lba(vol, cluster);

        for (uint32_t sec = 0; sec < vol->sectors_per_cluster; sec++) {
            for (uint32_t i = 0; i < 16u; i++, current++) {
                if (current == raw_index) {
                    *lba_out = cluster_lba + sec;
                    *offset_out = i * sizeof(struct fat32_dirent);
                    return 0;
                }
            }
        }
        if (fat32_next_cluster(vol, cluster, &cluster) != 0) {
            return -1;
        }
    }
    return -1;
}

static int fat32_delete_entry_chain(struct fat32_volume *vol,
                                    const struct fat32_file *dir,
                                    const struct fat32_file *file) {
    uint32_t total = (uint32_t)file->lfn_entry_count + 1u;

    for (uint32_t i = 0; i < total; i++) {
        uint32_t raw_index = file->dirent_index - i;
        uint32_t lba;
        uint32_t offset;
        struct fat32_dirent *dirent;

        if (fat32_raw_entry_position(vol, dir, raw_index, &lba, &offset) != 0 ||
            fat32_read_sector(vol, lba, sector_buffer) != 0) {
            return -1;
        }
        dirent = (struct fat32_dirent *)(sector_buffer + offset);
        dirent->name[0] = 0xe5;
        if (i == 0u) {
            dirent->file_size = 0u;
            fat32_set_dirent_cluster(dirent, 0u);
        }
        if (fat32_write_sector(vol, lba, sector_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

static int fat32_init_directory_cluster(struct fat32_volume *vol, uint32_t self_cluster, uint32_t parent_cluster) {
    struct fat32_dirent *dirents;

    if (vol == 0 || !vol->mounted || !fat32_cluster_is_data(vol, self_cluster)) {
        return -1;
    }
    if (fat32_read_sector(vol, fat32_cluster_lba(vol, self_cluster), sector_buffer) != 0) {
        return -1;
    }
    dirents = (struct fat32_dirent *)sector_buffer;
    mem_set(dirents, 0, sizeof(struct fat32_dirent) * 2u);

    mem_set(dirents[0].name, ' ', sizeof(dirents[0].name));
    dirents[0].name[0] = '.';
    dirents[0].attr = FAT32_ATTR_DIRECTORY;
    fat32_set_dirent_cluster(&dirents[0], self_cluster);

    mem_set(dirents[1].name, ' ', sizeof(dirents[1].name));
    dirents[1].name[0] = '.';
    dirents[1].name[1] = '.';
    dirents[1].attr = FAT32_ATTR_DIRECTORY;
    fat32_set_dirent_cluster(&dirents[1], fat32_cluster_is_data(vol, parent_cluster) ? parent_cluster : vol->root_cluster);

    return fat32_write_sector(vol, fat32_cluster_lba(vol, self_cluster), sector_buffer);
}

static int fat32_free_cluster_chain(struct fat32_volume *vol, uint32_t first_cluster) {
    uint32_t cluster = first_cluster;

    if (vol == 0 || !vol->mounted || !fat32_cluster_is_data(vol, cluster)) {
        return -1;
    }
    while (fat32_cluster_is_data(vol, cluster)) {
        uint32_t next = FAT32_CLUSTER_END;

        if (fat32_read_fat_entry(vol, cluster, &next) != 0) {
            return -1;
        }
        if (fat32_write_fat_entry(vol, cluster, 0) != 0) {
            return -1;
        }
        if (next >= FAT32_CLUSTER_END) {
            break;
        }
        if (!fat32_cluster_is_data(vol, next)) {
            return -1;
        }
        cluster = next;
    }
    return 0;
}

static int fat32_split_parent_child_path(const char *path,
                                         char *parent,
                                         uint32_t parent_size,
                                         char *name,
                                         uint32_t name_size) {
    uint32_t len = 0;
    uint32_t end;
    uint32_t slash = 0xffffffffu;
    uint32_t i;
    uint32_t parent_len;
    uint32_t name_len;

    if (path == 0 || parent == 0 || name == 0 || parent_size == 0 || name_size == 0) {
        return -1;
    }
    while (path[len] != '\0') {
        len++;
    }
    while (len != 0u && path[len - 1u] == '/') {
        len--;
    }
    if (len == 0u) {
        return -1;
    }
    end = len;
    for (i = 0; i < end; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }

    parent[0] = '\0';
    name[0] = '\0';
    if (slash == 0xffffffffu) {
        name_len = end;
        if (name_len + 1u > name_size) {
            return -1;
        }
        mem_copy(name, path, name_len);
        name[name_len] = '\0';
        return 0;
    }

    parent_len = slash;
    name_len = end - (slash + 1u);
    if (name_len == 0u || parent_len + 1u > parent_size || name_len + 1u > name_size) {
        return -1;
    }
    if (parent_len == 0u) {
        parent[0] = '\0';
    } else {
        mem_copy(parent, path, parent_len);
        parent[parent_len] = '\0';
    }
    mem_copy(name, path + slash + 1u, name_len);
    name[name_len] = '\0';
    return 0;
}

static int fat32_is_directory_empty(struct fat32_volume *vol, const struct fat32_file *dir) {
    struct fat32_file entry;

    if (vol == 0 || dir == 0 || (dir->attributes & FAT32_ATTR_DIRECTORY) == 0) {
        return 0;
    }
    for (uint32_t index = 0; fat32_get_dir_entry(vol, dir, index, &entry) == 0; index++) {
        if (entry.name[0] == '.' &&
            (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) {
            continue;
        }
        return 0;
    }
    return 1;
}

static int fat32_path_next_segment(const char **path_io, char segment[NOS_NAME_BUFFER_SIZE]) {
    const char *path = *path_io;
    uint32_t pos = 0;

    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        return 0;
    }
    while (*path != '\0' && *path != '/') {
        if (pos + 1u >= NOS_NAME_BUFFER_SIZE) {
            return -1;
        }
        segment[pos++] = *path++;
    }
    segment[pos] = '\0';
    while (*path == '/') {
        path++;
    }
    *path_io = path;
    return 1;
}

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
    if (blockdev_read(bdev, partition_lba, 1, sector_buffer) != 0) {
        fat32_log_probe_failure(partition_lba, "read", 0);
        return -1;
    }

    bpb = (struct fat32_bpb *)sector_buffer;
    if (sector_buffer[510] != 0x55u || sector_buffer[511] != 0xaau) {
        fat32_log_probe_failure(partition_lba, "signature", bpb);
        return -1;
    }
    if (bpb->jump[0] != 0xebu && bpb->jump[0] != 0xe9u) {
        fat32_log_probe_failure(partition_lba, "jump", bpb);
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
        fat32_log_probe_failure(partition_lba, "bpb", bpb);
        return -1;
    }

    total_sectors = bpb->total_sectors_16 != 0 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    fat_region_sectors = (uint64_t)bpb->reserved_sector_count +
                         (uint64_t)bpb->table_count * (uint64_t)bpb->table_size_32;
    if (total_sectors == 0u ||
        total_sectors > bdev->block_count - (uint64_t)partition_lba ||
        fat_region_sectors >= (uint64_t)total_sectors ||
        (uint64_t)partition_lba + (uint64_t)total_sectors > 0x100000000ull) {
        fat32_log_probe_failure(partition_lba, "geometry", bpb);
        return -1;
    }
    data_sectors = total_sectors - (uint32_t)fat_region_sectors;
    cluster_count = data_sectors / bpb->sectors_per_cluster;
    if (cluster_count == 0u ||
        (uint64_t)bpb->table_size_32 * 128u < (uint64_t)cluster_count + 2u) {
        fat32_log_probe_failure(partition_lba, "clusters", bpb);
        return -1;
    }
    vol->cluster_count = cluster_count;
    if (!fat32_cluster_is_data(vol, bpb->root_cluster)) {
        fat32_log_probe_failure(partition_lba, "root_cluster", bpb);
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
            if (fat32_read_sector(vol, cluster_lba + sec, sector_buffer) != 0) {
                return -1;
            }
            mem_copy(out + written, sector_buffer, copy_size);
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

            if (fat32_read_sector(vol, cluster_lba + sec, sector_buffer) != 0) {
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
                     sector_buffer + (sector_copy_start - sector_start),
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

static int fat32_ensure_file_first_cluster(struct fat32_volume *vol, struct fat32_file *file) {
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

            if (fat32_read_sector(vol, cluster_lba + sec, sector_buffer) != 0) {
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
            mem_copy(sector_buffer + (sector_copy_start - sector_start),
                     in + written,
                     copy_size);
            if (fat32_write_sector(vol, cluster_lba + sec, sector_buffer) != 0) {
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
