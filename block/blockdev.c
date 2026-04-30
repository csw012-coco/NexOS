#include "block/blockdev.h"
#include "block/block_event.h"

enum {
    BLOCKDEV_MAX = 8,
    BLOCKDEV_MBR_RETRIES = 8
};

static struct block_device *devices[BLOCKDEV_MAX];
static uint32_t device_count;

static uint32_t blockdev_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int blockdev_read_mbr(struct block_device *dev, uint8_t *sector) {
    if (dev == 0 || sector == 0 || dev->block_size != 512) {
        return -1;
    }
    for (uint32_t attempt = 0; attempt < BLOCKDEV_MBR_RETRIES; attempt++) {
        if (blockdev_read(dev, 0, 1, sector) == 0) {
            break;
        }
        if (attempt + 1u == BLOCKDEV_MBR_RETRIES) {
            return -1;
        }
    }
    if (sector[510] != 0x55 || sector[511] != 0xaa) {
        return -1;
    }
    return 0;
}

int blockdev_rescan_partitions(struct block_device *dev) {
    uint8_t sector[512];
    uint32_t count = 0;

    if (dev == 0) {
        return -1;
    }
    if (blockdev_read_mbr(dev, sector) != 0) {
        return -1;
    }
    for (uint32_t slot = 0; slot < 4u; slot++) {
        const uint8_t *entry = &sector[446 + slot * 16u];
        struct blockdev_partition part;

        if (entry[4] == 0u || blockdev_read_u32le(entry + 12) == 0u) {
            continue;
        }
        if (entry[0] != 0x00u && entry[0] != 0x80u) {
            continue;
        }
        part.index = slot;
        part.bootable = entry[0] == 0x80u;
        part.type = entry[4];
        part.start_lba = blockdev_read_u32le(entry + 8);
        part.sector_count = blockdev_read_u32le(entry + 12);
        if (part.start_lba == 0u ||
            (uint64_t)part.start_lba >= dev->block_count ||
            (uint64_t)part.sector_count > dev->block_count - (uint64_t)part.start_lba) {
            continue;
        }
        dev->partitions[count++] = part;
    }
    dev->partition_count = count;
    dev->partition_cache_valid = 1u;
    return 0;
}

void blockdev_init(void) {
    device_count = 0;
    for (uint32_t i = 0; i < BLOCKDEV_MAX; i++) {
        devices[i] = 0;
    }
}

int blockdev_register(struct block_device *dev) {
    uint32_t disk_index;

    if (dev == 0 || dev->read == 0 || device_count >= BLOCKDEV_MAX) {
        return -1;
    }

    dev->partition_count = 0;
    dev->partition_cache_valid = 0u;
    disk_index = device_count;
    devices[device_count++] = dev;
    (void)blockdev_rescan_partitions(dev);
    block_event_emit_change("add", disk_index, 0xffffffffu, dev->name, dev->block_count);
    for (uint32_t i = 0; i < dev->partition_count; i++) {
        struct blockdev_partition part = dev->partitions[i];

        block_event_emit_change("partition", disk_index, part.index, dev->name, part.sector_count);
    }
    return 0;
}

struct block_device *blockdev_get(uint32_t index) {
    if (index >= device_count) {
        return 0;
    }
    return devices[index];
}

uint32_t blockdev_count(void) {
    return device_count;
}

int blockdev_get_info(uint32_t index, struct blockdev_info *out) {
    struct block_device *dev = blockdev_get(index);

    if (dev == 0 || out == 0) {
        return -1;
    }
    out->name = dev->name;
    out->block_size = dev->block_size;
    out->block_count = dev->block_count;
    out->writable = dev->write != 0;
    return 0;
}

uint32_t blockdev_partition_count(struct block_device *dev) {
    if (dev == 0) {
        return 0;
    }
    if (!dev->partition_cache_valid || dev->partition_count == 0u) {
        (void)blockdev_rescan_partitions(dev);
    }
    return dev->partition_count;
}

int blockdev_partition_get(struct block_device *dev, uint32_t index, struct blockdev_partition *out) {
    if (dev == 0 || out == 0) {
        return -1;
    }
    if (!dev->partition_cache_valid || dev->partition_count == 0u) {
        (void)blockdev_rescan_partitions(dev);
    }
    if (index >= dev->partition_count) {
        return -1;
    }
    *out = dev->partitions[index];
    return 0;
}

int blockdev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (dev == 0 || dev->read == 0 || buffer == 0 || count == 0) {
        return -1;
    }
    return dev->read(dev, lba, count, buffer);
}

int blockdev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    int rc;

    if (dev == 0 || dev->write == 0 || buffer == 0 || count == 0) {
        return -1;
    }
    rc = dev->write(dev, lba, count, buffer);
    if (rc == 0 && lba == 0u) {
        (void)blockdev_rescan_partitions(dev);
    }
    return rc;
}
