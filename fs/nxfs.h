#pragma once

#include <stdint.h>
#include "block/blockdev.h"
#include "kernel/public/fs/nxfs_types.h"

enum {
    NXFS_BLOCK_SIZE = 512,
    NXFS_MAX_INODES = 128,
    NXFS_MAGIC = 0x4e584653u,
    NXFS_TYPE_FILE = 1,
    NXFS_TYPE_DIR = 2
};

struct nxfs_super {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t bitmap_start;
    uint32_t inode_start;
    uint32_t data_start;
} __attribute__((packed));

struct nxfs_dir_entry {
    uint32_t inode;
    char name[28];
} __attribute__((packed));

struct nxfs_volume {
    struct block_device *bdev;
    uint32_t partition_lba;
    struct nxfs_super super;
    uint8_t mounted;
};

int nxfs_mount(struct nxfs_volume *vol, struct block_device *bdev, uint32_t partition_lba);
int nxfs_read_inode(struct nxfs_volume *vol, uint32_t inode_index, struct nxfs_inode *out);
int nxfs_lookup_root(struct nxfs_volume *vol, const char *name, uint32_t *inode_index, struct nxfs_inode *out);
int nxfs_lookup_path(struct nxfs_volume *vol, const char *path, uint32_t *inode_index, struct nxfs_inode *out);
int nxfs_list_root(struct nxfs_volume *vol,
                   struct nxfs_dir_entry *entries,
                   uint32_t max_entries,
                   uint32_t *entry_count);
int nxfs_list_dir(struct nxfs_volume *vol,
                  uint32_t inode_index,
                  const struct nxfs_inode *dir,
                  struct nxfs_dir_entry *entries,
                  uint32_t max_entries,
                  uint32_t *entry_count);
int nxfs_get_dir_entry(struct nxfs_volume *vol,
                       uint32_t inode_index,
                       const struct nxfs_inode *dir,
                       uint32_t entry_index,
                       struct nxfs_dir_entry *entry_out);
int nxfs_read_file(struct nxfs_volume *vol,
                   const struct nxfs_inode *inode,
                   void *buffer,
                   uint32_t buffer_size,
                   uint32_t *bytes_read);
int nxfs_create_root(struct nxfs_volume *vol, const char *name, uint32_t *inode_index, struct nxfs_inode *out);
int nxfs_create_path(struct nxfs_volume *vol, const char *path, uint32_t *inode_index, struct nxfs_inode *out);
int nxfs_mkdir_path(struct nxfs_volume *vol, const char *path, uint32_t *inode_index, struct nxfs_inode *out);
int nxfs_truncate_inode(struct nxfs_volume *vol, uint32_t inode_index, struct nxfs_inode *inode);
int nxfs_truncate_path(struct nxfs_volume *vol, const char *path);
int nxfs_write_file_range(struct nxfs_volume *vol,
                          uint32_t inode_index,
                          struct nxfs_inode *inode,
                          uint32_t offset,
                          const void *buffer,
                          uint32_t buffer_size,
                          uint32_t *bytes_written);
int nxfs_unlink_root(struct nxfs_volume *vol, const char *name);
int nxfs_unlink_path(struct nxfs_volume *vol, const char *path);
int nxfs_rmdir_path(struct nxfs_volume *vol, const char *path);
int nxfs_read_file_range(struct nxfs_volume *vol,
                         const struct nxfs_inode *inode,
                         uint32_t offset,
                         void *buffer,
                         uint32_t buffer_size,
                         uint32_t *bytes_read);
