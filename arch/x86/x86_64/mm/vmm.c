#include "arch/x86/x86_64/mm/paging.h"
#include "kernel/internal/mem/vmm_diag.h"
#include "kernel/public/mem/vmm.h"

int vmm_cpu_supports_nx(void) {
    return paging_cpu_supports_nx();
}

int vmm_nx_enabled(void) {
    return paging_nx_enabled();
}

uint64_t vmm_get_current_cr3(void) {
    return paging_get_current_cr3();
}

int vmm_query_mapping_in_context(uint64_t context_cr3, uint64_t virt_addr,
                                 uint64_t *phys_addr, uint64_t *flags) {
    return paging_get_mapping_info_in_root(context_cr3, virt_addr, phys_addr, flags);
}

int vmm_query_page_walk_in_context(uint64_t context_cr3, uint64_t virt_addr,
                                   uint64_t *pml4e, uint64_t *pdpte,
                                   uint64_t *pde, uint64_t *pte) {
    return paging_get_walk_entries_in_root(context_cr3, virt_addr, pml4e, pdpte, pde, pte);
}

int vmm_query_page_walk(uint64_t virt_addr,
                        uint64_t *pml4e, uint64_t *pdpte,
                        uint64_t *pde, uint64_t *pte) {
    return paging_get_walk_entries(virt_addr, pml4e, pdpte, pde, pte);
}

int vmm_query_page_walk_full(uint64_t context_cr3, uint64_t virt_addr,
                             struct vmm_page_walk_info *info_out) {
    struct paging_walk_info paging_info;

    if (!info_out) {
        return 0;
    }
    if (!paging_get_walk_info_in_root(context_cr3, virt_addr, &paging_info)) {
        return 0;
    }
    info_out->pml4_phys = paging_info.pml4_phys;
    info_out->pdpt_phys = paging_info.pdpt_phys;
    info_out->pd_phys = paging_info.pd_phys;
    info_out->pt_phys = paging_info.pt_phys;
    info_out->pml4e = paging_info.pml4e;
    info_out->pdpte = paging_info.pdpte;
    info_out->pde = paging_info.pde;
    info_out->pte = paging_info.pte;
    info_out->phys_addr = paging_info.phys_addr;
    info_out->flags = paging_info.flags;
    return 1;
}

void vmm_get_page_fault_trace(struct vmm_page_fault_trace *trace_out) {
    struct paging_switch_trace paging_trace;

    if (!trace_out) {
        return;
    }
    paging_get_last_switch_trace(&paging_trace);
    trace_out->requested_cr3 = paging_trace.requested_cr3;
    trace_out->previous_cr3 = paging_trace.previous_cr3;
    trace_out->actual_cr3 = paging_trace.actual_cr3;
    trace_out->current_rip = paging_trace.current_rip;
    trace_out->current_rsp = paging_trace.current_rsp;
    trace_out->reject_flags = paging_trace.reject_flags;
}

void vmm_get_page_clone_trace(struct vmm_page_clone_trace *trace_out) {
    struct paging_clone_trace paging_trace;

    if (!trace_out) {
        return;
    }
    paging_get_last_clone_trace(&paging_trace);
    trace_out->source_cr3 = paging_trace.source_cr3;
    trace_out->clone_cr3 = paging_trace.clone_cr3;
    trace_out->source_pml4e0 = paging_trace.source_pml4e0;
    trace_out->source_pml4e511 = paging_trace.source_pml4e511;
    trace_out->clone_pml4e0 = paging_trace.clone_pml4e0;
    trace_out->clone_pml4e511 = paging_trace.clone_pml4e511;
    trace_out->fail_virt = paging_trace.fail_virt;
    trace_out->fail_phys = paging_trace.fail_phys;
    trace_out->fail_stage = paging_trace.fail_stage;
}
