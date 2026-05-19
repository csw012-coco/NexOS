#pragma once

#include "fs/nxfs.h"

void nxfs_mem_copy(void *dest, const void *src, uint32_t size);
void nxfs_mem_set(void *dest, uint8_t value, uint32_t size);
uint8_t *nxfs_cache_get(struct nxfs_volume *vol, uint32_t block);
void nxfs_cache_put(struct nxfs_volume *vol, uint32_t block, const uint8_t *data);
void nxfs_cache_update(struct nxfs_volume *vol, uint32_t block, const uint8_t *data);
int nxfs_read_block(struct nxfs_volume *vol, uint32_t block, void *buffer);
int nxfs_read_blocks(struct nxfs_volume *vol, uint32_t start_block, uint32_t count, void *buffer);
int nxfs_write_block(struct nxfs_volume *vol, uint32_t block, const void *buffer);
int nxfs_read_bytes(struct nxfs_volume *vol, uint32_t offset, void *buffer, uint32_t size);
int nxfs_write_bytes(struct nxfs_volume *vol, uint32_t offset, const void *buffer, uint32_t size);
