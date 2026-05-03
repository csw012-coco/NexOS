#pragma once

#include <stdint.h>

enum {
    NXFS_EXTENTS = 8
};

struct nxfs_extent {
    uint32_t start;
    uint32_t len;
};

struct nxfs_inode {
    uint32_t used;
    uint32_t size;
    uint8_t type;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t nlink;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
    struct nxfs_extent extents[NXFS_EXTENTS];
} __attribute__((packed));
