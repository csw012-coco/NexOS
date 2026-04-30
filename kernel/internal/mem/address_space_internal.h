#pragma once

#include "kernel/internal/proc/process_internal_base.h"

void addrspace_reset(struct address_space *address_space);
int addrspace_map_range(uint64_t start, uint64_t end);
void addrspace_release_dynamic_pages(void);
void addrspace_unmap_range_if_present(uint64_t start, uint64_t end);
int addrspace_map_range_with_perms(uint64_t start, uint64_t end, uint32_t perms);
int addrspace_zero_range(uint64_t start, uint64_t size);
int addrspace_copy_to_range(uint64_t dest, const uint8_t *src, uint64_t size);
