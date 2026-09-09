#pragma once

#include <stdint.h>

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

uint64_t vmm_get_current_cr3(void);
int vmm_query_mapping_in_context(uint64_t context_cr3,
                                 uint64_t virt_addr,
                                 uint64_t *phys_addr,
                                 uint64_t *flags);
int vmm_query_page_walk_in_context(uint64_t context_cr3,
                                   uint64_t virt_addr,
                                   uint64_t *pml4e,
                                   uint64_t *pdpte,
                                   uint64_t *pde,
                                   uint64_t *pte);
int vmm_query_page_walk(uint64_t virt_addr,
                        uint64_t *pml4e,
                        uint64_t *pdpte,
                        uint64_t *pde,
                        uint64_t *pte);
int vmm_query_page_walk_full(uint64_t context_cr3,
                             uint64_t virt_addr,
                             struct vmm_page_walk_info *info_out);
void vmm_get_page_fault_trace(struct vmm_page_fault_trace *trace_out);
void vmm_get_page_clone_trace(struct vmm_page_clone_trace *trace_out);
