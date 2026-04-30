#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs/nxfs.h"

enum {
    NXFS_HOST_TOTAL_BLOCKS = 1024
};

static FILE *g_disk = NULL;
static struct nxfs_super g_super;

static void write_block_local(uint32_t block, const void *buffer) {
    fseek(g_disk, (long)(block * NXFS_BLOCK_SIZE), SEEK_SET);
    fwrite(buffer, NXFS_BLOCK_SIZE, 1, g_disk);
}

static void write_inode_local(uint32_t inode_index, const struct nxfs_inode *inode) {
    uint8_t block[NXFS_BLOCK_SIZE];
    uint32_t inode_offset;
    uint32_t block_index;
    uint32_t offset_in_block;

    inode_offset = g_super.inode_start * NXFS_BLOCK_SIZE + inode_index * sizeof(*inode);
    block_index = inode_offset / NXFS_BLOCK_SIZE;
    offset_in_block = inode_offset % NXFS_BLOCK_SIZE;

    fseek(g_disk, (long)(block_index * NXFS_BLOCK_SIZE), SEEK_SET);
    fread(block, NXFS_BLOCK_SIZE, 1, g_disk);
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

static void write_bitmap_local(const uint8_t *bitmap) {
    write_block_local(g_super.bitmap_start, bitmap);
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
    uint8_t bitmap[NXFS_BLOCK_SIZE];
    struct nxfs_inode root;
    uint32_t inode_bytes;
    uint32_t inode_blocks;

    memset(zero, 0, sizeof(zero));
    memset(bitmap, 0, sizeof(bitmap));
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

    g_super.magic = NXFS_MAGIC;
    g_super.total_blocks = NXFS_HOST_TOTAL_BLOCKS;
    g_super.bitmap_start = 1;
    g_super.inode_start = 2;
    g_super.data_start = g_super.inode_start + inode_blocks;

    fseek(g_disk, 0, SEEK_SET);
    fwrite(&g_super, sizeof(g_super), 1, g_disk);

    for (uint32_t b = 0; b < g_super.data_start; b++) {
        bitmap_set_local(bitmap, b, 1);
    }
    bitmap_set_local(bitmap, g_super.data_start, 1);
    write_bitmap_local(bitmap);

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

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s mkfs <image>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "mkfs") != 0) {
        fprintf(stderr, "unknown command: %s\n", argv[1]);
        return 1;
    }

    mkfs_local(argv[2]);
    return 0;
}
