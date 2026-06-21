#include "bootx.h"

struct fat_bpb {
    union {
        struct {
            uint8_t jump[3];
            char oem[8];
            uint16_t bytes_per_sector;
            uint8_t sectors_per_cluster;
            uint16_t reserved_sectors;
            uint8_t fat_count;
            uint16_t root_entry_count;
            uint16_t total_sectors_16;
            uint8_t media_descriptor;
            uint16_t sectors_per_fat_16;
            uint16_t sectors_per_track;
            uint16_t head_count;
            uint32_t hidden_sectors;
            uint32_t total_sectors_32;

            uint32_t sectors_per_fat_32;
            uint16_t ext_flags;
            uint16_t fs_version;
            uint32_t root_cluster;
            uint16_t fs_info;
            uint16_t backup_boot_sector;
            uint8_t reserved0[12];
            uint8_t drive_number;
            uint8_t nt_flags;
            uint8_t signature;
            uint32_t volume_id;
            char volume_label[11];
            char fs_type[8];
        } __attribute__((packed));
        uint8_t raw[512];
    };
} __attribute__((packed));

static uint32_t cluster_to_lba(const struct fat16_context *ctx, uint32_t cluster) {
    return ctx->data_lba + (cluster - 2u) * ctx->sectors_per_cluster;
}

static int read_fat_sector(const struct fat16_context *ctx, uint32_t lba, uint8_t sector[512]) {
    static uint8_t fat_sector[512];
    static uint8_t cached_drive;
    static uint32_t cached_lba;
    static int cached;

    if (cached && cached_drive == ctx->drive && cached_lba == lba) {
        memcpy(sector, fat_sector, 512);
        return 0;
    }
    if (bios_read_sectors(ctx->drive, lba, 1, fat_sector) != 0) {
        cached = 0;
        return -1;
    }
    cached_drive = ctx->drive;
    cached_lba = lba;
    cached = 1;
    memcpy(sector, fat_sector, 512);
    return 0;
}

static uint32_t fat16_next_cluster(const struct fat16_context *ctx, uint32_t cluster) {
    static uint8_t fat_sector[512];
    uint32_t fat_offset = cluster * 2u;
    uint32_t fat_sector_lba = ctx->fat_lba + (fat_offset / 512u);
    uint32_t entry_offset = fat_offset % 512u;

    if (read_fat_sector(ctx, fat_sector_lba, fat_sector) != 0) {
        return 0xFFFFFFFFu;
    }
    return *(uint16_t *)(fat_sector + entry_offset);
}

static uint32_t fat32_next_cluster(const struct fat16_context *ctx, uint32_t cluster) {
    static uint8_t fat_sector[512];
    uint32_t fat_offset = cluster * 4u;
    uint32_t fat_sector_lba = ctx->fat_lba + (fat_offset / 512u);
    uint32_t entry_offset = fat_offset % 512u;
    uint32_t next;

    if (read_fat_sector(ctx, fat_sector_lba, fat_sector) != 0) {
        return 0xFFFFFFFFu;
    }
    next = (*(uint32_t *)(fat_sector + entry_offset)) & 0x0FFFFFFFu;
    return next;
}

static uint32_t fat_next_cluster(const struct fat16_context *ctx, uint32_t cluster) {
    if (ctx->fat_type == 32) {
        return fat32_next_cluster(ctx, cluster);
    }
    return fat16_next_cluster(ctx, cluster);
}

static int fat_is_end_of_chain(const struct fat16_context *ctx, uint32_t cluster) {
    if (ctx->fat_type == 32) {
        return cluster >= 0x0FFFFFF8u;
    }
    return cluster >= 0xFFF8u;
}

static int scan_dir_sector(const uint8_t *sector, const char *name_83, struct fat16_file *out) {
    const struct fat16_dirent *entries = (const struct fat16_dirent *)sector;
    uint32_t entries_per_sector = 512 / sizeof(struct fat16_dirent);

    for (uint32_t i = 0; i < entries_per_sector; i++) {
        if ((uint8_t)entries[i].name[0] == 0x00) {
            return 1;
        }
        if ((uint8_t)entries[i].name[0] == 0xE5 || (entries[i].attr & 0x0F) == 0x0F) {
            continue;
        }
        if (memcmp(entries[i].name, name_83, 11) == 0) {
            out->first_cluster = ((uint32_t)entries[i].cluster_high << 16) | entries[i].cluster_low;
            out->size = entries[i].file_size;
            out->attr = entries[i].attr;
            return 0;
        }
    }

    return -1;
}

static void path_component_to_name_83(char out[12], const char *src, size_t len) {
    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    out[11] = '\0';

    int pos = 0;
    size_t i = 0;
    while (i < len && src[i] != '.' && pos < 8) {
        char ch = src[i++];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - ('a' - 'A'));
        }
        out[pos++] = ch;
    }
    if (i < len && src[i] == '.') {
        i++;
        pos = 8;
        while (i < len && pos < 11) {
            char ch = src[i++];
            if (ch >= 'a' && ch <= 'z') {
                ch = (char)(ch - ('a' - 'A'));
            }
            out[pos++] = ch;
        }
    }
}

static int fat16_open_in_directory(struct fat16_context *ctx, uint32_t dir_cluster, int is_root,
                                   const char *name_83, struct fat16_file *out) {
    static uint8_t sector[512];

    if (is_root && ctx->fat_type == 16) {
        for (uint32_t sec = 0; sec < ctx->root_sectors; sec++) {
            int res;
            if (bios_read_sectors(ctx->drive, ctx->root_lba + sec, 1, sector) != 0) {
                return -1;
            }
            res = scan_dir_sector(sector, name_83, out);
            if (res == 0) {
                return 0;
            }
            if (res == 1) {
                return -1;
            }
        }
        return -1;
    }

    if (is_root) {
        dir_cluster = ctx->root_cluster;
    }

    while (dir_cluster >= 2 && !fat_is_end_of_chain(ctx, dir_cluster)) {
        uint32_t cluster_lba = cluster_to_lba(ctx, dir_cluster);

        for (uint8_t sec = 0; sec < ctx->sectors_per_cluster; sec++) {
            int res;
            if (bios_read_sectors(ctx->drive, cluster_lba + sec, 1, sector) != 0) {
                return -1;
            }
            res = scan_dir_sector(sector, name_83, out);
            if (res == 0) {
                return 0;
            }
            if (res == 1) {
                return -1;
            }
        }

        dir_cluster = fat_next_cluster(ctx, dir_cluster);
        if (dir_cluster == 0xFFFFFFFFu) {
            return -1;
        }
    }

    return -1;
}

int fat16_mount(struct fat16_context *ctx, uint8_t drive, uint32_t partition_lba) {
    struct fat_bpb bpb;
    uint32_t sectors_per_fat;

    if (bios_read_sectors(drive, partition_lba, 1, &bpb) != 0) {
        debug_puts("fat: read bpb failed\n");
        return -1;
    }

    debug_puts("fat: bps=");
    debug_put_hex(bpb.bytes_per_sector);
    debug_puts(" spc=");
    debug_put_hex(bpb.sectors_per_cluster);
    debug_puts(" spf16=");
    debug_put_hex(bpb.sectors_per_fat_16);
    debug_puts(" spf32=");
    debug_put_hex(bpb.sectors_per_fat_32);
    debug_puts(" fs=");
    for (int i = 0; i < 8; i++) {
        char ch = bpb.fs_type[i];
        if (ch == '\0') {
            ch = '.';
        }
        __asm__ __volatile__("outb %0, $0xE9" : : "a"(ch));
    }
    debug_puts("\n");

    if (bpb.bytes_per_sector != 512 || bpb.sectors_per_cluster == 0 || bpb.fat_count == 0) {
        return -1;
    }

    sectors_per_fat = bpb.sectors_per_fat_16 ? bpb.sectors_per_fat_16 : bpb.sectors_per_fat_32;
    if (sectors_per_fat == 0) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->drive = drive;
    ctx->partition_lba = partition_lba;
    ctx->bytes_per_sector = bpb.bytes_per_sector;
    ctx->sectors_per_cluster = bpb.sectors_per_cluster;
    ctx->reserved_sectors = bpb.reserved_sectors;
    ctx->fat_count = bpb.fat_count;
    ctx->root_entry_count = bpb.root_entry_count;
    ctx->sectors_per_fat = sectors_per_fat;
    ctx->fat_lba = partition_lba + bpb.reserved_sectors;

    if (memcmp(bpb.fs_type, "FAT32   ", 8) == 0 || (bpb.sectors_per_fat_16 == 0 && bpb.root_entry_count == 0)) {
        ctx->fat_type = 32;
        ctx->root_cluster = bpb.root_cluster ? bpb.root_cluster : 2u;
        ctx->root_sectors = 0;
        ctx->root_lba = 0;
        ctx->data_lba = ctx->fat_lba + (uint32_t)bpb.fat_count * sectors_per_fat;
        return 0;
    }

    if (memcmp(bpb.fs_type, "FAT16   ", 8) != 0) {
        return -1;
    }

    ctx->fat_type = 16;
    ctx->root_sectors = ((uint32_t)bpb.root_entry_count * 32u + 511u) / 512u;
    ctx->root_lba = ctx->fat_lba + (uint32_t)bpb.fat_count * sectors_per_fat;
    ctx->data_lba = ctx->root_lba + ctx->root_sectors;
    return 0;
}

int fat16_open_root(struct fat16_context *ctx, const char *name_83, struct fat16_file *out) {
    return fat16_open_in_directory(ctx, 0, 1, name_83, out);
}

int fat16_open_path(struct fat16_context *ctx, const char *path, struct fat16_file *out) {
    struct fat16_file current;
    uint32_t dir_cluster = 0;
    int is_root = 1;

    while (*path == '/' || *path == '\\') {
        path++;
    }
    if (*path == '\0') {
        return -1;
    }

    for (;;) {
        const char *end = path;
        char name_83[12];

        while (*end != '\0' && *end != '/' && *end != '\\') {
            end++;
        }
        if (end == path) {
            return -1;
        }

        path_component_to_name_83(name_83, path, (size_t)(end - path));
        if (fat16_open_in_directory(ctx, dir_cluster, is_root, name_83, &current) != 0) {
            return -1;
        }

        while (*end == '/' || *end == '\\') {
            end++;
        }
        if (*end == '\0') {
            *out = current;
            return 0;
        }
        if ((current.attr & 0x10u) == 0) {
            return -1;
        }

        dir_cluster = current.first_cluster;
        is_root = 0;
        path = end;
    }
}

int fat16_read_file(struct fat16_context *ctx, const struct fat16_file *file, void *buffer, uint32_t buffer_size) {
    uint8_t *dest = buffer;
    uint32_t cluster = file->first_cluster;
    uint32_t remaining = file->size;
    static uint8_t partial_sector[512];

    if (file->size > buffer_size) {
        return -1;
    }

    while (cluster >= 2 && !fat_is_end_of_chain(ctx, cluster) && remaining > 0) {
        uint32_t run_start = cluster;
        uint32_t run_clusters = 1;
        uint32_t run_sectors;
        uint32_t bytes_in_run;
        uint32_t full_sectors;
        uint32_t partial_bytes;
        uint32_t next = fat_next_cluster(ctx, cluster);

        while (!fat_is_end_of_chain(ctx, next) && next == cluster + 1u &&
               (run_clusters + 1u) * (uint32_t)ctx->sectors_per_cluster <= 255u &&
               remaining > run_clusters * (uint32_t)ctx->sectors_per_cluster * 512u) {
            cluster = next;
            run_clusters++;
            next = fat_next_cluster(ctx, cluster);
        }
        if (next == 0xFFFFFFFFu) {
            return -1;
        }

        run_sectors = run_clusters * (uint32_t)ctx->sectors_per_cluster;
        bytes_in_run = run_sectors * 512u;
        if (bytes_in_run > remaining) {
            bytes_in_run = remaining;
        }
        full_sectors = bytes_in_run / 512u;
        partial_bytes = bytes_in_run % 512u;

        if (full_sectors != 0u) {
            uint32_t lba = cluster_to_lba(ctx, run_start);
            uint32_t sectors_left = full_sectors;

            while (sectors_left != 0u) {
                uint8_t chunk = sectors_left > 255u ? 255u : (uint8_t)sectors_left;

                if (bios_read_sectors(ctx->drive, lba, chunk, dest) != 0) {
                    return -1;
                }
                dest += (uint32_t)chunk * 512u;
                lba += chunk;
                sectors_left -= chunk;
            }
            remaining -= full_sectors * 512u;
        }
        if (partial_bytes != 0u) {
            uint32_t lba = cluster_to_lba(ctx, run_start) + full_sectors;

            if (bios_read_sectors(ctx->drive, lba, 1, partial_sector) != 0) {
                return -1;
            }
            memcpy(dest, partial_sector, partial_bytes);
            dest += partial_bytes;
            remaining -= partial_bytes;
        }

        cluster = next;
    }

    return (remaining == 0) ? 0 : -1;
}
