#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs/nxfs.h"

enum {
    NXFS_HOST_TOTAL_BLOCKS = 131072
};

static FILE *g_disk = NULL;
static struct nxfs_super g_super;
static struct block_device g_host_bdev;

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
    uint8_t zero[NXFS_BLOCK_SIZE];
    struct nxfs_inode root;
    uint32_t bitmap_blocks;
    uint32_t inode_bytes;
    uint32_t inode_blocks;

    memset(zero, 0, sizeof(zero));
    memset(&root, 0, sizeof(root));

    g_disk = fopen(file, "wb+");
    if (g_disk == NULL) {
        perror("fopen");
        exit(1);
    }

    for (uint32_t i = 0; i < NXFS_HOST_TOTAL_BLOCKS; i++) {
        fwrite(zero, NXFS_BLOCK_SIZE, 1, g_disk);
    }

    inode_bytes = NXFS_MAX_INODES * sizeof(struct nxfs_inode);
    inode_blocks = (inode_bytes + NXFS_BLOCK_SIZE - 1u) / NXFS_BLOCK_SIZE;
    bitmap_blocks = (NXFS_HOST_TOTAL_BLOCKS + (NXFS_BLOCK_SIZE * 8u) - 1u) / (NXFS_BLOCK_SIZE * 8u);

    g_super.magic = NXFS_MAGIC;
    g_super.total_blocks = NXFS_HOST_TOTAL_BLOCKS;
    g_super.bitmap_start = 1;
    g_super.inode_start = g_super.bitmap_start + bitmap_blocks;
    g_super.data_start = g_super.inode_start + inode_blocks;

    fseek(g_disk, 0, SEEK_SET);
    fwrite(&g_super, sizeof(g_super), 1, g_disk);

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
    g_host_bdev.block_count = NXFS_HOST_TOTAL_BLOCKS;
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
    printf("ino=%u type=%u size=%u", ino, inode.type, inode.size);
    for (uint32_t i = 0; i < NXFS_EXTENTS; i++) {
        printf(" extent%u=%u:%u", i, inode.extents[i].start, inode.extents[i].len);
    }
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
    fprintf(stderr, "nxfs_host: bad command or arguments\n");
    return 1;
}
