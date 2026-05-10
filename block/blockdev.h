#pragma once

#include <stddef.h>
#include <stdint.h>

struct block_device;

enum {
    BLOCKDEV_MAX_PARTITIONS = 128u
};

typedef int (*blockdev_read_fn)(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
typedef int (*blockdev_write_fn)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);

struct blockdev_info {
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    uint8_t writable;
};

struct blockdev_partition {
    uint32_t index;
    uint8_t bootable;
    uint8_t type;
    uint16_t flags;
    uint64_t start_lba;
    uint64_t sector_count;
};

struct block_device {
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    uint32_t partition_count;
    uint8_t partition_cache_valid;
    struct blockdev_partition partitions[BLOCKDEV_MAX_PARTITIONS];
    blockdev_read_fn read;
    blockdev_write_fn write;
    void *driver_data;
};

void blockdev_init(void);
int blockdev_register(struct block_device *dev);
struct block_device *blockdev_get(uint32_t index);
uint32_t blockdev_count(void);
int blockdev_get_info(uint32_t index, struct blockdev_info *out);
int blockdev_rescan_partitions(struct block_device *dev);
uint32_t blockdev_partition_count(struct block_device *dev);
int blockdev_partition_get(struct block_device *dev, uint32_t index, struct blockdev_partition *out);
int blockdev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
int blockdev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
