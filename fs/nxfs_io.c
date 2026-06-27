#include "fs/nxfs_internal.h"
#include "lib/string.h"

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
            return vol->cache[i].data;
        }
    }
    return 0;
}

void nxfs_cache_put(struct nxfs_volume *vol, uint32_t block, const uint8_t *data) {
    uint32_t next;

    if (vol == 0 || data == 0) {
        return;
    }
    next = vol->cache_next % NXFS_CACHE_BLOCKS;
    vol->cache[next].block = block;
    vol->cache[next].valid = 1;
    nxfs_mem_copy(vol->cache[next].data, data, NXFS_BLOCK_SIZE);
    vol->cache_next = (next + 1u) % NXFS_CACHE_BLOCKS;
}

void nxfs_cache_update(struct nxfs_volume *vol, uint32_t block, const uint8_t *data) {
    uint8_t *cached = nxfs_cache_get(vol, block);

    if (cached != 0) {
        nxfs_mem_copy(cached, data, NXFS_BLOCK_SIZE);
    }
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
    if (blockdev_write(vol->bdev, vol->partition_lba + block, 1, buffer) != 0) {
        return -1;
    }
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

int nxfs_write_bytes(struct nxfs_volume *vol, uint32_t offset, const void *buffer, uint32_t size) {
    const uint8_t *in = (const uint8_t *)buffer;
    uint32_t done = 0;

    while (done < size) {
        uint32_t absolute = offset + done;
        uint32_t block = absolute / NXFS_BLOCK_SIZE;
        uint32_t block_off = absolute % NXFS_BLOCK_SIZE;
        uint32_t chunk = NXFS_BLOCK_SIZE - block_off;

        if (chunk > size - done) {
            chunk = size - done;
        }
        if (nxfs_read_block(vol, block, vol->sector_buffer) != 0) {
            return -1;
        }
        nxfs_mem_copy(vol->sector_buffer + block_off, in + done, chunk);
        if (nxfs_write_block(vol, block, vol->sector_buffer) != 0) {
            return -1;
        }
        done += chunk;
    }
    return 0;
}
