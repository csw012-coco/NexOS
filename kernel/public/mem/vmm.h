#pragma once

#include <stdint.h>

enum vmm_perm {
    VMM_PERM_NONE = 0,
    VMM_PERM_WRITE = 1u << 0,
    VMM_PERM_USER = 1u << 1,
    VMM_PERM_EXEC = 1u << 2
};

/* Core VMM operations */
uint64_t vmm_current_root(void);
uint64_t vmm_create_user_root(void);
uint64_t vmm_clone_root_cow(uint64_t source_root);
int vmm_resolve_cow_fault(uint64_t root, uint64_t fault_addr, uint64_t error_code);
void vmm_destroy_user_root(uint64_t root);
void vmm_switch_root(uint64_t root);
int vmm_root_is_current(uint64_t root);
int vmm_switch_root_or_fail(uint64_t root);
void vmm_allow_user_page(uint64_t addr);
void vmm_allow_user_range(uint64_t start, uint64_t end);
void vmm_set_supervisor_range(uint64_t start, uint64_t end);
int vmm_map(uint64_t virt_addr, uint64_t phys_addr, uint32_t perms);
int vmm_unmap(uint64_t virt_addr, uint64_t *phys_addr);
int vmm_query(uint64_t virt_addr, uint64_t *phys_addr);
int vmm_query_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags);
int vmm_cpu_supports_nx(void);
int vmm_nx_enabled(void);
int vmm_user_readable(uint64_t user_addr, uint32_t size);
int vmm_user_writable(uint64_t user_addr, uint32_t size);
int vmm_user_page_mapped(uint64_t user_addr);
int vmm_copy_from_user(void *dest, uint64_t user_addr, uint32_t size);
int vmm_copy_to_user(uint64_t user_addr, const void *src, uint32_t size);
int vmm_copy_user_cstr(char *dest, uint64_t user_addr, uint32_t max_len);
int vmm_zero_range(uint64_t start, uint64_t size);
int vmm_copy_to_range(uint64_t dest, const uint8_t *src, uint64_t size);
void vmm_unmap_range_if_present(uint64_t start, uint64_t end);
