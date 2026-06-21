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
uint64_t addrspace_mmap(uint64_t requested_addr,
                        uint64_t length,
                        uint32_t prot,
                        uint32_t flags,
                        uint32_t shm_handle,
                        uint64_t offset);
int addrspace_munmap(uint64_t addr, uint64_t length);
int addrspace_shm_open(const char *name, uint64_t size, uint32_t flags);
int addrspace_shm_unlink(const char *name);
