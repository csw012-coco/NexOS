#include "user/apps/elf/nexbox/applets/fs/cmdsuite_storage_common.h"
#include "kernel/public/fs/nxfs_types.h"

enum {
    STORAGE_TOOL_BLOCK_SIZE = 512u,
    STORAGE_TOOL_NXFS_MAGIC = 0x4e584653u,
    STORAGE_TOOL_NXFS_MAX_INODES = 1024u,
    STORAGE_TOOL_NXFS_TYPE_DIR = 2u,
    STORAGE_TOOL_FAT32_RESERVED_SECTORS = 32u,
    STORAGE_TOOL_FAT32_TABLES = 2u,
    STORAGE_TOOL_FAT32_ROOT_CLUSTER = 2u
};

struct storage_tool_nxfs_super {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t bitmap_start;
    uint32_t inode_start;
    uint32_t data_start;
    uint8_t uuid[16];
} __attribute__((packed));

struct storage_tool_nxfs_dir_entry {
    uint32_t inode;
    char name[28];
} __attribute__((packed));

struct storage_block_target {
    uint32_t valid;
    uint32_t disk;
    uint32_t part_index;
    uint32_t has_part;
    uint64_t start_lba;
    uint64_t blocks;
    uint32_t block_size;
    uint32_t writable;
};

static int storage_parse_u64_local(const char *text, uint64_t *out) {
    uint64_t value = 0u;
    uint32_t i = 0u;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    while (text[i] != '\0') {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
        i++;
    }
    *out = value;
    return 1;
}

static int storage_parse_size_local(const char *text, uint64_t *out) {
    uint64_t value = 0u;
    uint32_t i = 0u;
    uint64_t scale = 1u;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10u + (uint64_t)(text[i] - '0');
        i++;
    }
    if (i == 0u) {
        return 0;
    }
    if (text[i] != '\0') {
        char unit = text[i];

        if (text[i + 1u] != '\0') {
            return 0;
        }
        if (unit == 'k' || unit == 'K') {
            scale = 1024u;
        } else if (unit == 'm' || unit == 'M') {
            scale = 1024u * 1024u;
        } else if (unit == 'g' || unit == 'G') {
            scale = 1024u * 1024u * 1024u;
        } else {
            return 0;
        }
    }
    *out = value * scale;
    return 1;
}

static int storage_starts_with_local(const char *text, const char *prefix) {
    uint32_t i = 0u;

    if (text == NULL || prefix == NULL) {
        return 0;
    }
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void storage_zero_block_local(uint8_t *data) {
    for (uint32_t i = 0; i < STORAGE_TOOL_BLOCK_SIZE; i++) {
        data[i] = 0;
    }
}

static int storage_read_block_local(uint32_t disk, uint64_t lba, uint8_t *data) {
    struct syscall_block_read_info info;

    if (data == NULL || block_read(disk, lba, &info) <= 0 ||
        info.bytes_read != STORAGE_TOOL_BLOCK_SIZE) {
        return 0;
    }
    for (uint32_t i = 0; i < STORAGE_TOOL_BLOCK_SIZE; i++) {
        data[i] = info.data[i];
    }
    return 1;
}

static int storage_write_block_local(uint32_t disk, uint64_t lba, const uint8_t *data) {
    struct syscall_block_write_info info;

    if (data == NULL) {
        return 0;
    }
    info.disk_index = disk;
    info.block_size = STORAGE_TOOL_BLOCK_SIZE;
    info.bytes_to_write = STORAGE_TOOL_BLOCK_SIZE;
    info.bytes_written = 0;
    info.lba = lba;
    for (uint32_t i = 0; i < STORAGE_TOOL_BLOCK_SIZE; i++) {
        info.data[i] = data[i];
    }
    return block_write(disk, lba, &info) > 0 && info.bytes_written == STORAGE_TOOL_BLOCK_SIZE;
}

static int storage_parse_dev_path_local(const char *path,
                                        uint32_t *disk_out,
                                        uint32_t *part_number_out,
                                        int *has_part_out) {
    uint32_t i = 9u;
    uint32_t disk = 0u;
    uint32_t part = 0u;

    if (!storage_starts_with_local(path, "/dev/disk") ||
        path[i] < '0' || path[i] > '9' ||
        disk_out == NULL || part_number_out == NULL || has_part_out == NULL) {
        return 0;
    }
    while (path[i] >= '0' && path[i] <= '9') {
        disk = disk * 10u + (uint32_t)(path[i] - '0');
        i++;
    }
    if (path[i] == '\0') {
        *disk_out = disk;
        *part_number_out = 0u;
        *has_part_out = 0;
        return 1;
    }
    if (path[i] != 'p' || path[i + 1u] < '0' || path[i + 1u] > '9') {
        return 0;
    }
    i++;
    while (path[i] >= '0' && path[i] <= '9') {
        part = part * 10u + (uint32_t)(path[i] - '0');
        i++;
    }
    if (path[i] != '\0' || part == 0u) {
        return 0;
    }
    *disk_out = disk;
    *part_number_out = part;
    *has_part_out = 1;
    return 1;
}

static int storage_resolve_block_target_local(const char *path, struct storage_block_target *target) {
    struct syscall_block_info disk_info;
    struct syscall_partition_info part_info;
    uint32_t disk;
    uint32_t part_number;
    int has_part;

    if (target == NULL || !storage_parse_dev_path_local(path, &disk, &part_number, &has_part)) {
        return 0;
    }
    if (block_query(disk, &disk_info) <= 0 || disk_info.block_size != STORAGE_TOOL_BLOCK_SIZE) {
        return 0;
    }
    target->valid = 1u;
    target->disk = disk;
    target->part_index = 0xffffffffu;
    target->has_part = has_part ? 1u : 0u;
    target->start_lba = 0u;
    target->blocks = disk_info.block_count;
    target->block_size = disk_info.block_size;
    target->writable = disk_info.writable;
    if (has_part) {
        if (part_query(disk, part_number - 1u, &part_info) <= 0) {
            return 0;
        }
        target->part_index = part_info.part_index;
        target->start_lba = part_info.start_lba;
        target->blocks = part_info.sector_count;
    }
    return 1;
}

static int dd_read_local(const char *path,
                         const struct storage_block_target *target,
                         int fd,
                         uint64_t block_index,
                         uint8_t *data,
                         uint32_t bs,
                         uint32_t *got_out) {
    if (target != NULL && target->valid) {
        uint64_t byte_offset = block_index * (uint64_t)bs;
        uint64_t lba = byte_offset / STORAGE_TOOL_BLOCK_SIZE;
        uint32_t offset = (uint32_t)(byte_offset % STORAGE_TOOL_BLOCK_SIZE);
        uint32_t copied = 0u;
        uint8_t sector[STORAGE_TOOL_BLOCK_SIZE];

        if (offset != 0u || bs > STORAGE_TOOL_BLOCK_SIZE) {
            write_err_str("dd: block device io must be 512-byte aligned\n");
            return -1;
        }
        while (copied < bs) {
            if (lba >= target->blocks ||
                !storage_read_block_local(target->disk, target->start_lba + lba, sector)) {
                return copied == 0u ? 0 : -1;
            }
            for (uint32_t i = 0; i < STORAGE_TOOL_BLOCK_SIZE; i++) {
                data[copied + i] = sector[i];
            }
            copied += STORAGE_TOOL_BLOCK_SIZE;
            lba++;
        }
        *got_out = copied;
        return 1;
    }
    (void)path;
    {
        ssize_t got = read(fd, data, bs);

        if (got < 0) {
            return -1;
        }
        *got_out = (uint32_t)got;
        return got == 0 ? 0 : 1;
    }
}

static int dd_write_local(const char *path,
                          const struct storage_block_target *target,
                          int fd,
                          uint64_t block_index,
                          const uint8_t *data,
                          uint32_t bytes,
                          uint32_t bs) {
    if (target != NULL && target->valid) {
        uint64_t byte_offset = block_index * (uint64_t)bs;
        uint64_t lba = byte_offset / STORAGE_TOOL_BLOCK_SIZE;
        uint32_t offset = (uint32_t)(byte_offset % STORAGE_TOOL_BLOCK_SIZE);
        uint32_t copied = 0u;
        uint8_t sector[STORAGE_TOOL_BLOCK_SIZE];

        if (offset != 0u || bytes % STORAGE_TOOL_BLOCK_SIZE != 0u) {
            write_err_str("dd: block device writes must be 512-byte aligned\n");
            return 0;
        }
        while (copied < bytes) {
            if (lba >= target->blocks) {
                write_err_str("dd: write past end of block target\n");
                return 0;
            }
            for (uint32_t i = 0; i < STORAGE_TOOL_BLOCK_SIZE; i++) {
                sector[i] = data[copied + i];
            }
            if (!storage_write_block_local(target->disk, target->start_lba + lba, sector)) {
                return 0;
            }
            copied += STORAGE_TOOL_BLOCK_SIZE;
            lba++;
        }
        return 1;
    }
    (void)path;
    return write(fd, data, bytes) == (ssize_t)bytes;
}

int cmd_dd(int argc, char **argv) {
    const char *if_path = NULL;
    const char *of_path = NULL;
    uint64_t bs64 = STORAGE_TOOL_BLOCK_SIZE;
    uint64_t count = 0u;
    uint64_t skip = 0u;
    uint64_t seek = 0u;
    uint64_t copied_blocks = 0u;
    uint64_t copied_bytes = 0u;
    uint8_t buffer[STORAGE_TOOL_BLOCK_SIZE];
    struct storage_block_target in_target;
    struct storage_block_target out_target;
    int in_fd = -1;
    int out_fd = -1;

    in_target.valid = 0u;
    out_target.valid = 0u;
    for (int i = 1; i < argc; i++) {
        if (storage_starts_with_local(argv[i], "if=")) {
            if_path = argv[i] + 3;
        } else if (storage_starts_with_local(argv[i], "of=")) {
            of_path = argv[i] + 3;
        } else if (storage_starts_with_local(argv[i], "bs=")) {
            if (!storage_parse_size_local(argv[i] + 3, &bs64)) {
                write_err_str("dd: invalid bs\n");
                return 1;
            }
        } else if (storage_starts_with_local(argv[i], "count=")) {
            if (!storage_parse_u64_local(argv[i] + 6, &count)) {
                write_err_str("dd: invalid count\n");
                return 1;
            }
        } else if (storage_starts_with_local(argv[i], "skip=")) {
            if (!storage_parse_u64_local(argv[i] + 5, &skip)) {
                write_err_str("dd: invalid skip\n");
                return 1;
            }
        } else if (storage_starts_with_local(argv[i], "seek=")) {
            if (!storage_parse_u64_local(argv[i] + 5, &seek)) {
                write_err_str("dd: invalid seek\n");
                return 1;
            }
        } else {
            write_err_usage("dd", " if=<src> of=<dst> [bs=512] [count=n] [skip=n] [seek=n]\n");
            return 1;
        }
    }
    if (if_path == NULL || of_path == NULL || bs64 == 0u || bs64 > sizeof(buffer)) {
        write_err_usage("dd", " if=<src> of=<dst> [bs=512] [count=n] [skip=n] [seek=n]\n");
        return 1;
    }
    if (storage_resolve_block_target_local(if_path, &in_target)) {
        if (bs64 % STORAGE_TOOL_BLOCK_SIZE != 0u) {
            write_err_str("dd: block input requires bs multiple of 512\n");
            return 1;
        }
    } else {
        in_fd = open(if_path, O_RDONLY);
        if (in_fd < 0) {
            write_err_str("dd: input open failed\n");
            return 1;
        }
        if (skip != 0u && lseek(in_fd, (long)(skip * bs64), 0) < 0) {
            write_err_str("dd: input seek failed\n");
            close(in_fd);
            return 1;
        }
    }
    if (storage_resolve_block_target_local(of_path, &out_target)) {
        if (!out_target.writable) {
            write_err_str("dd: output block device is read-only\n");
            if (in_fd >= 0) {
                close(in_fd);
            }
            return 1;
        }
        if (bs64 % STORAGE_TOOL_BLOCK_SIZE != 0u) {
            write_err_str("dd: block output requires bs multiple of 512\n");
            if (in_fd >= 0) {
                close(in_fd);
            }
            return 1;
        }
    } else {
        out_fd = open(of_path, O_CREAT | O_WRONLY | (seek == 0u ? O_TRUNC : 0));
        if (out_fd < 0) {
            write_err_str("dd: output open failed\n");
            if (in_fd >= 0) {
                close(in_fd);
            }
            return 1;
        }
        if (seek != 0u && lseek(out_fd, (long)(seek * bs64), 0) < 0) {
            write_err_str("dd: output seek failed\n");
            close(out_fd);
            if (in_fd >= 0) {
                close(in_fd);
            }
            return 1;
        }
    }
    for (;;) {
        uint32_t got = 0u;
        int rc;

        if (count != 0u && copied_blocks >= count) {
            break;
        }
        storage_zero_block_local(buffer);
        rc = dd_read_local(if_path, in_target.valid ? &in_target : NULL, in_fd, skip + copied_blocks,
                           buffer, (uint32_t)bs64, &got);
        if (rc < 0) {
            write_err_str("dd: read failed\n");
            break;
        }
        if (rc == 0 || got == 0u) {
            break;
        }
        if (!dd_write_local(of_path, out_target.valid ? &out_target : NULL, out_fd, seek + copied_blocks,
                            buffer, got, (uint32_t)bs64)) {
            write_err_str("dd: write failed\n");
            break;
        }
        copied_blocks++;
        copied_bytes += got;
        if (got < bs64) {
            break;
        }
    }
    if (in_fd >= 0) {
        close(in_fd);
    }
    if (out_fd >= 0) {
        close(out_fd);
    }
    storage_write_u64_dec(copied_bytes);
    write_str(" bytes copied\n");
    return 0;
}

static void mkfs_put_u32_local(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
    dst[2] = (uint8_t)((value >> 16) & 0xffu);
    dst[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void mkfs_put_u16_local(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void mkfs_bitmap_set_local(uint8_t *bitmap, uint32_t block) {
    bitmap[block / 8u] |= (uint8_t)(1u << (block % 8u));
}

static void mkfs_make_uuid_local(uint8_t uuid[16], const struct storage_block_target *target) {
    uint32_t state = 0x9e3779b9u ^ target->disk ^ (uint32_t)target->start_lba ^ (uint32_t)target->blocks;

    for (uint32_t i = 0; i < 16u; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        uuid[i] = (uint8_t)(state >> 24);
    }
    uuid[6] = (uint8_t)((uuid[6] & 0x0fu) | 0x40u);
    uuid[8] = (uint8_t)((uuid[8] & 0x3fu) | 0x80u);
}

static int mkfs_write_struct_block_local(const struct storage_block_target *target, uint64_t rel_lba, const void *src, uint32_t size) {
    uint8_t block[STORAGE_TOOL_BLOCK_SIZE];
    const uint8_t *bytes = (const uint8_t *)src;

    storage_zero_block_local(block);
    for (uint32_t i = 0; i < size && i < STORAGE_TOOL_BLOCK_SIZE; i++) {
        block[i] = bytes[i];
    }
    return storage_write_block_local(target->disk, target->start_lba + rel_lba, block);
}

static int cmd_mkfs_nxfs_local(const struct storage_block_target *target) {
    struct storage_tool_nxfs_super super;
    struct nxfs_inode root;
    uint8_t block[STORAGE_TOOL_BLOCK_SIZE];
    uint32_t bitmap_blocks;
    uint32_t inode_bytes;
    uint32_t inode_blocks;
    uint32_t total_blocks;

    if (target == NULL || !target->valid || !target->writable || target->blocks > 0xffffffffu) {
        write_err_str("mkfs: invalid or read-only target\n");
        return 1;
    }
    total_blocks = (uint32_t)target->blocks;
    inode_bytes = STORAGE_TOOL_NXFS_MAX_INODES * (uint32_t)sizeof(root);
    inode_blocks = (inode_bytes + STORAGE_TOOL_BLOCK_SIZE - 1u) / STORAGE_TOOL_BLOCK_SIZE;
    bitmap_blocks = (total_blocks + (STORAGE_TOOL_BLOCK_SIZE * 8u) - 1u) / (STORAGE_TOOL_BLOCK_SIZE * 8u);
    if (1u + bitmap_blocks + inode_blocks >= total_blocks) {
        write_err_str("mkfs: target too small for nxfs metadata\n");
        return 1;
    }

    /*
     * Full-disk zeroing is unnecessarily expensive for large partitions: mkfs
     * only needs to initialize filesystem metadata and the bitmap blocks it
     * will update. Clearing every data block makes formatting O(total_blocks)
     * writes even though the filesystem marks the rest as free.
     */
    super.magic = STORAGE_TOOL_NXFS_MAGIC;
    super.total_blocks = total_blocks;
    super.bitmap_start = 1u;
    super.inode_start = super.bitmap_start + bitmap_blocks;
    super.data_start = super.inode_start + inode_blocks;
    mkfs_make_uuid_local(super.uuid, target);
    if (!mkfs_write_struct_block_local(target, 0u, &super, sizeof(super))) {
        write_err_str("mkfs: super write failed\n");
        return 1;
    }

    for (uint32_t b = 0; b < bitmap_blocks; b++) {
        uint32_t first_bit = b * STORAGE_TOOL_BLOCK_SIZE * 8u;
        uint32_t last_bit = first_bit + STORAGE_TOOL_BLOCK_SIZE * 8u - 1u;

        storage_zero_block_local(block);
        if (first_bit <= super.data_start) {
            uint32_t mark_last = super.data_start < last_bit ? super.data_start : last_bit;

            for (uint32_t i = first_bit; i <= mark_last; i++) {
                mkfs_bitmap_set_local(block, i - first_bit);
            }
        }
        if (!storage_write_block_local(target->disk, target->start_lba + super.bitmap_start + b, block)) {
            write_err_str("mkfs: bitmap write failed\n");
            return 1;
        }
    }

    for (uint32_t i = 0; i < sizeof(root); i++) {
        ((uint8_t *)&root)[i] = 0u;
    }
    root.used = 1u;
    root.size = STORAGE_TOOL_BLOCK_SIZE;
    root.type = STORAGE_TOOL_NXFS_TYPE_DIR;
    root.mode = 0755u;
    root.uid = 0u;
    root.gid = 0u;
    root.nlink = 2u;
    root.atime = 0;
    root.mtime = 0;
    root.ctime = 0;
    root.extents[0].start = super.data_start;
    root.extents[0].len = 1u;
    if (!mkfs_write_struct_block_local(target, super.inode_start, &root, sizeof(root))) {
        write_err_str("mkfs: root inode write failed\n");
        return 1;
    }

    storage_zero_block_local(block);
    mkfs_put_u32_local(block, 0u);
    block[4] = '.';
    mkfs_put_u32_local(block + sizeof(struct storage_tool_nxfs_dir_entry), 0u);
    block[sizeof(struct storage_tool_nxfs_dir_entry) + 4u] = '.';
    block[sizeof(struct storage_tool_nxfs_dir_entry) + 5u] = '.';
    if (!storage_write_block_local(target->disk, target->start_lba + super.data_start, block)) {
        write_err_str("mkfs: root directory write failed\n");
        return 1;
    }

    write_str("mkfs.nxfs: formatted ");
    storage_write_u64_dec(target->blocks * STORAGE_TOOL_BLOCK_SIZE);
    write_str(" bytes\n");
    return 0;
}

static uint32_t mkfs_fat32_volume_id_local(const struct storage_block_target *target) {
    uint32_t state = 0x4e465333u ^ target->disk ^
                     (uint32_t)target->start_lba ^
                     (uint32_t)(target->blocks >> 32) ^
                     (uint32_t)target->blocks;

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static uint8_t mkfs_fat32_sectors_per_cluster_local(uint32_t total_sectors) {
    if (total_sectors < 65536u) {
        return 1u;
    }
    if (total_sectors < 524288u) {
        return 4u;
    }
    if (total_sectors < 16777216u) {
        return 8u;
    }
    if (total_sectors < 33554432u) {
        return 32u;
    }
    return 64u;
}

static uint32_t mkfs_fat32_compute_spf_local(uint32_t total_sectors,
                                             uint8_t sectors_per_cluster,
                                             uint32_t *cluster_count_out) {
    uint32_t sectors_per_fat = 1u;
    uint32_t cluster_count = 0u;

    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t meta = STORAGE_TOOL_FAT32_RESERVED_SECTORS +
                        STORAGE_TOOL_FAT32_TABLES * sectors_per_fat;
        uint32_t data_sectors;
        uint32_t needed;

        if (meta >= total_sectors) {
            return 0u;
        }
        data_sectors = total_sectors - meta;
        cluster_count = data_sectors / sectors_per_cluster;
        needed = (uint32_t)((((uint64_t)cluster_count + 2u) * 4u +
                             STORAGE_TOOL_BLOCK_SIZE - 1u) /
                            STORAGE_TOOL_BLOCK_SIZE);
        if (needed == sectors_per_fat) {
            break;
        }
        sectors_per_fat = needed;
    }
    if (cluster_count_out != NULL) {
        *cluster_count_out = cluster_count;
    }
    return sectors_per_fat;
}

static void mkfs_fat32_make_boot_sector_local(uint8_t block[STORAGE_TOOL_BLOCK_SIZE],
                                              const struct storage_block_target *target,
                                              uint32_t total_sectors,
                                              uint8_t sectors_per_cluster,
                                              uint32_t sectors_per_fat) {
    storage_zero_block_local(block);
    block[0] = 0xebu;
    block[1] = 0x58u;
    block[2] = 0x90u;
    block[3] = 'N';
    block[4] = 'E';
    block[5] = 'X';
    block[6] = 'O';
    block[7] = 'S';
    block[8] = ' ';
    block[9] = ' ';
    block[10] = ' ';
    mkfs_put_u16_local(block + 11u, STORAGE_TOOL_BLOCK_SIZE);
    block[13] = sectors_per_cluster;
    mkfs_put_u16_local(block + 14u, STORAGE_TOOL_FAT32_RESERVED_SECTORS);
    block[16] = STORAGE_TOOL_FAT32_TABLES;
    mkfs_put_u16_local(block + 17u, 0u);
    mkfs_put_u16_local(block + 19u, 0u);
    block[21] = 0xf8u;
    mkfs_put_u16_local(block + 22u, 0u);
    mkfs_put_u16_local(block + 24u, 63u);
    mkfs_put_u16_local(block + 26u, 255u);
    mkfs_put_u32_local(block + 28u, (uint32_t)target->start_lba);
    mkfs_put_u32_local(block + 32u, total_sectors);
    mkfs_put_u32_local(block + 36u, sectors_per_fat);
    mkfs_put_u16_local(block + 40u, 0u);
    mkfs_put_u16_local(block + 42u, 0u);
    mkfs_put_u32_local(block + 44u, STORAGE_TOOL_FAT32_ROOT_CLUSTER);
    mkfs_put_u16_local(block + 48u, 1u);
    mkfs_put_u16_local(block + 50u, 6u);
    block[64] = 0x80u;
    block[66] = 0x29u;
    mkfs_put_u32_local(block + 67u, mkfs_fat32_volume_id_local(target));
    {
        const char label[11] = {'N', 'E', 'X', 'O', 'S', ' ', ' ', ' ', ' ', ' ', ' '};
        const char type[8] = {'F', 'A', 'T', '3', '2', ' ', ' ', ' '};

        for (uint32_t i = 0; i < sizeof(label); i++) {
            block[71u + i] = (uint8_t)label[i];
        }
        for (uint32_t i = 0; i < sizeof(type); i++) {
            block[82u + i] = (uint8_t)type[i];
        }
    }
    block[510] = 0x55u;
    block[511] = 0xaau;
}

static void mkfs_fat32_make_fsinfo_local(uint8_t block[STORAGE_TOOL_BLOCK_SIZE],
                                         uint32_t cluster_count) {
    storage_zero_block_local(block);
    mkfs_put_u32_local(block + 0u, 0x41615252u);
    mkfs_put_u32_local(block + 484u, 0x61417272u);
    mkfs_put_u32_local(block + 488u, cluster_count > 0u ? cluster_count - 1u : 0u);
    mkfs_put_u32_local(block + 492u, 3u);
    mkfs_put_u32_local(block + 508u, 0xaa550000u);
}

static int mkfs_fat32_write_fat_copy_local(const struct storage_block_target *target,
                                           uint32_t fat_lba,
                                           uint32_t sectors_per_fat) {
    uint8_t block[STORAGE_TOOL_BLOCK_SIZE];

    storage_zero_block_local(block);
    mkfs_put_u32_local(block + 0u, 0x0ffffff8u);
    mkfs_put_u32_local(block + 4u, 0xffffffffu);
    mkfs_put_u32_local(block + 8u, 0x0fffffffu);
    if (!storage_write_block_local(target->disk, target->start_lba + fat_lba, block)) {
        return 0;
    }
    storage_zero_block_local(block);
    for (uint32_t i = 1u; i < sectors_per_fat; i++) {
        if (!storage_write_block_local(target->disk, target->start_lba + fat_lba + i, block)) {
            return 0;
        }
    }
    return 1;
}

static int cmd_mkfs_fat32_local(const struct storage_block_target *target) {
    uint8_t block[STORAGE_TOOL_BLOCK_SIZE];
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t cluster_count = 0u;
    uint8_t sectors_per_cluster;
    uint32_t first_fat_lba;
    uint32_t second_fat_lba;
    uint32_t data_lba;

    if (target == NULL || !target->valid || !target->writable || target->blocks > 0xffffffffu) {
        write_err_str("mkfs.fat: invalid or read-only target\n");
        return 1;
    }
    total_sectors = (uint32_t)target->blocks;
    sectors_per_cluster = mkfs_fat32_sectors_per_cluster_local(total_sectors);
    sectors_per_fat = mkfs_fat32_compute_spf_local(total_sectors, sectors_per_cluster, &cluster_count);
    if (sectors_per_fat == 0u || cluster_count == 0u) {
        write_err_str("mkfs.fat: target too small\n");
        return 1;
    }
    first_fat_lba = STORAGE_TOOL_FAT32_RESERVED_SECTORS;
    second_fat_lba = first_fat_lba + sectors_per_fat;
    data_lba = STORAGE_TOOL_FAT32_RESERVED_SECTORS +
               STORAGE_TOOL_FAT32_TABLES * sectors_per_fat;
    if (data_lba + sectors_per_cluster > total_sectors) {
        write_err_str("mkfs.fat: target too small\n");
        return 1;
    }

    mkfs_fat32_make_boot_sector_local(block, target, total_sectors, sectors_per_cluster, sectors_per_fat);
    if (!storage_write_block_local(target->disk, target->start_lba, block) ||
        !storage_write_block_local(target->disk, target->start_lba + 6u, block)) {
        write_err_str("mkfs.fat: boot sector write failed\n");
        return 1;
    }

    mkfs_fat32_make_fsinfo_local(block, cluster_count);
    if (!storage_write_block_local(target->disk, target->start_lba + 1u, block) ||
        !storage_write_block_local(target->disk, target->start_lba + 7u, block)) {
        write_err_str("mkfs.fat: fsinfo write failed\n");
        return 1;
    }

    storage_zero_block_local(block);
    if (!storage_write_block_local(target->disk, target->start_lba + 2u, block) ||
        !storage_write_block_local(target->disk, target->start_lba + 8u, block)) {
        write_err_str("mkfs.fat: reserved sector write failed\n");
        return 1;
    }
    if (!mkfs_fat32_write_fat_copy_local(target, first_fat_lba, sectors_per_fat) ||
        !mkfs_fat32_write_fat_copy_local(target, second_fat_lba, sectors_per_fat)) {
        write_err_str("mkfs.fat: FAT write failed\n");
        return 1;
    }
    for (uint32_t i = 0u; i < sectors_per_cluster; i++) {
        storage_zero_block_local(block);
        if (!storage_write_block_local(target->disk, target->start_lba + data_lba + i, block)) {
            write_err_str("mkfs.fat: root directory write failed\n");
            return 1;
        }
    }

    write_str("mkfs.fat: formatted FAT32 ");
    storage_write_u64_dec(target->blocks * STORAGE_TOOL_BLOCK_SIZE);
    write_str(" bytes\n");
    return 0;
}

int cmd_mkfs(int argc, char **argv) {
    const char *kind;
    const char *target_path;
    struct storage_block_target target;
    uint32_t arg_index;
    uint32_t fat_bits = 0u;

    if (argc == 2 && storage_starts_with_local(argv[0], "mkfs.")) {
        kind = argv[0] + 5;
        arg_index = 1u;
    } else if (argc >= 3) {
        kind = argv[1];
        arg_index = 2u;
    } else {
        write_err_usage("mkfs", " nxfs /dev/diskXpY\n");
        write_err_str("   or: mkfs fat32 /dev/diskXpY\n");
        write_err_str("   or: mkfs.fat -F 32 /dev/diskXpY\n");
        write_err_str("   or: mkfs.nxfs /dev/diskXpY\n");
        return 1;
    }
    while (arg_index < (uint32_t)argc && argv[arg_index][0] == '-') {
        if (streq_local(argv[arg_index], "-F") && arg_index + 1u < (uint32_t)argc) {
            uint64_t parsed = 0u;

            if (!storage_parse_u64_local(argv[arg_index + 1u], &parsed) ||
                parsed > 0xffffffffu) {
                write_err_str("mkfs.fat: invalid -F value\n");
                return 1;
            }
            fat_bits = (uint32_t)parsed;
            arg_index += 2u;
        } else {
            write_err_usage("mkfs", " [nxfs|fat32] [-F 32] /dev/diskXpY\n");
            return 1;
        }
    }
    if (arg_index + 1u != (uint32_t)argc) {
        write_err_usage("mkfs", " [nxfs|fat32] [-F 32] /dev/diskXpY\n");
        return 1;
    }
    target_path = argv[arg_index];
    target.valid = 0u;
    if (!storage_resolve_block_target_local(target_path, &target)) {
        write_err_str("mkfs: target must be /dev/diskX or /dev/diskXpY\n");
        return 1;
    }
    if (streq_ignore_case_local(kind, "nxfs")) {
        if (fat_bits != 0u) {
            write_err_str("mkfs.nxfs: -F is only valid for FAT\n");
            return 1;
        }
        return cmd_mkfs_nxfs_local(&target);
    }
    if (streq_ignore_case_local(kind, "fat") || streq_ignore_case_local(kind, "fat32")) {
        if (fat_bits != 0u && fat_bits != 32u) {
            write_err_str("mkfs.fat: only -F 32 is supported\n");
            return 1;
        }
        return cmd_mkfs_fat32_local(&target);
    }
    write_err_str("mkfs: supported filesystems: nxfs, fat32\n");
    return 1;
}
