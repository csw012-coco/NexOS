#pragma once

#include <stdint.h>
#include "bootx.h"

void pmm_init(const struct bootx_memmap_entry *memmap,
              uint32_t memmap_count,
              uint64_t kernel_phys_addr,
              uint64_t kernel_phys_size);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_contiguous(uint32_t page_count);
int pmm_free_page(uint64_t phys_addr);
void pmm_reserve_range(uint64_t base, uint64_t size);
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);
uint32_t pmm_used_pages(void);
uint32_t pmm_dropped_pages(void);
