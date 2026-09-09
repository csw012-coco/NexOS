#include "block/blockdev.h"
#include "block/block_event.h"
#include "hal/hal.h"
#include "kernel/public/core/profile.h"
#include "lib/string.h"

enum {
    BLOCKDEV_MAX = 16,
    BLOCKDEV_READ_CACHE_ENTRIES = 1024,
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
    BLOCKDEV_GPT_TYPE_UNKNOWN = 0xeeu,
    BLOCKDEV_IO_TIMEOUT_TICKS = 5000u
};

static struct block_device *devices[BLOCKDEV_MAX];
static uint32_t device_count;
static uint32_t g_block_profile_read;
static uint32_t g_block_profile_write;
static uint32_t g_block_profile_flush;
static volatile uint32_t g_blockdev_lock;
static volatile uint32_t g_blockdev_scan_lock;
static struct blockdev_partition g_blockdev_scan_partitions[BLOCKDEV_MAX_PARTITIONS];

struct blockdev_read_cache_entry {
    struct block_device *dev;
    uint64_t lba;
    uint8_t valid;
    uint8_t data[BLOCKDEV_SECTOR_SIZE];
};

static struct blockdev_read_cache_entry
    g_block_read_cache[BLOCKDEV_READ_CACHE_ENTRIES];

static void blockdev_lock(void) {
    while (__sync_lock_test_and_set(&g_blockdev_lock, 1u) != 0u) {
        __asm__ __volatile__("pause");
    }
}

static void blockdev_unlock(void) {
    __sync_lock_release(&g_blockdev_lock);
}

static void blockdev_scan_lock(void) {
    while (__sync_lock_test_and_set(&g_blockdev_scan_lock, 1u) != 0u) {
        __asm__ __volatile__("pause");
    }
}

static void blockdev_scan_unlock(void) {
    __sync_lock_release(&g_blockdev_scan_lock);
}

static void blockdev_request_lock(struct block_device *dev) {
    while (__sync_lock_test_and_set(&dev->request_lock, 1u) != 0u) {
        __asm__ __volatile__("pause");
    }
}

static void blockdev_request_unlock(struct block_device *dev) {
    __sync_lock_release(&dev->request_lock);
}

static uint32_t blockdev_ms_to_ticks(uint32_t ms) {
    uint32_t hz = hal_timer_hz();
    uint64_t ticks;

    if (ms == 0u) {
        return 0u;
    }
    if (hz == 0u) {
        return ms;
    }
    ticks = ((uint64_t)hz * (uint64_t)ms + 999u) / 1000u;
    if (ticks == 0u) {
        ticks = 1u;
    }
    if (ticks > 0xffffffffull) {
        ticks = 0xffffffffu;
    }
    return (uint32_t)ticks;
}

/* Return 1 when a registered device reference was acquired, 0 for an
 * internal, not-yet-published device, and -1 when removal is in progress. */
static int blockdev_io_begin(struct block_device *dev) {
    int registered = 0;

    if (dev == 0) {
        return -1;
    }
    blockdev_lock();
    for (uint32_t i = 0u; i < device_count; i++) {
        if (devices[i] != dev) {
            continue;
        }
        registered = 1;
        if (dev->removing == 0u && dev->state == BLOCKDEV_STATE_ONLINE) {
            dev->io_refs++;
            dev->request_cancelled = 0u;
            dev->request_deadline = hal_timer_current_ticks() + BLOCKDEV_IO_TIMEOUT_TICKS;
        }
        break;
    }
    if (registered && (dev->removing != 0u || dev->state != BLOCKDEV_STATE_ONLINE)) {
        blockdev_unlock();
        return -1;
    }
    blockdev_unlock();
    return registered ? 1 : 0;
}

void blockdev_cancel_request(struct block_device *dev) {
    if (dev == 0) {
        return;
    }
    __sync_lock_test_and_set(&dev->request_cancelled, 1u);
}

void blockdev_extend_request_deadline_ms(struct block_device *dev, uint32_t ms) {
    uint32_t ticks;
    uint32_t deadline;

    if (dev == 0 || ms == 0u) {
        return;
    }
    ticks = blockdev_ms_to_ticks(ms);
    deadline = hal_timer_current_ticks() + ticks;
    blockdev_lock();
    if (dev->request_deadline == 0u ||
        (int32_t)(deadline - dev->request_deadline) > 0) {
        dev->request_deadline = deadline;
    }
    blockdev_unlock();
}

int blockdev_request_cancelled(const struct block_device *dev) {
    uint32_t now;
    uint32_t deadline;

    if (dev == 0 || dev->request_cancelled != 0u) {
        return 1;
    }
    deadline = dev->request_deadline;
    if (deadline == 0u) {
        return 0;
    }
    now = hal_timer_current_ticks();
    return (int32_t)(now - deadline) >= 0;
}

void blockdev_record_failure(struct block_device *dev, int error, const char *reason) {
    if (dev == 0) {
        return;
    }
    blockdev_lock();
    dev->failure_count++;
    dev->consecutive_failures++;
    dev->last_error = error;
    if (reason != 0 && reason[0] != '\0') {
        memcpy(dev->last_error_reason, reason, sizeof(dev->last_error_reason));
        dev->last_error_reason[sizeof(dev->last_error_reason) - 1u] = '\0';
    }
    blockdev_unlock();
}

void blockdev_record_success(struct block_device *dev) {
    if (dev == 0) {
        return;
    }
    blockdev_lock();
    dev->consecutive_failures = 0u;
    blockdev_unlock();
}

void blockdev_record_rebind(struct block_device *dev, const char *reason) {
    if (dev == 0) {
        return;
    }
    blockdev_lock();
    dev->rebind_count++;
    if (reason != 0 && reason[0] != '\0') {
        memcpy(dev->last_rebind_reason, reason, sizeof(dev->last_rebind_reason));
        dev->last_rebind_reason[sizeof(dev->last_rebind_reason) - 1u] = '\0';
    }
    blockdev_unlock();
}

static void blockdev_io_end(struct block_device *dev, int acquired) {
    if (dev == 0 || acquired != 1) {
        return;
    }
    blockdev_lock();
    if (dev->io_refs != 0u) {
        dev->io_refs--;
    }
    blockdev_unlock();
}

static uint32_t blockdev_cache_index(struct block_device *dev, uint64_t lba) {
    uintptr_t device_bits = (uintptr_t)dev >> 4;
    uint64_t mixed = lba ^ (lba >> 17) ^ (uint64_t)device_bits;

    return (uint32_t)(mixed % BLOCKDEV_READ_CACHE_ENTRIES);
}

static struct blockdev_read_cache_entry *blockdev_cache_entry(
    struct block_device *dev,
    uint64_t lba) {
    return &g_block_read_cache[blockdev_cache_index(dev, lba)];
}

static void blockdev_cache_invalidate_device(struct block_device *dev) {
    for (uint32_t i = 0u; i < BLOCKDEV_READ_CACHE_ENTRIES; i++) {
        if (g_block_read_cache[i].valid && g_block_read_cache[i].dev == dev) {
            g_block_read_cache[i].valid = 0u;
        }
    }
}

static void blockdev_cache_invalidate_range(struct block_device *dev,
                                            uint64_t lba,
                                            uint32_t count) {
    uint64_t end = lba + count;

    if (end < lba) {
        blockdev_cache_invalidate_device(dev);
        return;
    }
    for (uint32_t i = 0u; i < BLOCKDEV_READ_CACHE_ENTRIES; i++) {
        struct blockdev_read_cache_entry *entry = &g_block_read_cache[i];

        if (entry->valid && entry->dev == dev &&
            entry->lba >= lba && entry->lba < end) {
            entry->valid = 0u;
        }
    }
}

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

static int blockdev_scan_gpt(struct block_device *dev,
                             const uint8_t *mbr_sector,
                             struct blockdev_partition *parts,
                             uint32_t *out_count) {
    uint8_t header[BLOCKDEV_SECTOR_SIZE];
    uint8_t entries[BLOCKDEV_SECTOR_SIZE];
    uint64_t entry_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t count = 0;

    if (dev == 0 || mbr_sector == 0 || parts == 0 || out_count == 0 ||
        dev->block_count <= BLOCKDEV_GPT_HEADER_LBA ||
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
        parts[count++] = part;
    }

    *out_count = count;
    return 0;
}

static int blockdev_scan_mbr(struct block_device *dev,
                             const uint8_t *sector,
                             struct blockdev_partition *parts,
                             uint32_t *out_count) {
    uint32_t count = 0;

    if (dev == 0 || sector == 0 || parts == 0 || out_count == 0) {
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
        parts[count++] = part;
    }
    *out_count = count;
    return 0;
}

int blockdev_rescan_partitions(struct block_device *dev) {
    uint8_t sector[BLOCKDEV_SECTOR_SIZE];
    uint32_t count = 0u;
    int rc;
    int acquired;

    if (dev == 0) {
        return -1;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0) {
        return -1;
    }
    blockdev_scan_lock();
    blockdev_lock();
    dev->partition_count = 0;
    dev->partition_cache_valid = 0u;
    blockdev_unlock();
    if (blockdev_read_sector(dev, 0u, sector) != 0) {
        blockdev_scan_unlock();
        blockdev_io_end(dev, acquired);
        return -1;
    }
    /* A filesystem may occupy the whole device without an MBR.  Keep an
     * empty, valid partition cache so callers can still probe LBA 0. */
    if (sector[510] != 0x55u || sector[511] != 0xaau) {
        blockdev_lock();
        dev->partition_count = 0u;
        dev->partition_cache_valid = 1u;
        blockdev_unlock();
        blockdev_scan_unlock();
        blockdev_io_end(dev, acquired);
        return 0;
    }
    rc = blockdev_scan_gpt(dev, sector, g_blockdev_scan_partitions, &count);
    if (rc != 0) {
        rc = blockdev_scan_mbr(dev, sector, g_blockdev_scan_partitions, &count);
    }
    if (rc == 0) {
        blockdev_lock();
        memcpy(dev->partitions,
               g_blockdev_scan_partitions,
               count * sizeof(g_blockdev_scan_partitions[0]));
        dev->partition_count = count;
        dev->partition_cache_valid = 1u;
        blockdev_unlock();
    }
    blockdev_scan_unlock();
    blockdev_io_end(dev, acquired);
    return rc;
}

void blockdev_init(void) {
    g_blockdev_lock = 0u;
    g_blockdev_scan_lock = 0u;
    device_count = 0;
    for (uint32_t i = 0; i < BLOCKDEV_MAX; i++) {
        devices[i] = 0;
    }
    for (uint32_t i = 0; i < BLOCKDEV_READ_CACHE_ENTRIES; i++) {
        g_block_read_cache[i].valid = 0u;
        g_block_read_cache[i].dev = 0;
        g_block_read_cache[i].lba = 0u;
    }
}

enum blockdev_state blockdev_get_state(const struct block_device *dev) {
    enum blockdev_state state;

    if (dev == 0) {
        return BLOCKDEV_STATE_OFFLINE;
    }
    blockdev_lock();
    state = (enum blockdev_state)dev->state;
    blockdev_unlock();
    return state;
}

int blockdev_set_state(struct block_device *dev, enum blockdev_state state) {
    if (dev == 0 || state > BLOCKDEV_STATE_REMOVING) {
        return -1;
    }
    blockdev_lock();
    if (dev->rootfs_protected &&
        (state == BLOCKDEV_STATE_OFFLINE || state == BLOCKDEV_STATE_REMOVING)) {
        if (dev->state == BLOCKDEV_STATE_OFFLINE || dev->state == BLOCKDEV_STATE_REMOVING) {
            dev->state = BLOCKDEV_STATE_ONLINE;
        }
        dev->removing = 0u;
        dev->request_cancelled = 0u;
        blockdev_unlock();
        return -1;
    }
    dev->state = (uint8_t)state;
    if (state == BLOCKDEV_STATE_REMOVING) {
        dev->removing = 1u;
    }
    blockdev_unlock();
    return 0;
}

void blockdev_set_rootfs_protected(struct block_device *dev, uint8_t protected) {
    if (dev == 0) {
        return;
    }
    blockdev_lock();
    dev->rootfs_protected = protected != 0u ? 1u : 0u;
    if (dev->rootfs_protected &&
        (dev->state == BLOCKDEV_STATE_OFFLINE || dev->state == BLOCKDEV_STATE_REMOVING)) {
        dev->state = BLOCKDEV_STATE_ONLINE;
        dev->removing = 0u;
        dev->request_cancelled = 0u;
    }
    blockdev_unlock();
}

int blockdev_is_rootfs_protected(const struct block_device *dev) {
    int protected;

    if (dev == 0) {
        return 0;
    }
    blockdev_lock();
    protected = dev->rootfs_protected != 0u;
    blockdev_unlock();
    return protected;
}

int blockdev_reset(struct block_device *dev) {
    blockdev_reset_fn reset = 0;
    int registered = 0;
    int rc;

    if (dev == 0) {
        return -1;
    }
    blockdev_lock();
    for (uint32_t i = 0u; i < device_count; i++) {
        if (devices[i] != dev) {
            continue;
        }
        registered = 1;
        if (dev->removing == 0u && dev->state == BLOCKDEV_STATE_ONLINE) {
            reset = dev->reset;
            if (reset != 0) {
                dev->io_refs++;
                dev->state = BLOCKDEV_STATE_RECOVERING;
                dev->request_cancelled = 0u;
                dev->request_deadline = hal_timer_current_ticks() + BLOCKDEV_IO_TIMEOUT_TICKS;
            }
        }
        break;
    }
    blockdev_unlock();
    if (!registered || reset == 0) {
        return -1;
    }
    blockdev_request_lock(dev);
    rc = reset(dev);
    blockdev_request_unlock(dev);
    if (rc == 0) {
        blockdev_record_success(dev);
    } else {
        blockdev_record_failure(dev, rc, "reset");
    }
    blockdev_lock();
    if (dev->removing == 0u) {
        dev->state = (rc == 0 || dev->rootfs_protected)
            ? BLOCKDEV_STATE_ONLINE
            : BLOCKDEV_STATE_OFFLINE;
    }
    blockdev_unlock();
    blockdev_release(dev);
    return rc;
}

int blockdev_register(struct block_device *dev) {
    uint32_t disk_index;
    int scan_rc;

    if (dev == 0 || dev->read == 0) {
        return -1;
    }

    /* Do not publish a device until its partition metadata is stable.  MSC
     * devices may need several BOT requests here, and query clients must not
     * observe the half-initialized registration. */
    blockdev_lock();
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i] == dev) {
            blockdev_unlock();
            return 0;
        }
    }
    disk_index = BLOCKDEV_MAX;
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i] == 0) {
            disk_index = i;
            break;
        }
    }
    if (disk_index == BLOCKDEV_MAX) {
        if (device_count >= BLOCKDEV_MAX) {
            blockdev_unlock();
            return -1;
        }
        disk_index = device_count;
    }
    blockdev_unlock();

    dev->partition_count = 0u;
    dev->partition_cache_valid = 0u;
    dev->io_refs = 0u;
    dev->request_lock = 0u;
    dev->failure_count = 0u;
    dev->consecutive_failures = 0u;
    dev->rebind_count = 0u;
    dev->last_error = 0;
    memset(dev->last_error_reason, 0, sizeof(dev->last_error_reason));
    memset(dev->last_rebind_reason, 0, sizeof(dev->last_rebind_reason));
    dev->rootfs_protected = 0u;
    dev->request_cancelled = 1u;
    dev->request_deadline = 0u;
    dev->removing = 0u;
    dev->state = BLOCKDEV_STATE_PROBING;
    scan_rc = blockdev_rescan_partitions(dev);

    blockdev_lock();
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i] == dev) {
            blockdev_unlock();
            return 0;
        }
    }
    disk_index = BLOCKDEV_MAX;
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i] == 0) {
            disk_index = i;
            break;
        }
    }
    if (disk_index == BLOCKDEV_MAX) {
        if (device_count >= BLOCKDEV_MAX) {
            blockdev_unlock();
            return -1;
        }
        disk_index = device_count++;
    }
    dev->removing = 0u;
    dev->state = BLOCKDEV_STATE_ONLINE;
    devices[disk_index] = dev;
    blockdev_unlock();

    (void)scan_rc;
    block_event_emit_change("add", disk_index, 0xffffffffu, dev->name, dev->block_count);
    for (uint32_t i = 0; i < dev->partition_count; i++) {
        struct blockdev_partition part = dev->partitions[i];

        block_event_emit_change("partition", disk_index, part.index, dev->name, part.sector_count);
    }
    return 0;
}

int blockdev_unregister(struct block_device *dev) {
    uint32_t disk_index = BLOCKDEV_MAX;

    if (dev == 0) {
        return -1;
    }
    blockdev_lock();
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i] == dev) {
            disk_index = i;
            break;
        }
    }
    if (disk_index == BLOCKDEV_MAX) {
        blockdev_unlock();
        return -1;
    }
    if (dev->rootfs_protected) {
        dev->removing = 0u;
        if (dev->state == BLOCKDEV_STATE_OFFLINE || dev->state == BLOCKDEV_STATE_REMOVING) {
            dev->state = BLOCKDEV_STATE_ONLINE;
        }
        dev->request_cancelled = 0u;
        blockdev_unlock();
        return -1;
    }
    dev->removing = 1u;
    dev->state = BLOCKDEV_STATE_REMOVING;
    dev->request_cancelled = 1u;
    blockdev_unlock();

    /*
     * Removal can be called from a controller hotplug poll while the
     * controller serialization lock is held.  Waiting here for an active
     * request would deadlock that request: it needs the same lock to finish.
     * Leave the device published but REMOVING and let the next poll retry.
     */
    blockdev_lock();
    if (dev->io_refs != 0u) {
        blockdev_unlock();
        return -1;
    }
    blockdev_unlock();

    blockdev_lock();
    disk_index = BLOCKDEV_MAX;
    for (uint32_t i = 0u; i < device_count; i++) {
        if (devices[i] == dev) {
            disk_index = i;
            break;
        }
    }
    if (disk_index == BLOCKDEV_MAX) {
        blockdev_unlock();
        return -1;
    }
    blockdev_cache_invalidate_device(dev);
    for (uint32_t i = 0; i < dev->partition_count; i++) {
        struct blockdev_partition part = dev->partitions[i];

        block_event_emit_change("remove-partition", disk_index, part.index, dev->name, part.sector_count);
    }
    block_event_emit_change("remove", disk_index, 0xffffffffu, dev->name, dev->block_count);
    devices[disk_index] = 0;
    dev->io_refs = 0u;
    dev->partition_count = 0;
    dev->partition_cache_valid = 0u;
    while (device_count > 0u && devices[device_count - 1u] == 0) {
        device_count--;
    }
    blockdev_unlock();
    return 0;
}

struct block_device *blockdev_get(uint32_t index) {
    struct block_device *dev;

    blockdev_lock();
    if (index >= device_count) {
        blockdev_unlock();
        return 0;
    }
    dev = devices[index];
    blockdev_unlock();
    return dev;
}

struct block_device *blockdev_acquire(uint32_t index) {
    struct block_device *dev = 0;

    blockdev_lock();
    if (index < device_count) {
        dev = devices[index];
        if (dev == 0 || dev->removing != 0u ||
            dev->state != BLOCKDEV_STATE_ONLINE) {
            dev = 0;
        } else {
            dev->io_refs++;
        }
    }
    blockdev_unlock();
    return dev;
}

int blockdev_acquire_device(struct block_device *dev) {
    int found = 0;

    if (dev == 0) {
        return -1;
    }
    blockdev_lock();
    for (uint32_t i = 0u; i < device_count; i++) {
        if (devices[i] != dev) {
            continue;
        }
        found = 1;
        if (dev->removing == 0u && dev->state == BLOCKDEV_STATE_ONLINE) {
            dev->io_refs++;
        } else {
            found = 0;
        }
        break;
    }
    blockdev_unlock();
    return found ? 0 : -1;
}

void blockdev_release(struct block_device *dev) {
    if (dev == 0) {
        return;
    }
    blockdev_lock();
    if (dev->io_refs != 0u) {
        dev->io_refs--;
    }
    blockdev_unlock();
}

uint32_t blockdev_count(void) {
    uint32_t count;

    blockdev_lock();
    count = device_count;
    blockdev_unlock();
    return count;
}

int blockdev_get_info(uint32_t index, struct blockdev_info *out) {
    struct block_device *dev;

    if (out == 0) {
        return -1;
    }
    blockdev_lock();
    dev = index < device_count ? devices[index] : 0;
    if (dev == 0 || dev->removing != 0u) {
        dev = 0;
    } else {
        dev->io_refs++;
    }
    blockdev_unlock();
    if (dev == 0) {
        return -1;
    }
    blockdev_lock();
    memcpy(out->name, dev->name, sizeof(out->name));
    out->name[sizeof(out->name) - 1u] = '\0';
    out->block_size = dev->block_size;
    out->block_count = dev->block_count;
    out->writable = dev->write != 0;
    out->partition_count = dev->partition_cache_valid ? dev->partition_count : 0u;
    out->failure_count = dev->failure_count;
    out->consecutive_failures = dev->consecutive_failures;
    out->rebind_count = dev->rebind_count;
    out->last_error = dev->last_error;
    memcpy(out->last_error_reason,
           dev->last_error_reason,
           sizeof(out->last_error_reason));
    memcpy(out->last_rebind_reason,
           dev->last_rebind_reason,
           sizeof(out->last_rebind_reason));
    blockdev_unlock();
    blockdev_io_end(dev, 1);
    return 0;
}

int blockdev_get_partition_info(uint32_t disk_index,
                                uint32_t slot,
                                struct blockdev_partition *out) {
    struct block_device *dev;

    if (out == 0) {
        return -1;
    }
    blockdev_lock();
    dev = disk_index < device_count ? devices[disk_index] : 0;
    if (dev == 0 || dev->removing != 0u || !dev->partition_cache_valid ||
        slot >= dev->partition_count) {
        blockdev_unlock();
        return -1;
    }
    dev->io_refs++;
    *out = dev->partitions[slot];
    dev->io_refs--;
    blockdev_unlock();
    return 0;
}

uint32_t blockdev_partition_count(struct block_device *dev) {
    int acquired;

    if (dev == 0) {
        return 0;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0) {
        return 0;
    }
    blockdev_lock();
    if (!dev->partition_cache_valid) {
        blockdev_unlock();
        blockdev_io_end(dev, acquired);
        (void)blockdev_rescan_partitions(dev);
        acquired = blockdev_io_begin(dev);
        if (acquired < 0) {
            return 0;
        }
        blockdev_lock();
    }
    {
        uint32_t count = dev->partition_count;
        blockdev_unlock();
        blockdev_io_end(dev, acquired);
        return count;
    }
}

int blockdev_partition_get(struct block_device *dev, uint32_t index, struct blockdev_partition *out) {
    int acquired;

    if (dev == 0 || out == 0) {
        return -1;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0) {
        return -1;
    }
    blockdev_lock();
    if (!dev->partition_cache_valid) {
        blockdev_unlock();
        blockdev_io_end(dev, acquired);
        (void)blockdev_rescan_partitions(dev);
        acquired = blockdev_io_begin(dev);
        if (acquired < 0) {
            return -1;
        }
        blockdev_lock();
    }
    if (index >= dev->partition_count) {
        blockdev_unlock();
        blockdev_io_end(dev, acquired);
        return -1;
    }
    *out = dev->partitions[index];
    blockdev_unlock();
    blockdev_io_end(dev, acquired);
    return 0;
}

uint32_t blockdev_partition_count_cached(struct block_device *dev) {
    uint32_t count;
    int acquired;

    if (dev == 0) {
        return 0;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0) {
        return 0;
    }
    blockdev_lock();
    count = dev->partition_cache_valid ? dev->partition_count : 0u;
    blockdev_unlock();
    blockdev_io_end(dev, acquired);
    return count;
}

int blockdev_partition_get_cached(struct block_device *dev,
                                  uint32_t index,
                                  struct blockdev_partition *out) {
    int acquired;

    if (dev == 0 || out == 0) {
        return -1;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0) {
        return -1;
    }
    blockdev_lock();
    if (!dev->partition_cache_valid || index >= dev->partition_count) {
        blockdev_unlock();
        blockdev_io_end(dev, acquired);
        return -1;
    }
    *out = dev->partitions[index];
    blockdev_unlock();
    blockdev_io_end(dev, acquired);
    return 0;
}

int blockdev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    uint8_t *out = (uint8_t *)buffer;
    uint64_t start;
    int rc;
    int acquired;

    if (dev == 0 || buffer == 0 || count == 0) {
        return -1;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0 || dev->read == 0) {
        blockdev_io_end(dev, acquired);
        return -1;
    }
    blockdev_lock();
    if (dev->block_size == BLOCKDEV_SECTOR_SIZE) {
        uint32_t cached;

        for (cached = 0u; cached < count; cached++) {
            struct blockdev_read_cache_entry *entry =
                blockdev_cache_entry(dev, lba + cached);

            if (!entry->valid || entry->dev != dev ||
                entry->lba != lba + cached) {
                break;
            }
            memcpy(out + cached * BLOCKDEV_SECTOR_SIZE,
                   entry->data,
                   BLOCKDEV_SECTOR_SIZE);
        }
        if (cached == count) {
            blockdev_unlock();
            blockdev_io_end(dev, acquired);
            return 0;
        }
    }
    blockdev_unlock();
    if (g_block_profile_read == 0u) {
        g_block_profile_read = kernel_profile_register("block.read");
    }
    start = kernel_profile_clock();
    blockdev_request_lock(dev);
    rc = dev->read(dev, lba, count, buffer);
    blockdev_request_unlock(dev);
    if (rc == 0) {
        blockdev_record_success(dev);
    } else {
        blockdev_record_failure(dev, rc, 0);
    }
    blockdev_lock();
    if (rc == 0 && dev->block_size == BLOCKDEV_SECTOR_SIZE) {
        for (uint32_t i = 0u; i < count; i++) {
            struct blockdev_read_cache_entry *entry =
                blockdev_cache_entry(dev, lba + i);

            memcpy(entry->data,
                   out + i * BLOCKDEV_SECTOR_SIZE,
                   BLOCKDEV_SECTOR_SIZE);
            entry->dev = dev;
            entry->lba = lba + i;
            entry->valid = 1u;
        }
    } else if (rc != 0 && dev->block_size == BLOCKDEV_SECTOR_SIZE) {
        blockdev_cache_invalidate_range(dev, lba, count);
    }
    kernel_profile_record(g_block_profile_read,
                          kernel_profile_clock() - start,
                          rc == 0 ? (uint64_t)count * dev->block_size : 0u);
    blockdev_unlock();
    blockdev_io_end(dev, acquired);
    return rc;
}

int blockdev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    int rc;
    int acquired;

    if (dev == 0 || buffer == 0 || count == 0) {
        return -1;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0 || dev->write == 0) {
        blockdev_io_end(dev, acquired);
        return -1;
    }
    if (g_block_profile_write == 0u) {
        g_block_profile_write = kernel_profile_register("block.write");
    }
    {
        uint64_t start = kernel_profile_clock();

        blockdev_request_lock(dev);
        rc = dev->write(dev, lba, count, buffer);
        blockdev_request_unlock(dev);
        if (rc == 0) {
            blockdev_record_success(dev);
        } else {
            blockdev_record_failure(dev, rc, 0);
        }
        kernel_profile_record(g_block_profile_write,
                              kernel_profile_clock() - start,
                              rc == 0 ? (uint64_t)count * dev->block_size : 0u);
    }
    blockdev_lock();
    if (rc == 0) {
        blockdev_cache_invalidate_range(dev, lba, count);
    } else {
        blockdev_cache_invalidate_range(dev, lba, count);
        dev->partition_cache_valid = 0u;
    }
    if (rc == 0 && lba == 0u) {
        blockdev_unlock();
        blockdev_io_end(dev, acquired);
        (void)blockdev_rescan_partitions(dev);
        return rc;
    }
    blockdev_unlock();
    blockdev_io_end(dev, acquired);
    return rc;
}

int blockdev_flush(struct block_device *dev) {
    uint64_t start;
    int rc;
    int acquired;

    if (dev == 0) {
        return -1;
    }
    acquired = blockdev_io_begin(dev);
    if (acquired < 0) {
        return -1;
    }
    if (dev->flush == 0) {
        blockdev_io_end(dev, acquired);
        return 0;
    }
    if (g_block_profile_flush == 0u) {
        g_block_profile_flush = kernel_profile_register("block.flush");
    }
    start = kernel_profile_clock();
    blockdev_request_lock(dev);
    rc = dev->flush(dev);
    blockdev_request_unlock(dev);
    if (rc == 0) {
        blockdev_record_success(dev);
    } else {
        blockdev_record_failure(dev, rc, 0);
    }
    kernel_profile_record(g_block_profile_flush,
                          kernel_profile_clock() - start,
                          0u);
    blockdev_io_end(dev, acquired);
    return rc;
}
