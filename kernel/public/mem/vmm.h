#pragma once

#include <stdint.h>

enum vmm_perm {
    VMM_PERM_NONE = 0,
    VMM_PERM_WRITE = 1u << 0,
    VMM_PERM_USER = 1u << 1,
    VMM_PERM_EXEC = 1u << 2
};

/* Diagnostic structures for x86-64 paging introspection (SOSP-18: Mechanism Abstraction) */
enum {
    VMM_SWITCH_REJECT_ZERO = 1u << 0,
    VMM_SWITCH_REJECT_RIP_UNMAPPED = 1u << 1,
    VMM_SWITCH_REJECT_RSP_UNMAPPED = 1u << 2
};

struct vmm_page_fault_trace {
    uint64_t requested_cr3;
    uint64_t previous_cr3;
    uint64_t actual_cr3;
    uint64_t current_rip;
    uint64_t current_rsp;
    uint32_t reject_flags;
};

struct vmm_page_clone_trace {
    uint64_t source_cr3;
    uint64_t clone_cr3;
    uint64_t source_pml4e0;
    uint64_t source_pml4e511;
    uint64_t clone_pml4e0;
    uint64_t clone_pml4e511;
    uint64_t fail_virt;
    uint64_t fail_phys;
    uint32_t fail_stage;
};

struct vmm_page_walk_info {
    uint64_t pml4_phys;
    uint64_t pdpt_phys;
    uint64_t pd_phys;
    uint64_t pt_phys;
    uint64_t pml4e;
    uint64_t pdpte;
    uint64_t pde;
    uint64_t pte;
    uint64_t phys_addr;
    uint64_t flags;
};

/* Core VMM operations */
uint64_t vmm_current_root(void);
uint64_t vmm_create_user_root(void);
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

/* Diagnostic operations (kernel.c boot/diagnostic use only) */
uint64_t vmm_get_current_cr3(void);
int vmm_query_mapping_in_context(uint64_t context_cr3, uint64_t virt_addr,
                                 uint64_t *phys_addr, uint64_t *flags);
int vmm_query_page_walk_in_context(uint64_t context_cr3, uint64_t virt_addr,
                                   uint64_t *pml4e, uint64_t *pdpte,
                                   uint64_t *pde, uint64_t *pte);
int vmm_query_page_walk(uint64_t virt_addr,
                        uint64_t *pml4e, uint64_t *pdpte,
                        uint64_t *pde, uint64_t *pte);
int vmm_query_page_walk_full(uint64_t context_cr3, uint64_t virt_addr,
                             struct vmm_page_walk_info *info_out);
void vmm_get_page_fault_trace(struct vmm_page_fault_trace *trace_out);
void vmm_get_page_clone_trace(struct vmm_page_clone_trace *trace_out);
