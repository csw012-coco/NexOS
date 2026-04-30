#pragma once

#include <stdint.h>

struct address_space {
    uint64_t kernel_cr3;
    uint64_t user_cr3;
    uint64_t reserved_phys_base;
    uint64_t reserved_phys_limit;
    uint64_t reserved_phys_next;
};

uint64_t addrspace_alloc_page(void);
int addrspace_free_page(uint64_t virt_addr);
