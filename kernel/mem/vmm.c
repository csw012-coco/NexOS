#include "kernel/public/mem/vmm.h"
#include "hal/hal.h"
#include "arch/x86/paging.h"

enum {
    VMM_PAGE_SIZE = 4096u,
    VMM_USER_STRING_MAX = 255u,
    VMM_COPY_BOUNCE_SIZE = 256u
};

static uint64_t vmm_align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1u);
}

static uint64_t vmm_align_up(uint64_t value, uint64_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static void vmm_mem_copy(void *dest, const void *src, uint64_t size) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    for (uint64_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void vmm_mem_set(void *dest, uint8_t value, uint64_t size) {
    uint8_t *d = (uint8_t *)dest;

    for (uint64_t i = 0; i < size; i++) {
        d[i] = value;
    }
}

static int vmm_user_range_valid(uint64_t user_addr, uint32_t size, uint64_t *start_out, uint64_t *end_out) {
    uint64_t end;

    if (user_addr == 0) {
        return 0;
    }
    if (size == 0) {
        if (start_out != 0) {
            *start_out = user_addr;
        }
        if (end_out != 0) {
            *end_out = user_addr;
        }
        return 1;
    }
    end = user_addr + (uint64_t)size;
    if (end < user_addr) {
        return 0;
    }
    if (start_out != 0) {
        *start_out = vmm_align_down(user_addr, VMM_PAGE_SIZE);
    }
    if (end_out != 0) {
        *end_out = vmm_align_down(end - 1u, VMM_PAGE_SIZE) + VMM_PAGE_SIZE;
    }
    return 1;
}

static int vmm_user_range_has_perms(uint64_t user_addr, uint32_t size, uint64_t required_flags) {
    uint64_t start;
    uint64_t end;

    if (!vmm_user_range_valid(user_addr, size, &start, &end)) {
        return 0;
    }
    while (start < end) {
        uint64_t phys;
        uint64_t flags;

        if (!vmm_query_info(start, &phys, &flags) || (flags & required_flags) != required_flags) {
            return 0;
        }
        start += VMM_PAGE_SIZE;
    }
    return 1;
}

static uint32_t vmm_page_chunk_size(uint64_t virt, uint64_t remaining, uint64_t *page_off_out) {
    uint64_t page_off = virt & (VMM_PAGE_SIZE - 1u);
    uint64_t chunk = VMM_PAGE_SIZE - page_off;

    if (chunk > remaining) {
        chunk = remaining;
    }
    if (page_off_out != 0) {
        *page_off_out = page_off;
    }
    return (uint32_t)chunk;
}

uint64_t vmm_current_root(void) {
    return hal_paging_current_root();
}

uint64_t vmm_create_user_root(void) {
    return hal_paging_create_user_root();
}

void vmm_destroy_user_root(uint64_t root) {
    hal_paging_destroy_user_root(root);
}

void vmm_switch_root(uint64_t root) {
    hal_paging_switch_root(root);
}

int vmm_root_is_current(uint64_t root) {
    return root != 0 && vmm_current_root() == root;
}

int vmm_switch_root_or_fail(uint64_t root) {
    if (root == 0) {
        return 0;
    }
    hal_paging_switch_root(root);
    return vmm_current_root() == root;
}

void vmm_allow_user_page(uint64_t addr) {
    hal_paging_allow_user_page(addr);
}

void vmm_allow_user_range(uint64_t start, uint64_t end) {
    hal_paging_allow_user_range(start, end);
}

void vmm_set_supervisor_range(uint64_t start, uint64_t end) {
    hal_paging_set_supervisor_range(start, end);
}

int vmm_map(uint64_t virt_addr, uint64_t phys_addr, uint32_t perms) {
    int user_accessible = (perms & VMM_PERM_USER) != 0;
    int writable = (perms & VMM_PERM_WRITE) != 0;
    int executable = (perms & VMM_PERM_EXEC) != 0;

    return hal_paging_map_page_with_exec(virt_addr, phys_addr, user_accessible, writable, executable);
}

int vmm_unmap(uint64_t virt_addr, uint64_t *phys_addr) {
    return hal_paging_unmap_page(virt_addr, phys_addr);
}

int vmm_query(uint64_t virt_addr, uint64_t *phys_addr) {
    return hal_paging_get_mapping(virt_addr, phys_addr);
}

int vmm_query_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags) {
    return hal_paging_get_mapping_info(virt_addr, phys_addr, flags);
}

int vmm_cpu_supports_nx(void) {
    return paging_cpu_supports_nx();
}

int vmm_nx_enabled(void) {
    return paging_nx_enabled();
}

int vmm_user_readable(uint64_t user_addr, uint32_t size) {
    return vmm_user_range_has_perms(user_addr, size, 0x1u | 0x4u);
}

int vmm_user_writable(uint64_t user_addr, uint32_t size) {
    return vmm_user_range_has_perms(user_addr, size, 0x1u | 0x2u | 0x4u);
}

int vmm_user_page_mapped(uint64_t user_addr) {
    uint64_t phys;
    uint64_t flags;

    if ((user_addr & (VMM_PAGE_SIZE - 1u)) != 0) {
        return 0;
    }
    if (!vmm_query_info(user_addr, &phys, &flags)) {
        return 0;
    }
    return (flags & 0x1u) != 0 && (flags & 0x4u) != 0;
}

int vmm_copy_from_user(void *dest, uint64_t user_addr, uint32_t size) {
    uint8_t *out = (uint8_t *)dest;
    uint32_t offset = 0;

    if (!vmm_user_readable(user_addr, size)) {
        return 0;
    }

    while (offset < size) {
        uint64_t virt = user_addr + offset;
        uint64_t phys;
        uint64_t phys_page;
        uint64_t page_off;
        uint32_t chunk = vmm_page_chunk_size(virt, size - offset, &page_off);
        const uint8_t *src;

        if (!vmm_query(virt, &phys)) {
            return 0;
        }
        phys_page = phys & ~(uint64_t)(VMM_PAGE_SIZE - 1u);
        src = (const uint8_t *)hal_phys_direct_map(phys_page);
        for (uint32_t i = 0; i < chunk; i++) {
            out[offset + i] = src[page_off + i];
        }
        offset += chunk;
    }
    return 1;
}

int vmm_copy_to_user(uint64_t user_addr, const void *src, uint32_t size) {
    const uint8_t *in = (const uint8_t *)src;
    uint32_t offset = 0;

    if (!vmm_user_writable(user_addr, size)) {
        return 0;
    }

    while (offset < size) {
        uint64_t virt = user_addr + offset;
        uint64_t phys;
        uint64_t phys_page;
        uint64_t page_off;
        uint32_t chunk = vmm_page_chunk_size(virt, size - offset, &page_off);
        uint8_t *dest;

        if (!vmm_query(virt, &phys)) {
            return 0;
        }
        phys_page = phys & ~(uint64_t)(VMM_PAGE_SIZE - 1u);
        dest = (uint8_t *)hal_phys_direct_map(phys_page);
        for (uint32_t i = 0; i < chunk; i++) {
            dest[page_off + i] = in[offset + i];
        }
        offset += chunk;
    }
    return 1;
}

int vmm_copy_user_cstr(char *dest, uint64_t user_addr, uint32_t max_len) {
    uint32_t i;

    if (dest == 0 || max_len == 0 || max_len > VMM_USER_STRING_MAX + 1u) {
        return 0;
    }
    for (i = 0; i + 1u < max_len; i++) {
        if (!vmm_copy_from_user(&dest[i], user_addr + i, 1)) {
            return 0;
        }
        if (dest[i] == '\0') {
            return 1;
        }
    }
    dest[max_len - 1u] = '\0';
    return 1;
}

int vmm_zero_range(uint64_t start, uint64_t size) {
    uint64_t offset = 0;

    while (offset < size) {
        uint64_t virt = start + offset;
        uint64_t phys;
        uint64_t phys_page;
        uint64_t page_off;
        uint32_t chunk = vmm_page_chunk_size(virt, size - offset, &page_off);
        uint8_t *dest;

        if (!vmm_query(virt, &phys)) {
            return 0;
        }
        phys_page = phys & ~(uint64_t)(VMM_PAGE_SIZE - 1u);
        dest = (uint8_t *)hal_phys_direct_map(phys_page);
        vmm_mem_set(dest + page_off, 0, chunk);
        offset += chunk;
    }
    return 1;
}

int vmm_copy_to_range(uint64_t dest, const uint8_t *src, uint64_t size) {
    uint64_t offset = 0;

    if (src == 0) return 0;
    while (offset < size) {
        uint64_t virt = dest + offset;
        uint64_t phys;
        uint64_t phys_page;
        uint64_t page_off;
        uint32_t chunk = vmm_page_chunk_size(virt, size - offset, &page_off);
        uint8_t *out;

        if (!vmm_query(virt, &phys)) {
            return 0;
        }
        phys_page = phys & ~(uint64_t)(VMM_PAGE_SIZE - 1u);
        out = (uint8_t *)hal_phys_direct_map(phys_page);
        vmm_mem_copy(out + page_off, src + offset, chunk);
        offset += chunk;
    }
    return 1;
}

void vmm_unmap_range_if_present(uint64_t start, uint64_t end) {
    uint64_t page = vmm_align_down(start, VMM_PAGE_SIZE);
    uint64_t page_end = vmm_align_up(end, VMM_PAGE_SIZE);

    while (page < page_end) {
        uint64_t phys_addr;

        (void)vmm_unmap(page, &phys_addr);
        page += VMM_PAGE_SIZE;
    }
}

/* Diagnostic operations (kernel.c boot/diagnostic use only, SOSP-18 mechanism abstraction) */

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
    /* Copy paging_walk_info to vmm_page_walk_info (same structure layout) */
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
    /* Copy paging_switch_trace to vmm_page_fault_trace (same structure layout) */
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
    /* Copy paging_clone_trace to vmm_page_clone_trace (same structure layout) */
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
