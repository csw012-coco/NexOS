#pragma once

#include <stdint.h>

struct process_mm_ops {
    uint32_t (*page_alloc)(void);
    uint32_t (*page_alloc_prot)(int writable);
    uint32_t (*page_alloc_at)(uint32_t user_page, int writable);
    int32_t (*page_protect)(uint32_t user_page, int writable);
    int32_t (*page_free)(uint32_t user_page);
    int32_t (*page_free_pid)(uint32_t pid, uint32_t user_page);
    uint32_t (*shared_page_alloc)(void);
    int32_t (*shared_page_free)(uint32_t frame);
    uint32_t (*shared_page_map)(uint32_t frame);
    int32_t (*shared_page_unmap)(uint32_t user_page);
    int32_t (*shared_page_unmap_pid)(uint32_t pid, uint32_t user_page);
    void (*cleanup_pid)(uint32_t pid);
};

void process_mm_ops_register(const struct process_mm_ops *ops);
uint32_t process_mm_page_alloc(void);
uint32_t process_mm_page_alloc_prot(int writable);
uint32_t process_mm_page_alloc_at(uint32_t user_page, int writable);
int32_t process_mm_page_protect(uint32_t user_page, int writable);
int32_t process_mm_page_free(uint32_t user_page);
int32_t process_mm_page_free_pid(uint32_t pid, uint32_t user_page);
uint32_t process_mm_shared_page_alloc(void);
int32_t process_mm_shared_page_free(uint32_t frame);
uint32_t process_mm_shared_page_map(uint32_t frame);
int32_t process_mm_shared_page_unmap(uint32_t user_page);
int32_t process_mm_shared_page_unmap_pid(uint32_t pid, uint32_t user_page);
void process_mm_cleanup_pid(uint32_t pid);
