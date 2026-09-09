#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fs/nxfs.h"
#include "kernel/public/proc/process.h"

enum {
    NXFS_HOST_DEFAULT_BLOCKS = 147456,
    NXFS_HOST_CAP_MODE_SHIFT = 16u,
    NXFS_HOST_CAP_POLICY_PRESENT = 1u << 31,
    NXFS_HOST_CAP_POLICY_MAX = 8192u,
    NXFS_HOST_CAP_TOKEN_MAX = 96u,
    NXFS_HOST_CAP_MATCH_NONE = 0u,
    NXFS_HOST_CAP_MATCH_DEFAULT = 1u,
    NXFS_HOST_CAP_MATCH_BASENAME = 2u,
    NXFS_HOST_CAP_MATCH_EXACT = 3u
};

static FILE *g_disk = NULL;
static struct nxfs_super g_super;
static struct block_device g_host_bdev;

static uint32_t uuid_prng_next(uint32_t *state) {
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x != 0u ? x : 0x6d2b79f5u;
    return *state;
}

static void generate_uuid_local(uint8_t uuid[16]) {
    FILE *fp;
    uint32_t state;

    fp = fopen("/dev/urandom", "rb");
    if (fp != NULL) {
        if (fread(uuid, 1, 16, fp) == 16) {
            fclose(fp);
            uuid[6] = (uint8_t)((uuid[6] & 0x0fu) | 0x40u);
            uuid[8] = (uint8_t)((uuid[8] & 0x3fu) | 0x80u);
            return;
        }
        fclose(fp);
    }

    state = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)&state ^ 0xa5c3f19du;
    for (uint32_t i = 0; i < 16u; i++) {
        uuid[i] = (uint8_t)(uuid_prng_next(&state) >> 24);
    }
    uuid[6] = (uint8_t)((uuid[6] & 0x0fu) | 0x40u);
    uuid[8] = (uint8_t)((uuid[8] & 0x3fu) | 0x80u);
}

static void print_uuid_local(const uint8_t uuid[16]) {
    for (uint32_t i = 0; i < 16u; i++) {
        printf("%02x", (unsigned)uuid[i]);
        if (i == 3u || i == 5u || i == 7u || i == 9u) {
            printf("-");
        }
    }
}

static uint32_t host_file_block_count_or_die(FILE *fp, const char *path) {
    long size;

    if (fp == NULL) {
        fprintf(stderr, "nxfs_host: null file handle: %s\n", path);
        exit(EXIT_FAILURE);
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("nxfs_host: fseek");
        exit(EXIT_FAILURE);
    }

    size = ftell(fp);
    if (size < 0) {
        perror("nxfs_host: ftell");
        exit(EXIT_FAILURE);
    }

    if ((uint64_t)size < NXFS_BLOCK_SIZE) {
        fprintf(stderr, "nxfs_host: image too small: %s (%ld bytes)\n", path, size);
        exit(EXIT_FAILURE);
    }

    if (((uint64_t)size % NXFS_BLOCK_SIZE) != 0) {
        fprintf(stderr,
                "nxfs_host: image size is not block aligned: %s (%ld bytes, block=%u)\n",
                path,
                size,
                NXFS_BLOCK_SIZE);
        exit(EXIT_FAILURE);
    }

    if (((uint64_t)size / NXFS_BLOCK_SIZE) > UINT32_MAX) {
        fprintf(stderr, "nxfs_host: image too large: %s\n", path);
        exit(EXIT_FAILURE);
    }

    return (uint32_t)((uint64_t)size / NXFS_BLOCK_SIZE);
}

static void host_zero_blocks_or_die(uint32_t total_blocks) {
    uint8_t zero[NXFS_BLOCK_SIZE];

    memset(zero, 0, sizeof(zero));

    if (fseek(g_disk, 0, SEEK_SET) != 0) {
        perror("nxfs_host: fseek");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < total_blocks; i++) {
        if (fwrite(zero, NXFS_BLOCK_SIZE, 1, g_disk) != 1) {
            perror("nxfs_host: fwrite");
            exit(EXIT_FAILURE);
        }
    }

    if (fflush(g_disk) != 0) {
        perror("nxfs_host: fflush");
        exit(EXIT_FAILURE);
    }
}

int blockdev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (dev == NULL || dev->read == NULL || buffer == NULL || count == 0) {
        return -1;
    }
    return dev->read(dev, lba, count, buffer);
}

int blockdev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (dev == NULL || dev->write == NULL || buffer == NULL || count == 0) {
        return -1;
    }
    return dev->write(dev, lba, count, buffer);
}

static int host_block_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    (void)dev;
    if (g_disk == NULL || buffer == NULL) {
        return -1;
    }
    if (fseek(g_disk, (long)(lba * NXFS_BLOCK_SIZE), SEEK_SET) != 0) {
        return -1;
    }
    return fread(buffer, NXFS_BLOCK_SIZE, count, g_disk) == count ? 0 : -1;
}

static int host_block_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    (void)dev;
    if (g_disk == NULL || buffer == NULL) {
        return -1;
    }
    if (fseek(g_disk, (long)(lba * NXFS_BLOCK_SIZE), SEEK_SET) != 0) {
        return -1;
    }
    return fwrite(buffer, NXFS_BLOCK_SIZE, count, g_disk) == count ? 0 : -1;
}

static void write_block_local(uint32_t block, const void *buffer) {
    fseek(g_disk, (long)(block * NXFS_BLOCK_SIZE), SEEK_SET);
    fwrite(buffer, NXFS_BLOCK_SIZE, 1, g_disk);
}

static void write_inode_local(uint32_t inode_index, const struct nxfs_inode *inode) {
    uint8_t block[NXFS_BLOCK_SIZE];
    uint32_t inode_offset;
    uint32_t block_index;
    uint32_t offset_in_block;

    // 인덱스 계산 레이어
    inode_offset = g_super.inode_start * NXFS_BLOCK_SIZE + inode_index * sizeof(*inode);
    block_index = inode_offset / NXFS_BLOCK_SIZE;
    offset_in_block = inode_offset % NXFS_BLOCK_SIZE;

    // 1. Seek 검증
    if (fseek(g_disk, (long)(block_index * NXFS_BLOCK_SIZE), SEEK_SET) != 0) {
        perror("nxfs_host: fseek failed");
        exit(EXIT_FAILURE); // 호스트 툴이므로 즉시 중단하여 정합성 보호
    }

    // 2. Read 검증 (Ubuntu 경고 해결 및 데이터 무결성 확보)
    if (fread(block, NXFS_BLOCK_SIZE, 1, g_disk) != 1) {
        fprintf(stderr, "nxfs_host: Failed to read block %u for inode %u\n", block_index, inode_index);
        exit(EXIT_FAILURE);
    }

    // 3. Modify & Write
    memcpy(block + offset_in_block, inode, sizeof(*inode));
    write_block_local(block_index, block);
}

static void bitmap_set_local(uint8_t *bitmap, uint32_t block, int used) {
    if (used) {
        bitmap[block / 8u] |= (uint8_t)(1u << (block % 8u));
    } else {
        bitmap[block / 8u] &= (uint8_t)~(1u << (block % 8u));
    }
}

static void bitmap_set_block_local(uint32_t block, int used) {
    uint8_t bitmap[NXFS_BLOCK_SIZE];
    uint32_t bits_per_block = NXFS_BLOCK_SIZE * 8u;
    uint32_t bitmap_block = g_super.bitmap_start + block / bits_per_block;

    if (fseek(g_disk, (long)(bitmap_block * NXFS_BLOCK_SIZE), SEEK_SET) != 0 ||
        fread(bitmap, NXFS_BLOCK_SIZE, 1, g_disk) != 1) {
        fprintf(stderr, "nxfs_host: failed to read bitmap block %u\n", bitmap_block);
        exit(EXIT_FAILURE);
    }
    bitmap_set_local(bitmap, block % bits_per_block, used);
    write_block_local(bitmap_block, bitmap);
}

static void init_root_dir_block(uint32_t block) {
    struct nxfs_dir_entry entries[NXFS_BLOCK_SIZE / sizeof(struct nxfs_dir_entry)];

    memset(entries, 0, sizeof(entries));
    entries[0].inode = 0;
    memcpy(entries[0].name, ".", 2);
    entries[1].inode = 0;
    memcpy(entries[1].name, "..", 3);
    write_block_local(block, entries);
}

static void mkfs_local(const char *file) {
    struct nxfs_inode root;
    uint32_t bitmap_blocks;
    uint32_t inode_bytes;
    uint32_t inode_blocks;
    uint32_t total_blocks;

    memset(&root, 0, sizeof(root));

    /*
     * If the image already exists, use its current size.
     * This lets Makefile do:
     *
     *   truncate -s 511M image
     *   nxfs_host mkfs image
     *
     * If it does not exist, create a default-sized image for old workflows.
     */
    g_disk = fopen(file, "rb+");
    if (g_disk == NULL) {
        g_disk = fopen(file, "wb+");
        if (g_disk == NULL) {
            perror(file);
            exit(EXIT_FAILURE);
        }

        total_blocks = NXFS_HOST_DEFAULT_BLOCKS;
    } else {
        total_blocks = host_file_block_count_or_die(g_disk, file);
    }

    /*
     * For a newly-created file, extend it to the default size.
     * For an existing truncated file, rewrite exactly that many blocks.
     */
    host_zero_blocks_or_die(total_blocks);

    inode_bytes = NXFS_MAX_INODES * sizeof(struct nxfs_inode);
    inode_blocks = (inode_bytes + NXFS_BLOCK_SIZE - 1u) / NXFS_BLOCK_SIZE;
    bitmap_blocks = (total_blocks + (NXFS_BLOCK_SIZE * 8u) - 1u) / (NXFS_BLOCK_SIZE * 8u);

    g_super.magic = NXFS_MAGIC;
    g_super.total_blocks = total_blocks;
    g_super.bitmap_start = 1;
    g_super.inode_start = g_super.bitmap_start + bitmap_blocks;
    g_super.data_start = g_super.inode_start + inode_blocks;
    generate_uuid_local(g_super.uuid);

    if (g_super.data_start >= g_super.total_blocks) {
        fprintf(stderr,
                "nxfs_host: image too small for metadata: total=%u data_start=%u\n",
                g_super.total_blocks,
                g_super.data_start);
        fclose(g_disk);
        g_disk = NULL;
        exit(EXIT_FAILURE);
    }

    if (fseek(g_disk, 0, SEEK_SET) != 0) {
        perror("nxfs_host: fseek");
        fclose(g_disk);
        g_disk = NULL;
        exit(EXIT_FAILURE);
    }

    if (fwrite(&g_super, sizeof(g_super), 1, g_disk) != 1) {
        perror("nxfs_host: fwrite super");
        fclose(g_disk);
        g_disk = NULL;
        exit(EXIT_FAILURE);
    }

    for (uint32_t b = 0; b < g_super.data_start; b++) {
        bitmap_set_block_local(b, 1);
    }

    bitmap_set_block_local(g_super.data_start, 1);

    root.used = 1;
    root.type = NXFS_TYPE_DIR;
    root.mode = 0755;
    root.nlink = 2;
    root.size = NXFS_BLOCK_SIZE;
    root.extents[0].start = g_super.data_start;
    root.extents[0].len = 1;

    write_inode_local(0, &root);
    init_root_dir_block(root.extents[0].start);

    fclose(g_disk);
    g_disk = NULL;
}

static void open_disk_local(const char *file) {
    g_disk = fopen(file, "rb+");
    if (g_disk == NULL) {
        perror(file);
        exit(1);
    }
    memset(&g_host_bdev, 0, sizeof(g_host_bdev));
    g_host_bdev.name = "host-nxfs";
    g_host_bdev.block_size = NXFS_BLOCK_SIZE;
    g_host_bdev.block_count = host_file_block_count_or_die(g_disk, file);
    g_host_bdev.read = host_block_read;
    g_host_bdev.write = host_block_write;
}

static void close_disk_local(void) {
    if (g_disk != NULL) {
        fclose(g_disk);
        g_disk = NULL;
    }
}

static void mount_image_local(const char *file, struct nxfs_volume *vol) {
    open_disk_local(file);
    if (nxfs_mount(vol, &g_host_bdev, 0) != 0) {
        fprintf(stderr, "nxfs_host: mount failed: %s\n", file);
        close_disk_local();
        exit(1);
    }
    g_super = vol->super;
}

static void mkdir_local(const char *image, const char *path) {
    struct nxfs_volume vol;
    struct nxfs_inode inode;

    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, path, 0, &inode) == 0) {
        if (inode.type != NXFS_TYPE_DIR) {
            fprintf(stderr, "nxfs_host: exists and is not a directory: %s\n", path);
            close_disk_local();
            exit(1);
        }
        close_disk_local();
        return;
    }
    if (nxfs_mkdir_path(&vol, path, 0, 0) != 0) {
        fprintf(stderr, "nxfs_host: mkdir failed: %s\n", path);
        close_disk_local();
        exit(1);
    }
    close_disk_local();
}

static void write_file_local(const char *image, const char *src, const char *dst) {
    struct nxfs_volume vol;
    struct nxfs_inode inode;
    uint32_t inode_index = 0;
    uint32_t written = 0;
    long file_size;
    FILE *in;
    uint8_t *buffer;

    mount_image_local(image, &vol);
    in = fopen(src, "rb");
    if (in == NULL) {
        perror(src);
        close_disk_local();
        exit(1);
    }
    if (nxfs_lookup_path(&vol, dst, &inode_index, &inode) == 0) {
        if (inode.type != NXFS_TYPE_FILE || nxfs_unlink_path(&vol, dst) != 0) {
            fprintf(stderr, "nxfs_host: cannot replace: %s\n", dst);
            fclose(in);
            close_disk_local();
            exit(1);
        }
    }
    if (nxfs_create_path(&vol, dst, &inode_index, &inode) != 0) {
        fprintf(stderr, "nxfs_host: create failed: %s\n", dst);
        fclose(in);
        close_disk_local();
        exit(1);
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        perror(src);
        fclose(in);
        close_disk_local();
        exit(1);
    }
    file_size = ftell(in);
    if (file_size < 0 || (uint64_t)file_size > UINT32_MAX) {
        fprintf(stderr, "nxfs_host: file too large: %s\n", src);
        fclose(in);
        close_disk_local();
        exit(1);
    }
    if (fseek(in, 0, SEEK_SET) != 0) {
        perror(src);
        fclose(in);
        close_disk_local();
        exit(1);
    }
    buffer = malloc((size_t)file_size == 0 ? 1 : (size_t)file_size);
    if (buffer == NULL) {
        fprintf(stderr, "nxfs_host: out of memory: %s\n", src);
        fclose(in);
        close_disk_local();
        exit(1);
    }
    if (fread(buffer, 1, (size_t)file_size, in) != (size_t)file_size) {
        perror(src);
        free(buffer);
        fclose(in);
        close_disk_local();
        exit(1);
    }
    if (nxfs_write_file_range(&vol, inode_index, &inode, 0, buffer, (uint32_t)file_size, &written) != 0 ||
        written != (uint32_t)file_size) {
        fprintf(stderr, "nxfs_host: write failed: %s\n", dst);
        free(buffer);
        fclose(in);
        close_disk_local();
        exit(1);
    }
    free(buffer);
    fclose(in);
    close_disk_local();
}

static void exists_local(const char *image, const char *path) {
    struct nxfs_volume vol;

    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, path, 0, 0) != 0) {
        fprintf(stderr, "nxfs_host: not found: %s\n", path);
        close_disk_local();
        exit(1);
    }
    close_disk_local();
}

static void list_local(const char *image, const char *path) {
    struct nxfs_volume vol;
    struct nxfs_inode dir;
    uint32_t dir_ino;
    uint32_t count = 0;

    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, path, &dir_ino, &dir) != 0 || dir.type != NXFS_TYPE_DIR) {
        fprintf(stderr, "nxfs_host: not a directory: %s\n", path);
        close_disk_local();
        exit(1);
    }
    for (uint32_t i = 0;; i++) {
        struct nxfs_dir_entry entry;

        if (nxfs_get_dir_entry(&vol, dir_ino, &dir, i, &entry) != 0) {
            break;
        }
        printf("%s\n", entry.name);
        count++;
    }
    (void)count;
    close_disk_local();
}

static void info_local(const char *image, const char *path) {
    struct nxfs_volume vol;
    struct nxfs_inode inode;
    uint32_t ino;

    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, path, &ino, &inode) != 0) {
        fprintf(stderr, "nxfs_host: not found: %s\n", path);
        close_disk_local();
        exit(1);
    }
    printf("ino=%u type=%u size=%u mode=%03o uid=%u gid=%u",
           ino,
           inode.type,
           inode.size,
           inode.mode & 07777u,
           inode.uid,
           inode.gid);
    if ((inode.mode & NXFS_HOST_CAP_POLICY_PRESENT) != 0u) {
        printf(" cap_policy=1 caps=%x",
               (inode.mode >> NXFS_HOST_CAP_MODE_SHIFT) &
                   PROCESS_CAP_SYS_ADMIN);
    }
    for (uint32_t i = 0; i < NXFS_EXTENTS; i++) {
        printf(" extent%u=%u:%u", i, inode.extents[i].start, inode.extents[i].len);
    }
    printf("\n");
    close_disk_local();
}

static uint32_t parse_octal_mode_or_die(const char *text) {
    uint32_t mode = 0;

    if (text == NULL || text[0] == '\0') {
        fprintf(stderr, "nxfs_host: empty mode\n");
        exit(1);
    }
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '7') {
            fprintf(stderr, "nxfs_host: bad octal mode: %s\n", text);
            exit(1);
        }
        mode = (mode << 3) | (uint32_t)(text[i] - '0');
        if (mode > 07777u) {
            fprintf(stderr, "nxfs_host: mode out of range: %s\n", text);
            exit(1);
        }
    }
    return mode;
}

static uint32_t parse_u32_or_die(const char *text, const char *name) {
    uint32_t value = 0;

    if (text == NULL || text[0] == '\0') {
        fprintf(stderr, "nxfs_host: empty %s\n", name);
        exit(1);
    }
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            fprintf(stderr, "nxfs_host: bad %s: %s\n", name, text);
            exit(1);
        }
        value = value * 10u + (uint32_t)(text[i] - '0');
    }
    return value;
}

static const char *cap_policy_basename_local(const char *path) {
    const char *base = path;

    if (path == NULL) {
        return "";
    }
    for (uint32_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            base = path + i + 1u;
        }
    }
    return base;
}

static int cap_policy_has_path_local(const char *text) {
    if (text == NULL) {
        return 0;
    }
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '/') {
            return 1;
        }
    }
    return 0;
}

static const char *cap_policy_line_end_local(const char *line) {
    while (line != NULL && line[0] != '\0' && line[0] != '\n' && line[0] != '\r') {
        line++;
    }
    return line;
}

static const char *cap_policy_skip_inline_spaces_local(const char *cursor,
                                                       const char *line_end) {
    while (cursor < line_end && (cursor[0] == ' ' || cursor[0] == '\t')) {
        cursor++;
    }
    return cursor;
}

static int cap_policy_read_token_local(const char **cursor_io,
                                       const char *line_end,
                                       char *out,
                                       uint32_t out_size) {
    const char *cursor;
    uint32_t len = 0u;

    if (cursor_io == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    cursor = cap_policy_skip_inline_spaces_local(*cursor_io, line_end);
    if (cursor >= line_end || cursor[0] == '#') {
        out[0] = '\0';
        *cursor_io = cursor;
        return 0;
    }
    while (cursor < line_end &&
           cursor[0] != ' ' && cursor[0] != '\t' &&
           cursor[0] != '#') {
        if (len + 1u >= out_size) {
            return 0;
        }
        out[len++] = *cursor++;
    }
    out[len] = '\0';
    *cursor_io = cursor;
    return len != 0u;
}

static int cap_policy_streq_local(const char *a, const char *b) {
    uint32_t i = 0u;

    if (a == NULL || b == NULL) {
        return 0;
    }
    while (a[i] == b[i]) {
        if (a[i] == '\0') {
            return 1;
        }
        i++;
    }
    return 0;
}

static uint32_t cap_policy_match_score_local(const char *pattern,
                                             const char *image_name) {
    if (pattern == NULL || image_name == NULL || pattern[0] == '\0') {
        return NXFS_HOST_CAP_MATCH_NONE;
    }
    if (cap_policy_streq_local(pattern, "*") ||
        cap_policy_streq_local(pattern, "default")) {
        return NXFS_HOST_CAP_MATCH_DEFAULT;
    }
    if (cap_policy_has_path_local(pattern)) {
        return cap_policy_streq_local(pattern, image_name)
            ? NXFS_HOST_CAP_MATCH_EXACT
            : NXFS_HOST_CAP_MATCH_NONE;
    }
    return cap_policy_streq_local(pattern, cap_policy_basename_local(image_name))
        ? NXFS_HOST_CAP_MATCH_BASENAME
        : NXFS_HOST_CAP_MATCH_NONE;
}

static int cap_policy_parse_cap_local(const char *name, uint32_t *cap_out) {
    char *end = NULL;
    unsigned long numeric;

    if (name == NULL || cap_out == NULL) {
        return 0;
    }
    if (strcmp(name, "none") == 0 || strcmp(name, "-") == 0) {
        *cap_out = 0u;
        return 1;
    }
    if (strcmp(name, "all") == 0) {
        *cap_out = PROCESS_CAP_SYS_ADMIN;
        return 1;
    }
    if (strcmp(name, "power") == 0) {
        *cap_out = PROCESS_CAP_POWER;
        return 1;
    }
    if (strcmp(name, "raw-block") == 0 || strcmp(name, "raw_block") == 0 ||
        strcmp(name, "block") == 0) {
        *cap_out = PROCESS_CAP_RAW_BLOCK;
        return 1;
    }
    if (strcmp(name, "mount") == 0) {
        *cap_out = PROCESS_CAP_MOUNT;
        return 1;
    }
    if (strcmp(name, "signal") == 0 || strcmp(name, "kill") == 0) {
        *cap_out = PROCESS_CAP_SIGNAL;
        return 1;
    }
    if (strcmp(name, "grant") == 0) {
        *cap_out = PROCESS_CAP_GRANT;
        return 1;
    }
    if (strcmp(name, "audio") == 0) {
        *cap_out = PROCESS_CAP_AUDIO;
        return 1;
    }
    if (strcmp(name, "net-raw") == 0 || strcmp(name, "net_raw") == 0 ||
        strcmp(name, "network") == 0 || strcmp(name, "rtl8139") == 0) {
        *cap_out = PROCESS_CAP_NET_RAW;
        return 1;
    }
    if (strcmp(name, "display") == 0 || strcmp(name, "gfx") == 0) {
        *cap_out = PROCESS_CAP_DISPLAY;
        return 1;
    }
    if (strcmp(name, "input") == 0) {
        *cap_out = PROCESS_CAP_INPUT;
        return 1;
    }
    if (strcmp(name, "clipboard") == 0) {
        *cap_out = PROCESS_CAP_CLIPBOARD;
        return 1;
    }
    if (strcmp(name, "debug") == 0) {
        *cap_out = PROCESS_CAP_DEBUG;
        return 1;
    }
    numeric = strtoul(name, &end, 0);
    if (end != name && *end == '\0' && numeric <= PROCESS_CAP_SYS_ADMIN) {
        *cap_out = (uint32_t)numeric;
        return 1;
    }
    return 0;
}

static int cap_policy_parse_caps_local(const char *cursor,
                                       const char *line_end,
                                       uint32_t *mask_out) {
    char token[NXFS_HOST_CAP_TOKEN_MAX];
    uint32_t mask = 0u;
    int saw_cap = 0;

    cursor = cap_policy_skip_inline_spaces_local(cursor, line_end);
    while (cursor < line_end && cursor[0] != '#') {
        uint32_t cap;

        if (!cap_policy_read_token_local(&cursor, line_end, token, sizeof(token)) ||
            !cap_policy_parse_cap_local(token, &cap)) {
            return 0;
        }
        mask |= cap;
        saw_cap = 1;
        cursor = cap_policy_skip_inline_spaces_local(cursor, line_end);
    }
    if (!saw_cap || mask_out == NULL) {
        return 0;
    }
    *mask_out = mask & PROCESS_CAP_SYS_ADMIN;
    return 1;
}

static void cap_policy_apply_line_local(const char *line,
                                        const char *image_name,
                                        uint32_t *best_score_io,
                                        uint32_t *policy_mask_io) {
    char pattern[NXFS_HOST_CAP_TOKEN_MAX];
    const char *cursor = line;
    const char *line_end = cap_policy_line_end_local(line);
    uint32_t score;
    uint32_t mask;

    if (!cap_policy_read_token_local(&cursor, line_end, pattern, sizeof(pattern))) {
        return;
    }
    score = cap_policy_match_score_local(pattern, image_name);
    if (score == NXFS_HOST_CAP_MATCH_NONE ||
        score < *best_score_io ||
        !cap_policy_parse_caps_local(cursor, line_end, &mask)) {
        return;
    }
    *best_score_io = score;
    *policy_mask_io = mask;
}

static int cap_policy_load_file_local(const char *policy_path,
                                      char *buffer,
                                      uint32_t buffer_size) {
    FILE *fp;
    size_t got;

    if (policy_path == NULL || buffer == NULL || buffer_size == 0u) {
        return 0;
    }
    fp = fopen(policy_path, "rb");
    if (fp == NULL) {
        return 0;
    }
    got = fread(buffer, 1, buffer_size - 1u, fp);
    if (ferror(fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    buffer[got] = '\0';
    return 1;
}

static int cap_policy_match_image_local(const char *policy,
                                        const char *image_name,
                                        uint32_t *mask_out) {
    uint32_t pos = 0u;
    uint32_t best_score = NXFS_HOST_CAP_MATCH_NONE;
    uint32_t mask = PROCESS_CAP_SYS_ADMIN;

    if (policy == NULL || image_name == NULL || mask_out == NULL) {
        return 0;
    }
    while (policy[pos] != '\0') {
        cap_policy_apply_line_local(policy + pos, image_name, &best_score, &mask);
        while (policy[pos] != '\0' && policy[pos] != '\n') {
            pos++;
        }
        if (policy[pos] == '\n') {
            pos++;
        }
    }
    if (best_score == NXFS_HOST_CAP_MATCH_NONE) {
        return 0;
    }
    *mask_out = mask & PROCESS_CAP_SYS_ADMIN;
    return 1;
}

static void setcap_inode_local(struct nxfs_volume *vol,
                               uint32_t ino,
                               struct nxfs_inode *inode,
                               uint32_t caps,
                               const char *path) {
    uint32_t mode;

    if ((caps & ~PROCESS_CAP_SYS_ADMIN) != 0u || inode->type != NXFS_TYPE_FILE) {
        fprintf(stderr, "nxfs_host: bad cap target: %s\n", path);
        close_disk_local();
        exit(1);
    }
    mode = (inode->mode & 07777u) |
           NXFS_HOST_CAP_POLICY_PRESENT |
           ((caps & PROCESS_CAP_SYS_ADMIN) << NXFS_HOST_CAP_MODE_SHIFT);
    if (nxfs_set_inode_metadata(vol, ino, inode, mode, inode->uid, inode->gid) != 0) {
        fprintf(stderr, "nxfs_host: setcap failed: %s\n", path);
        close_disk_local();
        exit(1);
    }
}

static void chmod_local(const char *image, const char *mode_text, const char *path) {
    struct nxfs_volume vol;
    struct nxfs_inode inode;
    uint32_t ino;
    uint32_t mode = parse_octal_mode_or_die(mode_text);

    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, path, &ino, &inode) != 0) {
        fprintf(stderr, "nxfs_host: not found: %s\n", path);
        close_disk_local();
        exit(1);
    }
    mode = (inode.mode & ~07777u) | (mode & 07777u);
    if (nxfs_set_inode_metadata(&vol, ino, &inode, mode, inode.uid, inode.gid) != 0) {
        fprintf(stderr, "nxfs_host: chmod failed: %s\n", path);
        close_disk_local();
        exit(1);
    }
    close_disk_local();
}

static void setcap_local(const char *image, const char *caps_text, const char *path) {
    struct nxfs_volume vol;
    struct nxfs_inode inode;
    uint32_t ino;
    uint32_t caps;

    if (!cap_policy_parse_cap_local(caps_text, &caps)) {
        caps = parse_u32_or_die(caps_text, "caps");
    }
    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, path, &ino, &inode) != 0) {
        fprintf(stderr, "nxfs_host: not found: %s\n", path);
        close_disk_local();
        exit(1);
    }
    setcap_inode_local(&vol, ino, &inode, caps, path);
    close_disk_local();
}

static void apply_caps_local(const char *image, const char *policy_path) {
    static char policy[NXFS_HOST_CAP_POLICY_MAX];
    struct nxfs_volume vol;
    struct nxfs_inode dir;
    uint32_t dir_ino;

    if (!cap_policy_load_file_local(policy_path, policy, sizeof(policy))) {
        fprintf(stderr, "nxfs_host: cannot read policy: %s\n", policy_path);
        exit(1);
    }
    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, "/cmd", &dir_ino, &dir) != 0 ||
        dir.type != NXFS_TYPE_DIR) {
        fprintf(stderr, "nxfs_host: /cmd not found\n");
        close_disk_local();
        exit(1);
    }
    for (uint32_t i = 0;; i++) {
        struct nxfs_dir_entry entry;
        struct nxfs_inode inode;
        char path[64];
        uint32_t ino;
        uint32_t caps;

        if (nxfs_get_dir_entry(&vol, dir_ino, &dir, i, &entry) != 0) {
            break;
        }
        if (snprintf(path, sizeof(path), "/cmd/%s", entry.name) <= 0) {
            continue;
        }
        if (nxfs_lookup_path(&vol, path, &ino, &inode) != 0 ||
            inode.type != NXFS_TYPE_FILE ||
            !cap_policy_match_image_local(policy, path, &caps)) {
            continue;
        }
        setcap_inode_local(&vol, ino, &inode, caps, path);
    }
    close_disk_local();
}

static void chown_local(const char *image,
                        const char *uid_text,
                        const char *gid_text,
                        const char *path) {
    struct nxfs_volume vol;
    struct nxfs_inode inode;
    uint32_t ino;
    uint32_t uid = parse_u32_or_die(uid_text, "uid");
    uint32_t gid = parse_u32_or_die(gid_text, "gid");

    mount_image_local(image, &vol);
    if (nxfs_lookup_path(&vol, path, &ino, &inode) != 0) {
        fprintf(stderr, "nxfs_host: not found: %s\n", path);
        close_disk_local();
        exit(1);
    }
    if (nxfs_set_inode_metadata(&vol, ino, &inode, inode.mode, uid, gid) != 0) {
        fprintf(stderr, "nxfs_host: chown failed: %s\n", path);
        close_disk_local();
        exit(1);
    }
    close_disk_local();
}

static void uuid_local(const char *image) {
    struct nxfs_volume vol;

    mount_image_local(image, &vol);
    printf("uuid=");
    print_uuid_local(vol.super.uuid);
    printf("\n");
    close_disk_local();
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s mkfs <image>\n", argv[0]);
        fprintf(stderr, "       %s mkdir <image> <path>\n", argv[0]);
        fprintf(stderr, "       %s write <image> <host-file> <path>\n", argv[0]);
        fprintf(stderr, "       %s exists <image> <path>\n", argv[0]);
        fprintf(stderr, "       %s ls <image> <path>\n", argv[0]);
        fprintf(stderr, "       %s info <image> <path>\n", argv[0]);
        fprintf(stderr, "       %s chmod <image> <mode> <path>\n", argv[0]);
        fprintf(stderr, "       %s chown <image> <uid> <gid> <path>\n", argv[0]);
        fprintf(stderr, "       %s setcap <image> <caps> <path>\n", argv[0]);
        fprintf(stderr, "       %s apply-caps <image> <policy>\n", argv[0]);
        fprintf(stderr, "       %s uuid <image>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "mkfs") == 0 && argc == 3) {
        mkfs_local(argv[2]);
        return 0;
    }
    if (strcmp(argv[1], "mkdir") == 0 && argc == 4) {
        mkdir_local(argv[2], argv[3]);
        return 0;
    }
    if (strcmp(argv[1], "write") == 0 && argc == 5) {
        write_file_local(argv[2], argv[3], argv[4]);
        return 0;
    }
    if (strcmp(argv[1], "exists") == 0 && argc == 4) {
        exists_local(argv[2], argv[3]);
        return 0;
    }
    if (strcmp(argv[1], "ls") == 0 && argc == 4) {
        list_local(argv[2], argv[3]);
        return 0;
    }
    if (strcmp(argv[1], "info") == 0 && argc == 4) {
        info_local(argv[2], argv[3]);
        return 0;
    }
    if (strcmp(argv[1], "chmod") == 0 && argc == 5) {
        chmod_local(argv[2], argv[3], argv[4]);
        return 0;
    }
    if (strcmp(argv[1], "chown") == 0 && argc == 6) {
        chown_local(argv[2], argv[3], argv[4], argv[5]);
        return 0;
    }
    if (strcmp(argv[1], "setcap") == 0 && argc == 5) {
        setcap_local(argv[2], argv[3], argv[4]);
        return 0;
    }
    if (strcmp(argv[1], "apply-caps") == 0 && argc == 4) {
        apply_caps_local(argv[2], argv[3]);
        return 0;
    }
    if (strcmp(argv[1], "uuid") == 0 && argc == 3) {
        uuid_local(argv[2]);
        return 0;
    }
    fprintf(stderr, "nxfs_host: bad command or arguments\n");
    return 1;
}
