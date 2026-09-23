#include "fs/nxfs_internal.h"
#include "lib/string.h"

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
#define NXFS_IO_FAIL_TRACE(...) ((void)0)
#else
#include "kernel/public/core/kprint.h"
#define NXFS_IO_FAIL_TRACE(...) kprint(__VA_ARGS__)
#endif

void nxfs_mem_copy(void *dest, const void *src, uint32_t size) {
    memcpy(dest, src, size);
}

void nxfs_mem_set(void *dest, uint8_t value, uint32_t size) {
    memset(dest, value, size);
}

uint8_t *nxfs_cache_get(struct nxfs_volume *vol, uint32_t block) {
    uint32_t i;

    if (vol == 0) {
        return 0;
    }
    for (i = 0; i < NXFS_CACHE_BLOCKS; i++) {
        if (vol->cache[i].valid && vol->cache[i].block == block) {
            vol->cache[i].last_used = ++vol->cache_epoch;
            return vol->cache[i].data;
        }
    }
    return 0;
}

void nxfs_cache_put(struct nxfs_volume *vol, uint32_t block, const uint8_t *data) {
    uint32_t next;
    uint32_t oldest_index = 0u;
    uint32_t oldest_stamp = 0xffffffffu;

    if (vol == 0 || data == 0) {
        return;
    }
    for (uint32_t i = 0u; i < NXFS_CACHE_BLOCKS; i++) {
        if (!vol->cache[i].valid) {
            next = i;
            break;
        }
        if (vol->cache[i].last_used < oldest_stamp) {
            oldest_stamp = vol->cache[i].last_used;
            oldest_index = i;
        }
        next = oldest_index;
    }
    vol->cache[next].block = block;
    vol->cache[next].valid = 1u;
    vol->cache[next].last_used = ++vol->cache_epoch;
    nxfs_mem_copy(vol->cache[next].data, data, NXFS_BLOCK_SIZE);
    vol->cache_next = (vol->cache_next + 1u) % NXFS_CACHE_BLOCKS;
}

void nxfs_cache_update(struct nxfs_volume *vol, uint32_t block, const uint8_t *data) {
    uint8_t *cached = nxfs_cache_get(vol, block);

    if (cached != 0) {
        nxfs_mem_copy(cached, data, NXFS_BLOCK_SIZE);
    }
}

void nxfs_cache_invalidate_all(struct nxfs_volume *vol) {
    if (vol == 0) {
        return;
    }
    for (uint32_t i = 0u; i < NXFS_CACHE_BLOCKS; i++) {
        vol->cache[i].valid = 0u;
        vol->cache[i].last_used = 0u;
    }
    vol->cache_epoch = 0u;
    vol->cache_next = 0u;
}

void nxfs_mark_write_failed(struct nxfs_volume *vol) {
    if (vol == 0) {
        return;
    }
    vol->write_error = 1u;
    vol->dirty = 1u;
    nxfs_cache_invalidate_all(vol);
}

int nxfs_flush(struct nxfs_volume *vol) {
    if (vol == 0 || !vol->mounted || vol->bdev == 0) {
        return -1;
    }
    if (vol->write_error) {
        return -1;
    }
    if (!vol->dirty) {
        return 0;
    }
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
    vol->dirty = 0u;
    return 0;
#else
    if (blockdev_flush(vol->bdev) != 0) {
        nxfs_mark_write_failed(vol);
        return -1;
    }
    vol->dirty = 0u;
    return 0;
#endif
}

int nxfs_read_block(struct nxfs_volume *vol, uint32_t block, void *buffer) {
    uint8_t *cached;

    if (vol == 0 || !vol->mounted || vol->bdev == 0 || buffer == 0) {
        return -1;
    }
    cached = nxfs_cache_get(vol, block);
    if (cached != 0) {
        nxfs_mem_copy(buffer, cached, NXFS_BLOCK_SIZE);
        return 0;
    }
    if (blockdev_read(vol->bdev, vol->partition_lba + block, 1, buffer) != 0) {
        return -1;
    }
    nxfs_cache_put(vol, block, (const uint8_t *)buffer);
    return 0;
}

int nxfs_read_blocks(struct nxfs_volume *vol, uint32_t start_block, uint32_t count, void *buffer) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t cached = 0u;

    if (vol == 0 || !vol->mounted || buffer == 0 || count == 0u) {
        return -1;
    }

    while (cached < count) {
        uint8_t *entry = nxfs_cache_get(vol, start_block + cached);

        if (entry == 0) {
            break;
        }
        nxfs_mem_copy(out + cached * NXFS_BLOCK_SIZE, entry, NXFS_BLOCK_SIZE);
        cached++;
    }
    if (cached == count) {
        return 0;
    }
    if (blockdev_read(vol->bdev,
                      vol->partition_lba + start_block + cached,
                      count - cached,
                      out + cached * NXFS_BLOCK_SIZE) != 0) {
        return -1;
    }
    for (uint32_t i = cached; i < count; i++) {
        nxfs_cache_put(vol,
                       start_block + i,
                       out + i * NXFS_BLOCK_SIZE);
    }
    return 0;
}

int nxfs_write_block(struct nxfs_volume *vol, uint32_t block, const void *buffer) {
    if (vol == 0 || !vol->mounted || vol->bdev == 0 || buffer == 0) {
        return -1;
    }
    if (vol->write_error) {
        return -1;
    }
    {
        int rc = blockdev_write(vol->bdev, vol->partition_lba + block, 1, buffer);

        if (rc != 0) {
            NXFS_IO_FAIL_TRACE("nxfs: block write failed dev=%s block=%u lba=%u rc=%d\n",
                               vol->bdev->name != 0 ? vol->bdev->name : "(null)",
	                               block,
	                               vol->partition_lba + block,
	                               rc);
	            nxfs_mark_write_failed(vol);
	            return -1;
	        }
	    }
    vol->dirty = 1u;
    nxfs_cache_update(vol, block, (const uint8_t *)buffer);
    return 0;
}

int nxfs_read_bytes(struct nxfs_volume *vol, uint32_t offset, void *buffer, uint32_t size) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0;

    while (done < size) {
        uint32_t absolute = offset + done;
        uint32_t block = absolute / NXFS_BLOCK_SIZE;
        uint32_t block_off = absolute % NXFS_BLOCK_SIZE;
        uint8_t *cached = nxfs_cache_get(vol, block);
        uint32_t chunk;

        if (cached != 0) {
            chunk = NXFS_BLOCK_SIZE - block_off;
            if (chunk > size - done) {
                chunk = size - done;
            }
            nxfs_mem_copy(out + done, cached + block_off, chunk);
            done += chunk;
            continue;
        }

        {
            uint32_t max_blocks = 8;
            uint32_t remaining = (size - done + block_off + NXFS_BLOCK_SIZE - 1u) / NXFS_BLOCK_SIZE;
            uint8_t temp[NXFS_BLOCK_SIZE * 8];
            uint32_t i;

            if (remaining < max_blocks) {
                max_blocks = remaining;
            }
            if (nxfs_read_blocks(vol, block, max_blocks, temp) != 0) {
                return -1;
            }
            for (i = 0; i < max_blocks; i++) {
                nxfs_cache_put(vol, block + i, temp + i * NXFS_BLOCK_SIZE);
            }
            chunk = NXFS_BLOCK_SIZE - block_off;
            if (chunk > size - done) {
                chunk = size - done;
            }
            nxfs_mem_copy(out + done, temp + block_off, chunk);
            done += chunk;
        }
    }
    return 0;
}

static int nxfs_write_block_batch(struct nxfs_volume *vol,
                                 uint32_t start_block,
                                 uint32_t start_offset,
                                 const uint8_t *src,
                                 uint32_t length) {
    enum {
        NXFS_WRITE_BATCH_MAX_BLOCKS = 16u
    };
    uint8_t scratch[NXFS_BLOCK_SIZE * NXFS_WRITE_BATCH_MAX_BLOCKS];
    uint32_t block_count = (start_offset + length + NXFS_BLOCK_SIZE - 1u) / NXFS_BLOCK_SIZE;
    uint32_t copied = 0u;

    if (vol == 0 || !vol->mounted || vol->bdev == 0 || src == 0 || length == 0u) {
        return -1;
    }
    if (block_count == 0u || block_count > NXFS_WRITE_BATCH_MAX_BLOCKS) {
        return -1;
    }

    for (uint32_t i = 0u; i < block_count; i++) {
        uint8_t *cached = nxfs_cache_get(vol, start_block + i);

        if (cached != 0) {
            nxfs_mem_copy(scratch + i * NXFS_BLOCK_SIZE, cached, NXFS_BLOCK_SIZE);
            continue;
        }
        if (blockdev_read(vol->bdev,
                          vol->partition_lba + start_block + i,
                          1u,
                          scratch + i * NXFS_BLOCK_SIZE) != 0) {
            return -1;
        }
    }

    for (uint32_t i = 0u; i < block_count; i++) {
        uint32_t block_begin = i * NXFS_BLOCK_SIZE;
        uint32_t offset_in_block = (i == 0u) ? start_offset : 0u;
        uint32_t segment_len = length - copied;
        uint32_t remaining_in_block = NXFS_BLOCK_SIZE - offset_in_block;

        if (segment_len > remaining_in_block) {
            segment_len = remaining_in_block;
        }
        if (segment_len == 0u) {
            break;
        }
        nxfs_mem_copy(scratch + block_begin + offset_in_block,
                      src + copied,
                      segment_len);
        copied += segment_len;
        nxfs_cache_update(vol, start_block + i, scratch + block_begin);
    }

    if (blockdev_write(vol->bdev,
                       vol->partition_lba + start_block,
                       block_count,
                       scratch) != 0) {
        NXFS_IO_FAIL_TRACE("nxfs: batched block write failed dev=%s start_block=%u lba=%u blocks=%u\n",
                           vol->bdev->name != 0 ? vol->bdev->name : "(null)",
                           start_block,
                           vol->partition_lba + start_block,
                           block_count);
        nxfs_mark_write_failed(vol);
        return -1;
    }

    vol->dirty = 1u;
    return 0;
}

int nxfs_write_bytes(struct nxfs_volume *vol, uint32_t offset, const void *buffer, uint32_t size) {
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t done = 0u;

    if (vol == 0 || !vol->mounted || buffer == 0 || size == 0u) {
        return -1;
    }

    while (done < size) {
        uint32_t absolute = offset + done;
        uint32_t block = absolute / NXFS_BLOCK_SIZE;
        uint32_t block_off = absolute % NXFS_BLOCK_SIZE;
        uint32_t remaining = size - done;
        uint32_t max_batch = (NXFS_BLOCK_SIZE * 8u) - block_off;
        uint32_t batch_len = remaining;

        if (max_batch < batch_len) {
            batch_len = max_batch;
        }
        if (batch_len == 0u) {
            batch_len = 1u;
        }
        if (nxfs_write_block_batch(vol, block, block_off, in + done, batch_len) != 0) {
            return -1;
        }
        done += batch_len;
    }
    return 0;
}
