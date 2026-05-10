#include "block/blockdev.h"
#include "block/block_event.h"

enum {
    BLOCKDEV_MAX = 16,
    BLOCKDEV_MBR_RETRIES = 8,
    BLOCKDEV_SECTOR_SIZE = 512u,
    BLOCKDEV_MBR_TABLE_OFFSET = 446u,
    BLOCKDEV_MBR_ENTRY_SIZE = 16u,
    BLOCKDEV_MBR_SLOT_COUNT = 4u,
    BLOCKDEV_GPT_HEADER_LBA = 1u,
    BLOCKDEV_GPT_MIN_HEADER_SIZE = 92u,
    BLOCKDEV_GPT_MIN_ENTRY_SIZE = 128u,
    BLOCKDEV_GPT_TYPE_UNUSED = 0x00u,
    BLOCKDEV_GPT_TYPE_EFI_SYSTEM = 0xefu,
    BLOCKDEV_GPT_TYPE_MICROSOFT_BASIC = 0x07u,
    BLOCKDEV_GPT_TYPE_LINUX_DATA = 0x83u,
    BLOCKDEV_GPT_TYPE_UNKNOWN = 0xeeu
};

static struct block_device *devices[BLOCKDEV_MAX];
static uint32_t device_count;

static const uint8_t gpt_guid_zero[16] = {
    0
};

static const uint8_t gpt_guid_efi_system[16] = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
};

static const uint8_t gpt_guid_microsoft_basic[16] = {
    0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
    0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7
};

static const uint8_t gpt_guid_linux_data[16] = {
    0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
    0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4
};

static uint32_t blockdev_read_u32le(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t blockdev_read_u64le(const uint8_t *data) {
    return (uint64_t)data[0] |
           ((uint64_t)data[1] << 8) |
           ((uint64_t)data[2] << 16) |
           ((uint64_t)data[3] << 24) |
           ((uint64_t)data[4] << 32) |
           ((uint64_t)data[5] << 40) |
           ((uint64_t)data[6] << 48) |
           ((uint64_t)data[7] << 56);
}

static int blockdev_guid_eq(const uint8_t *lhs, const uint8_t *rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < 16u; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static int blockdev_read_sector(struct block_device *dev, uint64_t lba, uint8_t *sector) {
    if (dev == 0 || sector == 0 || dev->block_size != BLOCKDEV_SECTOR_SIZE) {
        return -1;
    }
    for (uint32_t attempt = 0; attempt < BLOCKDEV_MBR_RETRIES; attempt++) {
        if (blockdev_read(dev, lba, 1, sector) == 0) {
            return 0;
        }
    }
    return -1;
}

static int blockdev_read_mbr(struct block_device *dev, uint8_t *sector) {
    if (blockdev_read_sector(dev, 0, sector) != 0) {
        return -1;
    }
    return sector[510] == 0x55u && sector[511] == 0xaau ? 0 : -1;
}

static int blockdev_mbr_has_gpt_protective_entry(const uint8_t *sector) {
    for (uint32_t slot = 0; slot < BLOCKDEV_MBR_SLOT_COUNT; slot++) {
        const uint8_t *entry = &sector[BLOCKDEV_MBR_TABLE_OFFSET + slot * BLOCKDEV_MBR_ENTRY_SIZE];

        if (entry[4] == 0xeeu && blockdev_read_u32le(entry + 12) != 0u) {
            return 1;
        }
    }
    return 0;
}

static uint8_t blockdev_gpt_type_from_guid(const uint8_t *guid) {
    if (blockdev_guid_eq(guid, gpt_guid_efi_system)) {
        return BLOCKDEV_GPT_TYPE_EFI_SYSTEM;
    }
    if (blockdev_guid_eq(guid, gpt_guid_microsoft_basic)) {
        return BLOCKDEV_GPT_TYPE_MICROSOFT_BASIC;
    }
    if (blockdev_guid_eq(guid, gpt_guid_linux_data)) {
        return BLOCKDEV_GPT_TYPE_LINUX_DATA;
    }
    return BLOCKDEV_GPT_TYPE_UNKNOWN;
}

static int blockdev_scan_gpt(struct block_device *dev, const uint8_t *mbr_sector) {
    uint8_t header[BLOCKDEV_SECTOR_SIZE];
    uint8_t entries[BLOCKDEV_SECTOR_SIZE];
    uint64_t entry_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t count = 0;

    if (dev == 0 || mbr_sector == 0 || dev->block_count <= BLOCKDEV_GPT_HEADER_LBA ||
        !blockdev_mbr_has_gpt_protective_entry(mbr_sector)) {
        return -1;
    }
    if (blockdev_read_sector(dev, BLOCKDEV_GPT_HEADER_LBA, header) != 0 ||
        header[0] != 'E' || header[1] != 'F' || header[2] != 'I' || header[3] != ' ' ||
        header[4] != 'P' || header[5] != 'A' || header[6] != 'R' || header[7] != 'T') {
        return -1;
    }

    if (blockdev_read_u32le(header + 12) < BLOCKDEV_GPT_MIN_HEADER_SIZE ||
        blockdev_read_u32le(header + 12) > BLOCKDEV_SECTOR_SIZE) {
        return -1;
    }

    entry_lba = blockdev_read_u64le(header + 72);
    entry_count = blockdev_read_u32le(header + 80);
    entry_size = blockdev_read_u32le(header + 84);
    if (entry_lba == 0 || entry_lba >= dev->block_count ||
        entry_count == 0u ||
        entry_size < BLOCKDEV_GPT_MIN_ENTRY_SIZE ||
        entry_size > BLOCKDEV_SECTOR_SIZE ||
        (BLOCKDEV_SECTOR_SIZE % entry_size) != 0u) {
        return -1;
    }

    for (uint32_t entry_index = 0; entry_index < entry_count && count < BLOCKDEV_MAX_PARTITIONS; entry_index++) {
        uint64_t byte_offset = (uint64_t)entry_index * (uint64_t)entry_size;
        uint64_t lba = entry_lba + byte_offset / BLOCKDEV_SECTOR_SIZE;
        uint32_t sector_offset = (uint32_t)(byte_offset % BLOCKDEV_SECTOR_SIZE);
        const uint8_t *entry;
        uint64_t first_lba;
        uint64_t last_lba;
        struct blockdev_partition part;

        if (lba >= dev->block_count || sector_offset + entry_size > BLOCKDEV_SECTOR_SIZE) {
            break;
        }
        if (blockdev_read_sector(dev, lba, entries) != 0) {
            return -1;
        }
        entry = entries + sector_offset;
        if (blockdev_guid_eq(entry, gpt_guid_zero)) {
            continue;
        }
        first_lba = blockdev_read_u64le(entry + 32);
        last_lba = blockdev_read_u64le(entry + 40);
        if (first_lba == 0 || last_lba < first_lba ||
            first_lba >= dev->block_count ||
            last_lba >= dev->block_count) {
            continue;
        }
        part.index = count;
        part.bootable = 0;
        part.type = blockdev_gpt_type_from_guid(entry);
        part.flags = 0;
        part.start_lba = first_lba;
        part.sector_count = last_lba - first_lba + 1u;
        dev->partitions[count++] = part;
    }

    dev->partition_count = count;
    dev->partition_cache_valid = 1u;
    return 0;
}

static int blockdev_scan_mbr(struct block_device *dev, const uint8_t *sector) {
    uint32_t count = 0;

    if (dev == 0) {
        return -1;
    }
    for (uint32_t slot = 0; slot < BLOCKDEV_MBR_SLOT_COUNT; slot++) {
        const uint8_t *entry = &sector[BLOCKDEV_MBR_TABLE_OFFSET + slot * BLOCKDEV_MBR_ENTRY_SIZE];
        struct blockdev_partition part;
        uint64_t start_lba;
        uint64_t sector_count;

        if (entry[4] == 0u || blockdev_read_u32le(entry + 12) == 0u) {
            continue;
        }
        if (entry[0] != 0x00u && entry[0] != 0x80u) {
            continue;
        }
        part.index = slot;
        part.bootable = entry[0] == 0x80u;
        part.type = entry[4];
        part.flags = 0;
        start_lba = blockdev_read_u32le(entry + 8);
        sector_count = blockdev_read_u32le(entry + 12);
        part.start_lba = start_lba;
        part.sector_count = sector_count;
        if (start_lba == 0u ||
            start_lba >= dev->block_count ||
            sector_count > dev->block_count - start_lba) {
            continue;
        }
        dev->partitions[count++] = part;
    }
    dev->partition_count = count;
    dev->partition_cache_valid = 1u;
    return 0;
}

int blockdev_rescan_partitions(struct block_device *dev) {
    uint8_t sector[BLOCKDEV_SECTOR_SIZE];

    if (dev == 0) {
        return -1;
    }
    dev->partition_count = 0;
    dev->partition_cache_valid = 0u;
    if (blockdev_read_mbr(dev, sector) != 0) {
        return -1;
    }
    if (blockdev_scan_gpt(dev, sector) == 0) {
        return 0;
    }
    return blockdev_scan_mbr(dev, sector);
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
    if (!dev->partition_cache_valid) {
        (void)blockdev_rescan_partitions(dev);
    }
    return dev->partition_count;
}

int blockdev_partition_get(struct block_device *dev, uint32_t index, struct blockdev_partition *out) {
    if (dev == 0 || out == 0) {
        return -1;
    }
    if (!dev->partition_cache_valid) {
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
