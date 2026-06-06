#pragma once

#include <stdint.h>

enum {
    PAGING_MAP_PRESENT = 1ull << 0,
    PAGING_MAP_RW = 1ull << 1,
    PAGING_MAP_USER = 1ull << 2,
    PAGING_MAP_EXEC = 1ull << 3
};

enum {
    PAGING_SWITCH_REJECT_ZERO = 1u << 0,
    PAGING_SWITCH_REJECT_RIP_UNMAPPED = 1u << 1,
    PAGING_SWITCH_REJECT_RSP_UNMAPPED = 1u << 2
};

struct paging_switch_trace {
    uint64_t requested_cr3;
    uint64_t previous_cr3;
    uint64_t actual_cr3;
    uint64_t current_rip;
    uint64_t current_rsp;
    uint32_t reject_flags;
};

struct paging_clone_trace {
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

struct paging_walk_info {
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

void paging_init(uint64_t kernel_phys_base);
uint64_t paging_get_current_cr3(void);
void paging_set_current_cr3(uint64_t cr3);
uint64_t paging_create_kernel_root(void);
uint64_t paging_clone_root_deep(uint64_t source_cr3);
uint64_t paging_clone_current_root_deep(void);
uint64_t paging_create_user_root(void);
void paging_destroy_root_deep(uint64_t root_cr3);
void paging_make_page_user_accessible(uint64_t addr);
void paging_make_range_user_accessible(uint64_t start, uint64_t end);
void paging_make_range_supervisor_only(uint64_t start, uint64_t end);
int paging_map_page(uint64_t virt_addr, uint64_t phys_addr, int user_accessible, int writable);
int paging_map_page_with_exec(uint64_t virt_addr,
                              uint64_t phys_addr,
                              int user_accessible,
                              int writable,
                              int executable);
int paging_unmap_page(uint64_t virt_addr, uint64_t *phys_addr);
int paging_get_mapping(uint64_t virt_addr, uint64_t *phys_addr);
int paging_get_mapping_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags);
void *paging_phys_direct_map(uint64_t phys_addr);
int paging_get_walk_entries(uint64_t virt_addr,
                            uint64_t *pml4e,
                            uint64_t *pdpte,
                            uint64_t *pde,
                            uint64_t *pte);
int paging_get_mapping_info_in_root(uint64_t root_cr3,
                                    uint64_t virt_addr,
                                    uint64_t *phys_addr,
                                    uint64_t *flags);
int paging_get_walk_entries_in_root(uint64_t root_cr3,
                                    uint64_t virt_addr,
                                    uint64_t *pml4e,
                                    uint64_t *pdpte,
                                    uint64_t *pde,
                                    uint64_t *pte);
int paging_get_walk_info_in_root(uint64_t root_cr3, uint64_t virt_addr, struct paging_walk_info *info_out);
void paging_get_last_switch_trace(struct paging_switch_trace *trace_out);
void paging_get_last_clone_trace(struct paging_clone_trace *trace_out);
int paging_cpu_supports_nx(void);
int paging_nx_enabled(void);
