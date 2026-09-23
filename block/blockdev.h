#pragma once

#include <stddef.h>
#include <stdint.h>

struct block_device;

enum {
    BLOCKDEV_MAX_PARTITIONS = 128u,
    BLOCKDEV_REQUEST_QUEUE_DEPTH = 8u,
    BLOCKDEV_MERGE_SECTORS_MAX = 64u
};

struct blockdev_async_request;
typedef void (*blockdev_async_complete_fn)(struct blockdev_async_request *req,
                                           int status,
                                           void *context);

struct blockdev_async_request {
    struct block_device *dev;
    uint64_t lba;
    uint32_t count;
    void *buffer;
    const void *write_buffer;
    uint8_t kind;
    uint8_t valid;
    uint8_t in_flight;
    volatile uint8_t done;
    int status;
    void *context;
    blockdev_async_complete_fn complete;
};

enum blockdev_state {
    BLOCKDEV_STATE_PROBING = 0u,
    BLOCKDEV_STATE_ONLINE = 1u,
    BLOCKDEV_STATE_RECOVERING = 2u,
    BLOCKDEV_STATE_OFFLINE = 3u,
    BLOCKDEV_STATE_REMOVING = 4u
};

typedef int (*blockdev_read_fn)(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
typedef int (*blockdev_write_fn)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
typedef int (*blockdev_flush_fn)(struct block_device *dev);
typedef int (*blockdev_reset_fn)(struct block_device *dev);

struct blockdev_info {
    char name[32];
    uint32_t block_size;
    uint64_t block_count;
    uint8_t writable;
    uint32_t partition_count;
    uint32_t state;
    uint32_t failure_count;
    uint32_t consecutive_failures;
    uint32_t rebind_count;
    int32_t last_error;
    char last_error_reason[32];
    char last_rebind_reason[32];
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
    blockdev_flush_fn flush;
    blockdev_reset_fn reset;
    void *driver_data;
    volatile uint32_t io_refs;
    volatile uint32_t request_lock;
    volatile uint32_t request_cancelled;
    volatile uint32_t request_deadline;
    volatile uint32_t failure_count;
    volatile uint32_t consecutive_failures;
    volatile uint32_t rebind_count;
    volatile int32_t last_error;
    char last_error_reason[32];
    char last_rebind_reason[32];
    volatile uint8_t rootfs_protected;
    volatile uint8_t removing;
    volatile uint8_t state;
};

void blockdev_init(void);
int blockdev_register(struct block_device *dev);
int blockdev_unregister(struct block_device *dev);
/* Legacy lookup only; the returned pointer has no lifetime protection. */
struct block_device *blockdev_get(uint32_t index);
/* Acquire a registered ONLINE device reference. Release it on every path. */
struct block_device *blockdev_acquire(uint32_t index);
int blockdev_acquire_device(struct block_device *dev);
void blockdev_release(struct block_device *dev);
void blockdev_cancel_request(struct block_device *dev);
void blockdev_extend_request_deadline_ms(struct block_device *dev, uint32_t ms);
int blockdev_request_cancelled(const struct block_device *dev);
void blockdev_record_failure(struct block_device *dev, int error, const char *reason);
void blockdev_record_success(struct block_device *dev);
void blockdev_record_rebind(struct block_device *dev, const char *reason);
uint32_t blockdev_count(void);
enum blockdev_state blockdev_get_state(const struct block_device *dev);
int blockdev_set_state(struct block_device *dev, enum blockdev_state state);
void blockdev_set_rootfs_protected(struct block_device *dev, uint8_t protected);
int blockdev_is_rootfs_protected(const struct block_device *dev);
int blockdev_reset(struct block_device *dev);
int blockdev_get_info(uint32_t index, struct blockdev_info *out);
int blockdev_get_partition_info(uint32_t disk_index,
                                uint32_t slot,
                                struct blockdev_partition *out);
int blockdev_rescan_partitions(struct block_device *dev);
uint32_t blockdev_partition_count(struct block_device *dev);
int blockdev_partition_get(struct block_device *dev, uint32_t index, struct blockdev_partition *out);
uint32_t blockdev_partition_count_cached(struct block_device *dev);
int blockdev_partition_get_cached(struct block_device *dev, uint32_t index, struct blockdev_partition *out);
int blockdev_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer);
int blockdev_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer);
int blockdev_flush(struct block_device *dev);
int blockdev_submit_read_async(struct block_device *dev,
                              uint64_t lba,
                              uint32_t count,
                              void *buffer,
                              blockdev_async_complete_fn complete,
                              void *context);
int blockdev_submit_write_async(struct block_device *dev,
                               uint64_t lba,
                               uint32_t count,
                               const void *buffer,
                               blockdev_async_complete_fn complete,
                               void *context);
void blockdev_async_flush_pending(void);
