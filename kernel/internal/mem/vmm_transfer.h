#pragma once

#include <stdint.h>

struct vmm_transfer_ops {
    uint64_t page_size;
    uint64_t user_base;
    uint64_t user_limit;
    int (*readable)(uint64_t user_addr, uint32_t size);
    int (*writable)(uint64_t user_addr, uint32_t size);
    const void *(*read_ptr)(uint64_t user_addr);
    void *(*write_ptr)(uint64_t user_addr);
};

struct vmm_page_access_ops {
    uint64_t page_size;
    uint64_t user_base;
    uint64_t user_limit;
    int (*page_accessible)(uint64_t page, int writable, void *context);
};

int vmm_transfer_user_range_bounds(const struct vmm_transfer_ops *ops,
                                   uint64_t user_addr,
                                   uint64_t size,
                                   uint64_t *start_out,
                                   uint64_t *end_out);
int vmm_transfer_user_access_range_bounds(const struct vmm_page_access_ops *ops,
                                          uint64_t user_addr,
                                          uint64_t size,
                                          uint64_t *start_out,
                                          uint64_t *end_out);
int vmm_transfer_user_range_accessible(const struct vmm_page_access_ops *ops,
                                       uint64_t user_addr,
                                       uint64_t size,
                                       int writable,
                                       void *context);
uint32_t vmm_transfer_page_chunk_size(const struct vmm_transfer_ops *ops,
                                      uint64_t virt,
                                      uint64_t remaining,
                                      uint64_t *page_off_out);
int vmm_transfer_copy_from_user(const struct vmm_transfer_ops *ops,
                                void *dest,
                                uint64_t user_addr,
                                uint32_t size);
int vmm_transfer_copy_to_user(const struct vmm_transfer_ops *ops,
                              uint64_t user_addr,
                              const void *src,
                              uint32_t size);
int vmm_transfer_copy_user_cstr(const struct vmm_transfer_ops *ops,
                                char *dest,
                                uint64_t user_addr,
                                uint32_t max_len);
int vmm_transfer_zero_range(const struct vmm_transfer_ops *ops,
                            uint64_t start,
                            uint64_t size);
