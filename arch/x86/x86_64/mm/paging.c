#include "arch/x86/x86_64/mm/paging.h"
#include "arch/x86/x86_64/mm/pmm.h"
#include "lib/string.h"

extern char __kernel_start[];
extern char __kernel_end[];
extern char __kernel_text_start[];
extern char __kernel_text_end[];
extern char __kernel_data_start[];
extern char __kernel_data_end[];
extern char __kernel_bss_start[];
extern char __kernel_bss_end[];
extern char __userelf_start[];
extern char __userelf_stack_top[];

enum {
    PAGING_FLAG_PRESENT = 1ull << 0,
    PAGING_FLAG_RW = 1ull << 1,
    PAGING_FLAG_USER = 1ull << 2,
    PAGING_FLAG_PWT = 1ull << 3,
    PAGING_FLAG_PCD = 1ull << 4,
    PAGING_FLAG_PAGE_SIZE = 1ull << 7,
    PAGING_FLAG_PAT_4K = 1ull << 7,
    PAGING_FLAG_COW = 1ull << 9,
    PAGING_FLAG_NX = 1ull << 63,
    PAGING_ADDR_MASK = 0x000ffffffffff000ull,
    PAGING_TABLE_ENTRIES = 512,
    PAGING_KERNEL_RANGE_LIMIT = 64,
    PAGING_KERNEL_GUARD_LIMIT = 64,
    PAGING_MSR_PAT = 0x00000277u,
    PAGING_MSR_EFER = 0xc0000080u,
    PAGING_EFER_NXE = 1ull << 11,
    PAGING_CPUID_BASIC_FEATURES = 0x00000001u,
    PAGING_CPUID_PAT_BIT = 1u << 16,
    PAGING_CPUID_EXT_FEATURES = 0x80000001u,
    PAGING_CPUID_NX_BIT = 1u << 20,
    PAGING_PAT_ENTRY_WC = 4u,
    PAGING_MEMORY_TYPE_WC = 1u,
    PAGING_CR0_NW = 1ull << 29,
    PAGING_CR0_CD = 1ull << 30,
    PAGING_RFLAGS_IF = 1ull << 9
};

struct paging_kernel_range {
    uint64_t phys_start;
    uint64_t phys_end;
    uint64_t virt_start;
};

struct paging_walk_alloc {
    uint64_t *pml4_entry;
    uint64_t *pdpt_entry;
    uint64_t *pd_entry;

    uint64_t pdpt_phys;
    uint64_t pd_phys;
    uint64_t pt_phys;

    uint8_t created_pdpt;
    uint8_t created_pd;
    uint8_t created_pt;
};

static uint64_t g_kernel_phys_base;
static struct paging_switch_trace g_last_switch_trace;
static struct paging_clone_trace g_last_clone_trace;
static struct paging_kernel_range g_kernel_ranges[PAGING_KERNEL_RANGE_LIMIT];
static uint32_t g_kernel_range_count;
static uint64_t g_kernel_guard_pages[PAGING_KERNEL_GUARD_LIMIT];
static uint32_t g_kernel_guard_count;
static uint8_t g_paging_nx_supported;
static uint8_t g_paging_nx_enabled;
static uint8_t g_paging_pat_wc_enabled;
static uint64_t *paging_table_from_entry(uint64_t entry);
static int paging_translate_in_root(uint64_t cr3, uint64_t virt_addr, uint64_t *phys_out);
static int paging_translate_current(uint64_t virt_addr, uint64_t *phys_out);
static uint64_t *paging_root_table(uint64_t cr3);
static uint64_t *paging_alloc_table(uint64_t *phys_out);
static uint64_t paging_clone_table_deep(uint64_t table_phys, uint32_t level);
static uint64_t paging_clone_table_cow(uint64_t table_phys,
                                       uint32_t level,
                                       int path_user,
                                       int *ok);
static void paging_mark_clone_cow_pair(uint64_t source_phys,
                                       uint64_t clone_phys,
                                       uint32_t level,
                                       int path_user);
static void paging_destroy_table_deep(uint64_t table_phys, uint32_t level);
static void paging_destroy_table_deep_internal(uint64_t table_phys,
                                               uint32_t level,
                                               int path_user);
static int paging_walk_in_root(uint64_t root_cr3, uint64_t virt_addr, struct paging_walk_info *info_out);
static uint64_t *paging_walk_to_pte_in_root(uint64_t root_cr3, uint64_t addr, int create, int user_accessible);
static uint64_t *paging_walk_to_pte(uint64_t addr, int create);
static int paging_map_page_in_root(uint64_t root_cr3,
                                   uint64_t virt_addr,
                                   uint64_t phys_addr,
                                   int user_accessible,
                                   int writable,
                                   int executable);
static void paging_build_kernel_ranges(void);
static void paging_set_range_write_flag(uint64_t start, uint64_t end, int writable);
static void paging_set_range_execute_flag(uint64_t start, uint64_t end, int executable);
static void paging_walk_rollback(uint64_t *pml4_entry,
                                 uint64_t *pdpt_entry,
                                 uint64_t *pd_entry,
                                 uint64_t new_pdpt_phys,
                                 uint64_t new_pd_phys,
                                 uint64_t new_pt_phys);
static uint64_t paging_clone_table_supervisor(uint64_t table_phys,
                                              uint32_t level,
                                              int path_user,
                                              int *ok,
                                              int *nonempty);
static int paging_validate_table(uint64_t table_phys,
                                 uint32_t level,
                                 int path_user,
                                 uint64_t ancestors[4],
                                 uint32_t ancestor_count);
        
static void paging_cpuid(uint32_t leaf,
                         uint32_t subleaf,
                         uint32_t *eax,
                         uint32_t *ebx,
                         uint32_t *ecx,
                         uint32_t *edx) {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t d = 0;

    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(leaf), "c"(subleaf));
    if (eax != 0) {
        *eax = a;
    }
    if (ebx != 0) {
        *ebx = b;
    }
    if (ecx != 0) {
        *ecx = c;
    }
    if (edx != 0) {
        *edx = d;
    }
}

static uint64_t paging_read_msr(uint32_t msr) {
    uint32_t lo = 0;
    uint32_t hi = 0;

    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void paging_write_msr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);

    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static void paging_enable_nx(void) {
    uint32_t eax = 0;
    uint32_t edx = 0;

    g_paging_nx_supported = 0;
    g_paging_nx_enabled = 0;
    paging_cpuid(0x80000000u, 0, &eax, 0, 0, 0);
    if (eax < PAGING_CPUID_EXT_FEATURES) {
        return;
    }
    paging_cpuid(PAGING_CPUID_EXT_FEATURES, 0, 0, 0, 0, &edx);
    if ((edx & PAGING_CPUID_NX_BIT) == 0) {
        return;
    }
    g_paging_nx_supported = 1;
    paging_write_msr(PAGING_MSR_EFER, paging_read_msr(PAGING_MSR_EFER) | PAGING_EFER_NXE);
    g_paging_nx_enabled = (paging_read_msr(PAGING_MSR_EFER) & PAGING_EFER_NXE) != 0;
}

static int paging_enable_pat_write_combining(void) {
    uint32_t edx = 0;
    uint64_t pat;
    uint64_t next_pat;
    uint64_t cr0;
    uint64_t rflags;
    uint64_t cr3;

    if (g_paging_pat_wc_enabled) {
        return 1;
    }
    paging_cpuid(PAGING_CPUID_BASIC_FEATURES, 0, 0, 0, 0, &edx);
    if ((edx & PAGING_CPUID_PAT_BIT) == 0) {
        return 0;
    }

    pat = paging_read_msr(PAGING_MSR_PAT);
    next_pat = (pat & ~(0xffull << (PAGING_PAT_ENTRY_WC * 8u))) |
               ((uint64_t)PAGING_MEMORY_TYPE_WC << (PAGING_PAT_ENTRY_WC * 8u));
    if (next_pat != pat) {
        __asm__ __volatile__("pushfq; popq %0" : "=r"(rflags));
        __asm__ __volatile__("cli" : : : "memory");
        __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        __asm__ __volatile__("mov %0, %%cr0"
                             :
                             : "r"((cr0 | PAGING_CR0_CD) & ~PAGING_CR0_NW)
                             : "memory");
        __asm__ __volatile__("wbinvd" : : : "memory");
        __asm__ __volatile__("mov %0, %%cr3" : : "r"(cr3) : "memory");
        paging_write_msr(PAGING_MSR_PAT, next_pat);
        __asm__ __volatile__("wbinvd" : : : "memory");
        __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0) : "memory");
        __asm__ __volatile__("mov %0, %%cr3" : : "r"(cr3) : "memory");
        if ((rflags & PAGING_RFLAGS_IF) != 0) {
            __asm__ __volatile__("sti" : : : "memory");
        }
    }
    g_paging_pat_wc_enabled = 1;
    return 1;
}

static void paging_append_kernel_range(uint64_t virt_addr, uint64_t phys_addr) {
    struct paging_kernel_range *range;

    if (g_kernel_range_count != 0) {
        range = &g_kernel_ranges[g_kernel_range_count - 1u];
        if (range->phys_end == phys_addr &&
            range->virt_start + (range->phys_end - range->phys_start) == virt_addr) {
            range->phys_end += 0x1000ull;
            return;
        }
    }

    if (g_kernel_range_count >= PAGING_KERNEL_RANGE_LIMIT) {
        return;
    }

    range = &g_kernel_ranges[g_kernel_range_count++];
    range->phys_start = phys_addr;
    range->phys_end = phys_addr + 0x1000ull;
    range->virt_start = virt_addr;
}

static void paging_build_kernel_ranges(void) {
    uint64_t kernel_start = (uint64_t)(uintptr_t)__kernel_start;
    uint64_t kernel_end = (uint64_t)(uintptr_t)__kernel_end;
    uint64_t virt = kernel_start & ~0xfffull;
    uint64_t virt_end = (kernel_end + 0xfffull) & ~0xfffull;

    g_kernel_range_count = 0;
    while (virt < virt_end) {
        uint64_t phys = 0;

        if (paging_translate_current(virt, &phys)) {
            paging_append_kernel_range(virt, phys & PAGING_ADDR_MASK);
        }
        virt += 0x1000ull;
    }
}

static int paging_kernel_phys_for_virt(uint64_t virt, uint64_t *phys_out) {
    for (uint32_t i = 0; i < g_kernel_range_count; i++) {
        const struct paging_kernel_range *range = &g_kernel_ranges[i];
        uint64_t size = range->phys_end - range->phys_start;

        if (virt >= range->virt_start &&
            virt - range->virt_start < size) {
            *phys_out = range->phys_start + (virt - range->virt_start);
            return 1;
        }
    }

    return 0;
}

static int paging_is_kernel_guard_page(uint64_t virt_addr) {
    for (uint32_t i = 0u; i < g_kernel_guard_count; i++) {
        if (g_kernel_guard_pages[i] == virt_addr) {
            return 1;
        }
    }
    return 0;
}

static int paging_is_canonical(uint64_t addr) {
    uint64_t upper = addr >> 48;

    return upper == 0u || upper == 0xffffu;
}

static int paging_is_page_aligned(uint64_t addr) {
    return (addr & 0xfffull) == 0u;
}

static int paging_checked_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == 0 || UINT64_MAX - a < b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int paging_align_up_checked(uint64_t value, uint64_t align, uint64_t *out) {
    uint64_t adjusted;

    if (align == 0 || (align & (align - 1u)) != 0u) {
        return 0;
    }
    if (!paging_checked_add(value, align - 1u, &adjusted)) {
        return 0;
    }
    *out = adjusted & ~(align - 1u);
    return 1;
}

static int paging_validate_page_address(uint64_t virt_addr) {
    return paging_is_canonical(virt_addr) && paging_is_page_aligned(virt_addr);
}

static int paging_validate_page_range(uint64_t start, uint64_t end) {
    return start < end &&
           paging_is_canonical(start) &&
           paging_is_canonical(end - 1u) &&
           paging_is_page_aligned(start) &&
           paging_is_page_aligned(end);
}

static void paging_invalidate_page(uint64_t virt_addr) {
    __asm__ __volatile__("invlpg (%0)"
                         :
                         : "r"((void *)(uintptr_t)virt_addr)
                         : "memory");
}

static int paging_walk_in_root(uint64_t root_cr3, uint64_t virt_addr, struct paging_walk_info *info_out) {
    struct paging_walk_info info = {0};
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t effective_rw = PAGING_FLAG_RW;
    uint64_t effective_user = PAGING_FLAG_USER;
    uint64_t effective_nx = 0;
    uint64_t pml4_index = (virt_addr >> 39) & 0x1ffu;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1ffu;
    uint64_t pd_index = (virt_addr >> 21) & 0x1ffu;
    uint64_t pt_index = (virt_addr >> 12) & 0x1ffu;

    if (root_cr3 == 0) {
        return 0;
    }

    info.pml4_phys = root_cr3 & PAGING_ADDR_MASK;
    pml4 = paging_root_table(root_cr3);
    info.pml4e = pml4[pml4_index];
    if ((info.pml4e & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    effective_rw &= info.pml4e & PAGING_FLAG_RW;
    effective_user &= info.pml4e & PAGING_FLAG_USER;
    effective_nx |= info.pml4e & PAGING_FLAG_NX;
    info.flags = PAGING_FLAG_PRESENT | effective_rw | effective_user | effective_nx;
    info.pdpt_phys = info.pml4e & PAGING_ADDR_MASK;
    pdpt = paging_table_from_entry(info.pml4e);
    info.pdpte = pdpt[pdpt_index];
    if ((info.pdpte & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    effective_rw &= info.pdpte & PAGING_FLAG_RW;
    effective_user &= info.pdpte & PAGING_FLAG_USER;
    effective_nx |= info.pdpte & PAGING_FLAG_NX;
    info.flags = PAGING_FLAG_PRESENT | effective_rw | effective_user | effective_nx;
    if (info.pdpte & PAGING_FLAG_PAGE_SIZE) {
        info.phys_addr = (info.pdpte & 0x000fffffc0000000ull) | (virt_addr & 0x3fffffffull);
        if (info_out != 0) {
            *info_out = info;
        }
        return 1;
    }

    info.pd_phys = info.pdpte & PAGING_ADDR_MASK;
    pd = paging_table_from_entry(info.pdpte);
    info.pde = pd[pd_index];
    if ((info.pde & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    effective_rw &= info.pde & PAGING_FLAG_RW;
    effective_user &= info.pde & PAGING_FLAG_USER;
    effective_nx |= info.pde & PAGING_FLAG_NX;
    info.flags = PAGING_FLAG_PRESENT | effective_rw | effective_user | effective_nx;
    if (info.pde & PAGING_FLAG_PAGE_SIZE) {
        info.phys_addr = (info.pde & 0x000fffffffe00000ull) | (virt_addr & 0x1fffffull);
        if (info_out != 0) {
            *info_out = info;
        }
        return 1;
    }

    info.pt_phys = info.pde & PAGING_ADDR_MASK;
    pt = paging_table_from_entry(info.pde);
    info.pte = pt[pt_index];
    if ((info.pte & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }

    effective_rw &= info.pte & PAGING_FLAG_RW;
    effective_user &= info.pte & PAGING_FLAG_USER;
    effective_nx |= info.pte & PAGING_FLAG_NX;
    info.flags = PAGING_FLAG_PRESENT | effective_rw | effective_user | effective_nx;
    info.phys_addr = (info.pte & PAGING_ADDR_MASK) | (virt_addr & 0xfffull);
    if (info_out != 0) {
        *info_out = info;
    }
    return 1;
}

static uint64_t *paging_root_table(uint64_t cr3) {
    return (uint64_t *)paging_phys_direct_map(cr3 & PAGING_ADDR_MASK);
}

static int paging_translate_in_root(uint64_t cr3, uint64_t virt_addr, uint64_t *phys_out) {
    struct paging_walk_info info;

    if (!paging_walk_in_root(cr3, virt_addr, &info)) {
        return 0;
    }
    if (phys_out != 0) {
        *phys_out = info.phys_addr;
    }
    return 1;
}

static int paging_translate_current(uint64_t virt_addr, uint64_t *phys_out) {
    uint64_t cr3;

    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return paging_translate_in_root(cr3, virt_addr, phys_out);
}

uint64_t paging_get_current_cr3(void) {
    uint64_t cr3;

    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGING_ADDR_MASK;
}

void paging_set_current_cr3(uint64_t cr3) {
    uint64_t next_cr3 = cr3 & PAGING_ADDR_MASK;
    uint64_t previous_cr3 = paging_get_current_cr3();
    uint64_t current_rsp;
    uint64_t current_rip = (uint64_t)(uintptr_t)&&paging_after_switch;
    uint32_t reject_flags = 0;

    g_last_switch_trace.requested_cr3 = next_cr3;
    g_last_switch_trace.previous_cr3 = previous_cr3;
    g_last_switch_trace.actual_cr3 = previous_cr3;
    g_last_switch_trace.current_rip = current_rip;
    g_last_switch_trace.current_rsp = 0;
    g_last_switch_trace.reject_flags = 0;

    if (next_cr3 == 0) {
        g_last_switch_trace.reject_flags = PAGING_SWITCH_REJECT_ZERO;
        return;
    }
    if (next_cr3 == previous_cr3) {
        g_last_switch_trace.actual_cr3 = previous_cr3;
        g_last_switch_trace.reject_flags = 0;
        return;
    }
    __asm__ __volatile__("mov %%rsp, %0" : "=r"(current_rsp));
    g_last_switch_trace.current_rsp = current_rsp;
    if (!paging_translate_in_root(next_cr3, current_rip, 0)) {
        reject_flags |= PAGING_SWITCH_REJECT_RIP_UNMAPPED;
    }
    if (!paging_translate_in_root(next_cr3, current_rsp, 0)) {
        reject_flags |= PAGING_SWITCH_REJECT_RSP_UNMAPPED;
    }
    if (reject_flags != 0) {
        g_last_switch_trace.reject_flags = reject_flags;
        return;
    }

    __asm__ __volatile__("mov %0, %%cr3" : : "r"(next_cr3) : "memory");
    g_last_switch_trace.actual_cr3 = paging_get_current_cr3();
    g_last_switch_trace.reject_flags = 0;

paging_after_switch:
    return;
}

static void paging_flush_tlb(void) {
    uint64_t cr3;

    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

void *paging_phys_direct_map(uint64_t phys) {
    uint64_t page = phys & PAGING_ADDR_MASK;
    uint64_t page_off = phys & ~PAGING_ADDR_MASK;

    for (uint32_t i = 0; i < g_kernel_range_count; i++) {
        const struct paging_kernel_range *range = &g_kernel_ranges[i];

        if (page >= range->phys_start && page < range->phys_end) {
            return (void *)(uintptr_t)(range->virt_start + (page - range->phys_start) + page_off);
        }
    }
    return (void *)(uintptr_t)phys;
}

static void paging_walk_rollback(uint64_t *pml4_entry,
                                 uint64_t *pdpt_entry,
                                 uint64_t *pd_entry,
                                 uint64_t new_pdpt_phys,
                                 uint64_t new_pd_phys,
                                 uint64_t new_pt_phys) {
    if (new_pt_phys != 0u) {
        if (pd_entry != 0 &&
            (*pd_entry & PAGING_ADDR_MASK) == new_pt_phys) {
            *pd_entry = 0;
        }

        (void)pmm_free_page(new_pt_phys);
    }

    if (new_pd_phys != 0u) {
        if (pdpt_entry != 0 &&
            (*pdpt_entry & PAGING_ADDR_MASK) == new_pd_phys) {
            *pdpt_entry = 0;
        }

        (void)pmm_free_page(new_pd_phys);
    }

    if (new_pdpt_phys != 0u) {
        if (pml4_entry != 0 &&
            (*pml4_entry & PAGING_ADDR_MASK) == new_pdpt_phys) {
            *pml4_entry = 0;
        }

        (void)pmm_free_page(new_pdpt_phys);
    }
}

static uint64_t *paging_alloc_table(uint64_t *phys_out) {
    uint64_t phys_addr;
    uint64_t *table;

    if (phys_out == 0) {
        return 0;
    }

    phys_addr = pmm_alloc_page();
    if (phys_addr == 0) {
        return 0;
    }
    table = (uint64_t *)paging_phys_direct_map(phys_addr);
    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        table[i] = 0;
    }
    *phys_out = phys_addr;
    return table;
}

static uint64_t *paging_table_from_entry(uint64_t entry) {
    return (uint64_t *)paging_phys_direct_map(entry & PAGING_ADDR_MASK);
}

static int paging_table_is_empty(const uint64_t *table) {
    if (table == 0) {
        return 1;
    }

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        if ((table[i] & PAGING_FLAG_PRESENT) != 0u) {
            return 0;
        }
    }

    return 1;
}

static uint64_t paging_clone_table_deep(uint64_t table_phys, uint32_t level) {
    uint64_t *src_table;
    uint64_t *dst_table;
    uint64_t dst_phys = 0;

    if (table_phys == 0 || level == 0) {
        return 0;
    }

    src_table = (uint64_t *)paging_phys_direct_map(table_phys & PAGING_ADDR_MASK);
    dst_table = paging_alloc_table(&dst_phys);
    if (dst_table == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        uint64_t entry = src_table[i];

        if ((entry & PAGING_FLAG_PRESENT) == 0 || level == 1 || (entry & PAGING_FLAG_PAGE_SIZE) != 0) {
            dst_table[i] = entry;
            continue;
        }

        {
            uint64_t cloned_child_phys = paging_clone_table_deep(entry & PAGING_ADDR_MASK, level - 1);

            if (cloned_child_phys == 0) {
                paging_destroy_table_deep(dst_phys, level);
                return 0;
            }
            dst_table[i] = (entry & ~PAGING_ADDR_MASK) | cloned_child_phys;
        }
    }

    return dst_phys;
}

static uint64_t paging_clone_table_supervisor(uint64_t table_phys,
                                              uint32_t level,
                                              int path_user,
                                              int *ok,
                                              int *nonempty) {
    uint64_t *src_table;
    uint64_t *dst_table;
    uint64_t dst_phys = 0;
    int any = 0;

    if (ok == 0 || nonempty == 0 ||
        table_phys == 0 || level == 0 || !*ok) {
        if (ok != 0) {
            *ok = 0;
        }

        return 0;
    }

    *nonempty = 0;

    src_table =
        (uint64_t *)paging_phys_direct_map(table_phys & PAGING_ADDR_MASK);

    dst_table = paging_alloc_table(&dst_phys);
    if (dst_table == 0) {
        *ok = 0;
        return 0;
    }

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        uint64_t entry = src_table[i];
        int entry_user;

        if ((entry & PAGING_FLAG_PRESENT) == 0) {
            continue;
        }

        entry_user =
            path_user && ((entry & PAGING_FLAG_USER) != 0);

        if (level == 1 ||
            (entry & PAGING_FLAG_PAGE_SIZE) != 0) {
            /*
             * Effective USER mapping은 새 process에 가져오지 않는다.
             */
            if (entry_user) {
                continue;
            }

            /*
             * 상위 entry 때문에 supervisor인 mapping이라면
             * leaf USER bit도 제거해서 ownership 의미를 명확하게 한다.
             */
            if (!path_user) {
                entry &= ~PAGING_FLAG_USER;
            }

            dst_table[i] = entry;
            any = 1;
            continue;
        }

        {
            uint64_t child_phys;
            int child_nonempty = 0;

            child_phys =
                paging_clone_table_supervisor(
                    entry & PAGING_ADDR_MASK,
                    level - 1u,
                    entry_user,
                    ok,
                    &child_nonempty);

            if (!*ok) {
                paging_destroy_table_deep(dst_phys, level);
                return 0;
            }

            if (!child_nonempty) {
                continue;
            }

            if (!path_user) {
                entry &= ~PAGING_FLAG_USER;
            }

            dst_table[i] =
                (entry & ~PAGING_ADDR_MASK) |
                child_phys;

            any = 1;
        }
    }

    if (!any) {
        (void)pmm_free_page(dst_phys);
        return 0;
    }

    *nonempty = 1;
    return dst_phys;
}

static uint64_t paging_clone_table_cow(uint64_t table_phys,
                                       uint32_t level,
                                       int path_user,
                                       int *ok) {
    uint64_t *src_table;
    uint64_t *dst_table;
    uint64_t dst_phys = 0;

    if (table_phys == 0 || level == 0 ||
        ok == 0 || !*ok) {
        return 0;
    }

    src_table = (uint64_t *)paging_phys_direct_map(
        table_phys & PAGING_ADDR_MASK);

    dst_table = paging_alloc_table(&dst_phys);
    if (dst_table == 0) {
        *ok = 0;
        return 0;
    }

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        uint64_t entry = src_table[i];
        int effective_user;

        if ((entry & PAGING_FLAG_PRESENT) == 0) {
            continue;
        }

        effective_user =
            path_user &&
            ((entry & PAGING_FLAG_USER) != 0);

        if (level == 1 ||
            (entry & PAGING_FLAG_PAGE_SIZE) != 0) {
            uint64_t phys = entry & PAGING_ADDR_MASK;

            /*
             * 현재 COW/refcount 정책은 4K user leaf만 관리한다.
             * effective user huge page가 생기면 silent 공유 대신
             * clone을 명시적으로 거부한다.
             */
            if (effective_user &&
                (entry & PAGING_FLAG_PAGE_SIZE) != 0u) {
                *ok = 0;
                break;
            }

            if (level == 1 &&
                effective_user &&
                pmm_ref_count(phys) != 0u) {
                if (!pmm_retain_page(phys)) {
                    *ok = 0;
                    break;
                }
            }

            /*
             * 중요:
             * 이 단계에서는 source PTE를 절대 변경하지 않는다.
             */
            dst_table[i] = entry;
            continue;
        }

        {
            uint64_t child;

            child = paging_clone_table_cow(
                entry & PAGING_ADDR_MASK,
                level - 1u,
                effective_user,
                ok);

            if (!*ok || child == 0) {
                *ok = 0;
                break;
            }

            dst_table[i] =
                (entry & ~PAGING_ADDR_MASK) |
                child;
        }
    }

    if (!*ok) {
        paging_destroy_table_deep(dst_phys, level);
        return 0;
    }

    return dst_phys;
}

static void paging_mark_clone_cow_pair(uint64_t source_phys,
                                       uint64_t clone_phys,
                                       uint32_t level,
                                       int path_user) {
    uint64_t *src_table;
    uint64_t *dst_table;

    if (source_phys == 0 ||
        clone_phys == 0 ||
        level == 0) {
        return;
    }

    src_table = (uint64_t *)paging_phys_direct_map(
        source_phys & PAGING_ADDR_MASK);

    dst_table = (uint64_t *)paging_phys_direct_map(
        clone_phys & PAGING_ADDR_MASK);

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        uint64_t src_entry = src_table[i];
        uint64_t dst_entry = dst_table[i];
        int effective_user;

        if ((src_entry & PAGING_FLAG_PRESENT) == 0 ||
            (dst_entry & PAGING_FLAG_PRESENT) == 0) {
            continue;
        }

        effective_user =
            path_user &&
            ((src_entry & PAGING_FLAG_USER) != 0);

        if (level == 1) {
            uint64_t phys;

            if (!effective_user) {
                continue;
            }

            phys = src_entry & PAGING_ADDR_MASK;

            /*
             * PMM이 ownership을 추적하는 user frame만
             * COW 대상으로 취급한다.
             */
            if (pmm_ref_count(phys) == 0u) {
                continue;
            }

            if ((src_entry & PAGING_FLAG_RW) != 0u) {
                src_entry =
                    (src_entry & ~PAGING_FLAG_RW) |
                    PAGING_FLAG_COW;

                dst_entry =
                    (dst_entry & ~PAGING_FLAG_RW) |
                    PAGING_FLAG_COW;

                src_table[i] = src_entry;
                dst_table[i] = dst_entry;
            }

            continue;
        }

        /*
         * 현재 user huge-page COW는 지원하지 않는다.
         */
        if ((src_entry & PAGING_FLAG_PAGE_SIZE) != 0u ||
            (dst_entry & PAGING_FLAG_PAGE_SIZE) != 0u) {
            continue;
        }

        paging_mark_clone_cow_pair(
            src_entry & PAGING_ADDR_MASK,
            dst_entry & PAGING_ADDR_MASK,
            level - 1u,
            effective_user);
    }
}

static void paging_destroy_table_deep(uint64_t table_phys, uint32_t level) {
    paging_destroy_table_deep_internal(table_phys, level, 1);
}

static void paging_destroy_table_deep_internal(uint64_t table_phys,
                                               uint32_t level,
                                               int path_user) {
    uint64_t *table;

    if (table_phys == 0 || level == 0) {
        return;
    }

    table = (uint64_t *)paging_phys_direct_map(
        table_phys & PAGING_ADDR_MASK);

    if (level > 1) {
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            uint64_t entry = table[i];
            int child_user;

            if ((entry & PAGING_FLAG_PRESENT) == 0) {
                continue;
            }

            /*
             * Huge page는 하위 page table이 아니므로
             * 재귀적으로 따라가면 안 된다.
             */
            if ((entry & PAGING_FLAG_PAGE_SIZE) != 0) {
                continue;
            }

            child_user =
                path_user &&
                ((entry & PAGING_FLAG_USER) != 0);

            paging_destroy_table_deep_internal(
                entry & PAGING_ADDR_MASK,
                level - 1u,
                child_user);
        }
    } else {
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            uint64_t entry = table[i];
            uint64_t phys;
            int effective_user;

            if ((entry & PAGING_FLAG_PRESENT) == 0) {
                continue;
            }

            effective_user =
                path_user &&
                ((entry & PAGING_FLAG_USER) != 0);

            if (!effective_user) {
                continue;
            }

            phys = entry & PAGING_ADDR_MASK;

            if (pmm_ref_count(phys) != 0u) {
                (void)pmm_release_page(phys);
            }
        }
    }

    (void)pmm_free_page(table_phys & PAGING_ADDR_MASK);
}

static int paging_validate_table(uint64_t table_phys,
                                 uint32_t level,
                                 int path_user,
                                 uint64_t ancestors[4],
                                 uint32_t ancestor_count) {
    uint64_t *table;

    if (table_phys == 0 ||
        (table_phys & 0xfffull) != 0u ||
        level == 0 ||
        level > 4 ||
        ancestor_count >= 4u ||
        pmm_ref_count(table_phys & PAGING_ADDR_MASK) == 0u) {
        return 0;
    }

    for (uint32_t i = 0; i < ancestor_count; i++) {
        if (ancestors[i] == (table_phys & PAGING_ADDR_MASK)) {
            return 0;
        }
    }

    ancestors[ancestor_count++] = table_phys & PAGING_ADDR_MASK;
    table = (uint64_t *)paging_phys_direct_map(table_phys & PAGING_ADDR_MASK);

    for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
        uint64_t entry = table[i];
        uint64_t phys;
        int entry_user;

        if ((entry & PAGING_FLAG_PRESENT) == 0u) {
            continue;
        }

        if ((entry & PAGING_FLAG_COW) != 0u &&
            (entry & PAGING_FLAG_RW) != 0u) {
            return 0;
        }

        entry_user = path_user && ((entry & PAGING_FLAG_USER) != 0u);

        if ((entry & PAGING_FLAG_PAGE_SIZE) != 0u && level > 1u) {
            if (level == 4u) {
                return 0;
            }
            if (level == 3u &&
                (entry & 0x000000003ffff000ull) != 0u) {
                return 0;
            }
            if (level == 2u &&
                (entry & 0x00000000001ff000ull) != 0u) {
                return 0;
            }
            if (entry_user &&
                (entry & PAGING_FLAG_COW) != 0u) {
                return 0;
            }
            continue;
        }

        phys = entry & PAGING_ADDR_MASK;
        if (phys == 0 || (phys & 0xfffull) != 0u) {
            return 0;
        }

        if (level == 1u) {
            if ((entry & PAGING_FLAG_USER) != 0u && !path_user) {
                return 0;
            }
            continue;
        }

        if ((entry & PAGING_FLAG_USER) != 0u && !path_user) {
            return 0;
        }

        if (!paging_validate_table(phys,
                                   level - 1u,
                                   entry_user,
                                   ancestors,
                                   ancestor_count)) {
            return 0;
        }
    }

    return 1;
}

int paging_validate_root(uint64_t root_cr3) {
    uint64_t ancestors[4] = {0};
    uint64_t root_phys = root_cr3 & PAGING_ADDR_MASK;

    if (root_phys == 0 ||
        (root_cr3 & ~PAGING_ADDR_MASK) != 0u) {
        return 0;
    }

    return paging_validate_table(root_phys, 4, 1, ancestors, 0);
}

void paging_init(uint64_t kernel_phys_base) {
    g_kernel_phys_base = kernel_phys_base;
    paging_enable_nx();
    paging_build_kernel_ranges();
    paging_set_range_write_flag((uint64_t)(uintptr_t)__kernel_text_start,
                                (uint64_t)(uintptr_t)__kernel_text_end,
                                0);
    paging_set_range_write_flag((uint64_t)(uintptr_t)__kernel_data_start,
                                (uint64_t)(uintptr_t)__kernel_data_end,
                                1);
    paging_set_range_write_flag((uint64_t)(uintptr_t)__kernel_bss_start,
                                (uint64_t)(uintptr_t)__kernel_bss_end,
                                1);
    paging_set_range_write_flag((uint64_t)(uintptr_t)__userelf_start,
                                (uint64_t)(uintptr_t)__userelf_stack_top,
                                1);
    if (g_paging_nx_enabled) {
        paging_set_range_execute_flag((uint64_t)(uintptr_t)__kernel_text_start,
                                      (uint64_t)(uintptr_t)__kernel_text_end,
                                      1);
        paging_set_range_execute_flag((uint64_t)(uintptr_t)__kernel_data_start,
                                      (uint64_t)(uintptr_t)__kernel_data_end,
                                      0);
        paging_set_range_execute_flag((uint64_t)(uintptr_t)__kernel_bss_start,
                                      (uint64_t)(uintptr_t)__kernel_bss_end,
                                      0);
        paging_set_range_execute_flag((uint64_t)(uintptr_t)__userelf_start,
                                      (uint64_t)(uintptr_t)__userelf_stack_top,
                                      0);
    }
}

int paging_guard_kernel_page(uint64_t virt_addr) {
    uint64_t root = paging_get_current_cr3();
    uint64_t phys;
    uint64_t expected_phys;
    uint64_t *pte;

    if (root == 0u ||
        g_kernel_guard_count >= PAGING_KERNEL_GUARD_LIMIT ||
        paging_is_kernel_guard_page(virt_addr) ||
        !paging_validate_page_address(virt_addr) ||
        virt_addr < (uint64_t)(uintptr_t)__kernel_bss_start ||
        virt_addr >= (uint64_t)(uintptr_t)__kernel_bss_end ||
        !paging_kernel_phys_for_virt(virt_addr, &expected_phys) ||
        !paging_translate_in_root(root, virt_addr, &phys) ||
        phys != expected_phys) {
        return 0;
    }

    pte = paging_walk_to_pte_in_root(root, virt_addr, 1, 0);
    if (pte == 0 ||
        (*pte & (PAGING_FLAG_PRESENT | PAGING_FLAG_USER)) != PAGING_FLAG_PRESENT ||
        (*pte & PAGING_ADDR_MASK) != phys) {
        return 0;
    }

    *pte = 0u;
    paging_invalidate_page(virt_addr);
    g_kernel_guard_pages[g_kernel_guard_count++] = virt_addr;
    return 1;
}

static uint64_t *paging_walk_to_pte_in_root(uint64_t root_cr3,
                                            uint64_t addr,
                                            int create,
                                            int user_accessible) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t *pml4_entry = 0;
    uint64_t *pdpt_entry = 0;
    uint64_t *pd_entry = 0;
    uint64_t new_pdpt_phys = 0;
    uint64_t new_pd_phys = 0;
    uint64_t new_pt_phys = 0;
    uint64_t old_pdpt_entry = 0;
    uint64_t old_pd_entry = 0;
    uint64_t *split_pdpt_entry = 0;
    uint64_t *split_pd_entry = 0;
    uint64_t entry_flags = PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
    uint64_t pml4_index = (addr >> 39) & 0x1ffu;
    uint64_t pdpt_index = (addr >> 30) & 0x1ffu;
    uint64_t pd_index = (addr >> 21) & 0x1ffu;
    uint64_t pt_index = (addr >> 12) & 0x1ffu;

    if (user_accessible) {
        entry_flags |= PAGING_FLAG_USER;
    }

    if (root_cr3 == 0 ||
        !paging_validate_page_address(addr)) {
        return 0;
    }

    pml4 = paging_root_table(root_cr3);
    pml4_entry = &pml4[pml4_index];

    if ((*pml4_entry & PAGING_FLAG_PRESENT) == 0) {
        uint64_t *new_pdpt;

        if (!create) {
            return 0;
        }

        new_pdpt = paging_alloc_table(&new_pdpt_phys);
        if (new_pdpt == 0) {
            goto fail;
        }

        *pml4_entry = new_pdpt_phys | entry_flags;
    } else if (create && user_accessible) {
        *pml4_entry |= PAGING_FLAG_USER;
    }

    pdpt = paging_table_from_entry(*pml4_entry);
    pdpt_entry = &pdpt[pdpt_index];

    if ((*pdpt_entry & PAGING_FLAG_PRESENT) == 0) {
        uint64_t *new_pd;

        if (!create) {
            return 0;
        }

        new_pd = paging_alloc_table(&new_pd_phys);
        if (new_pd == 0) {
            goto fail;
        }

        *pdpt_entry = new_pd_phys | entry_flags;
    } else if (create && user_accessible) {
        *pdpt_entry |= PAGING_FLAG_USER;
    }

    if ((*pdpt_entry & PAGING_FLAG_PAGE_SIZE) != 0u) {
        uint64_t *new_pd;
        uint64_t pdpt_base;
        uint64_t flags;

        if (!create) {
            return 0;
        }

        new_pd = paging_alloc_table(&new_pd_phys);
        if (new_pd == 0) {
            goto fail;
        }

        old_pdpt_entry = *pdpt_entry;
        split_pdpt_entry = pdpt_entry;
        pdpt_base = old_pdpt_entry & 0x000fffffc0000000ull;
        flags = (old_pdpt_entry & ~0x000fffffc0000000ull) & ~PAGING_FLAG_PAGE_SIZE;
        flags |= PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
        if (user_accessible) {
            flags |= PAGING_FLAG_USER;
        }

        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            new_pd[i] = (pdpt_base + (uint64_t)i * 0x200000ull) |
                        flags |
                        PAGING_FLAG_PAGE_SIZE;
        }
        *pdpt_entry = new_pd_phys | flags;
    }

    pd = paging_table_from_entry(*pdpt_entry);
    pd_entry = &pd[pd_index];

    if ((*pd_entry & PAGING_FLAG_PRESENT) == 0) {
        uint64_t *new_pt;

        if (!create) {
            return 0;
        }

        new_pt = paging_alloc_table(&new_pt_phys);
        if (new_pt == 0) {
            goto fail;
        }

        *pd_entry = new_pt_phys | entry_flags;
    } else if (create && user_accessible) {
        *pd_entry |= PAGING_FLAG_USER;
    }

    if ((*pd_entry & PAGING_FLAG_PAGE_SIZE) != 0u) {
        uint64_t *new_pt;
        uint64_t pd_base;
        uint64_t flags;

        if (!create) {
            return 0;
        }

        new_pt = paging_alloc_table(&new_pt_phys);
        if (new_pt == 0) {
            goto fail;
        }

        old_pd_entry = *pd_entry;
        split_pd_entry = pd_entry;
        pd_base = old_pd_entry & 0x000fffffffe00000ull;
        flags = (old_pd_entry & ~0x000fffffffe00000ull) & ~PAGING_FLAG_PAGE_SIZE;
        flags |= PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
        if (user_accessible) {
            flags |= PAGING_FLAG_USER;
        }

        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            new_pt[i] = (pd_base + (uint64_t)i * 0x1000ull) | flags;
        }
        *pd_entry = new_pt_phys | flags;
    }

    pt = paging_table_from_entry(*pd_entry);
    return &pt[pt_index];

fail:
    if (split_pd_entry != 0 &&
        (*split_pd_entry & PAGING_ADDR_MASK) == new_pt_phys) {
        *split_pd_entry = old_pd_entry;
        (void)pmm_free_page(new_pt_phys);
        new_pt_phys = 0;
    }
    if (split_pdpt_entry != 0 &&
        (*split_pdpt_entry & PAGING_ADDR_MASK) == new_pd_phys) {
        *split_pdpt_entry = old_pdpt_entry;
        (void)pmm_free_page(new_pd_phys);
        new_pd_phys = 0;
    }
    paging_walk_rollback(pml4_entry,
                         pdpt_entry,
                         pd_entry,
                         new_pdpt_phys,
                         new_pd_phys,
                         new_pt_phys);
    return 0;
}

static int paging_map_page_in_root(uint64_t root_cr3,
                                   uint64_t virt_addr,
                                   uint64_t phys_addr,
                                   int user_accessible,
                                   int writable,
                                   int executable) {
    uint64_t *pte;
    uint64_t entry_flags = PAGING_FLAG_PRESENT;

    if (root_cr3 == 0 ||
        paging_is_kernel_guard_page(virt_addr) ||
        !paging_validate_page_address(virt_addr) ||
        !paging_is_page_aligned(phys_addr)) {
        return 0;
    }

    pte = paging_walk_to_pte_in_root(root_cr3, virt_addr, 1, user_accessible);
    if (pte == 0) {
        return 0;
    }
    if (writable) {
        entry_flags |= PAGING_FLAG_RW;
    }
    if (user_accessible) {
        entry_flags |= PAGING_FLAG_USER;
    }
    if (g_paging_nx_enabled && !executable) {
        entry_flags |= PAGING_FLAG_NX;
    }
    *pte = (phys_addr & PAGING_ADDR_MASK) | entry_flags;
    return 1;
}

uint64_t paging_create_kernel_root(void) {
    uint64_t root_phys = 0;
    uint64_t *pml4 = paging_alloc_table(&root_phys);
    uint64_t pdpt_phys = 0;
    uint64_t *pdpt;
    uint64_t kernel_start = (uint64_t)(uintptr_t)__kernel_start;
    uint64_t kernel_end = (uint64_t)(uintptr_t)__kernel_end;

    if (pml4 == 0) {
        return 0;
    }

    pdpt = paging_alloc_table(&pdpt_phys);
    if (pdpt == 0) {
        (void)pmm_free_page(root_phys);
        return 0;
    }
    pml4[0] = pdpt_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;

    for (uint32_t table = 0; table < 4; table++) {
        uint64_t pd_phys = 0;
        uint64_t *pd = paging_alloc_table(&pd_phys);

        if (pd == 0) {
            goto fail;
        }
        pdpt[table] = pd_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            uint64_t base = ((uint64_t)table * PAGING_TABLE_ENTRIES + (uint64_t)i) * 0x200000ull;
            pd[i] = base | PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_PAGE_SIZE;
        }
    }

    for (uint64_t virt = kernel_start; virt < kernel_end; virt += 0x1000ull) {
        uint64_t phys;

        if (paging_is_kernel_guard_page(virt) ||
            !paging_kernel_phys_for_virt(virt, &phys)) {
            continue;
        }
        if (!paging_map_page_in_root(root_phys, virt, phys, 0, 1, 1)) {
            goto fail;
        }
    }

    return root_phys;

fail:
    paging_destroy_table_deep(root_phys, 4);
    return 0;
}

uint64_t paging_clone_root_deep(uint64_t source_cr3) {
    return paging_clone_table_deep(source_cr3 & PAGING_ADDR_MASK, 4);
}

uint64_t paging_clone_current_root_deep(void) {
    return paging_clone_root_deep(paging_get_current_cr3());
}

uint64_t paging_clone_root_cow(uint64_t source_cr3) {
    uint64_t source_phys;
    uint64_t clone;
    int ok = 1;

    source_phys = source_cr3 & PAGING_ADDR_MASK;

    if (source_phys == 0) {
        return 0;
    }

    clone = paging_clone_table_cow(
        source_phys,
        4,
        1,
        &ok);

    if (!ok || clone == 0) {
        return 0;
    }

    /*
     * 여기까지 왔다는 것은 모든 page-table allocation과
     * PMM retain이 성공했다는 뜻이다.
     *
     * 이제 source + clone을 동시에 COW로 전환한다.
     */
    paging_mark_clone_cow_pair(
        source_phys,
        clone,
        4,
        1);

    if (source_phys == paging_get_current_cr3()) {
        paging_flush_tlb();
    }

    return clone;
}

int paging_resolve_cow_fault(uint64_t root_cr3, uint64_t fault_addr, uint64_t error_code) {
    uint64_t page = fault_addr & PAGING_ADDR_MASK;
    uint64_t *pte;
    uint64_t entry;
    uint64_t old_phys;
    uint32_t refs;

    if ((error_code & 0x7u) != 0x7u) {
        return 0;
    }
    pte = paging_walk_to_pte_in_root(root_cr3, page, 0, 1);
    if (pte == 0) {
        return 0;
    }
    entry = *pte;
    if ((entry & (PAGING_FLAG_PRESENT | PAGING_FLAG_USER | PAGING_FLAG_COW)) !=
        (PAGING_FLAG_PRESENT | PAGING_FLAG_USER | PAGING_FLAG_COW)) {
        return 0;
    }
    old_phys = entry & PAGING_ADDR_MASK;
    refs = pmm_ref_count(old_phys);
    if (refs == 0) {
        return 0;
    }
    if (refs == 1u) {
        *pte = (entry | PAGING_FLAG_RW) & ~PAGING_FLAG_COW;
    } else {
        uint64_t new_phys = pmm_alloc_page();

        if (new_phys == 0) {
            return 0;
        }
        memcpy(paging_phys_direct_map(new_phys),
               paging_phys_direct_map(old_phys),
               4096u);
        *pte = (entry & ~(PAGING_ADDR_MASK | PAGING_FLAG_COW)) |
               new_phys | PAGING_FLAG_RW;
        if (!pmm_release_page(old_phys)) {
            *pte = entry;
            (void)pmm_release_page(new_phys);
            return 0;
        }
    }
    if ((root_cr3 & PAGING_ADDR_MASK) == paging_get_current_cr3()) {
        paging_invalidate_page(page);
    }
    return 1;
}

uint64_t paging_create_user_root(void) {
    uint64_t source_cr3;
    uint64_t dst_pml4_phys;
    uint64_t *dst_pml4;
    uint64_t *src_pml4;
    int ok = 1;
    int nonempty = 0;

    source_cr3 = paging_get_current_cr3();

    dst_pml4_phys =
        paging_clone_table_supervisor(
            source_cr3 & PAGING_ADDR_MASK,
            4,
            1,
            &ok,
            &nonempty);

    if (!ok || !nonempty || dst_pml4_phys == 0) {
        return 0;
    }

    dst_pml4 =
        (uint64_t *)paging_phys_direct_map(dst_pml4_phys);

    src_pml4 = paging_root_table(source_cr3);

    g_last_clone_trace.source_cr3 = source_cr3;
    g_last_clone_trace.clone_cr3 = dst_pml4_phys;
    g_last_clone_trace.source_pml4e0 = src_pml4[0];
    g_last_clone_trace.source_pml4e511 = src_pml4[511];
    g_last_clone_trace.clone_pml4e0 = dst_pml4[0];
    g_last_clone_trace.clone_pml4e511 = dst_pml4[511];
    g_last_clone_trace.fail_virt = 0;
    g_last_clone_trace.fail_phys = 0;
    g_last_clone_trace.fail_stage = 0;

    return dst_pml4_phys;
}

void paging_destroy_root_deep(uint64_t root_cr3) {
    uint64_t root_phys = root_cr3 & PAGING_ADDR_MASK;

    if (root_phys == 0 || root_phys == paging_get_current_cr3()) {
        return;
    }
    paging_destroy_table_deep(root_phys, 4);
}

static uint64_t *paging_walk_to_pte(uint64_t addr, int create) {
    return paging_walk_to_pte_in_root(paging_get_current_cr3(), addr, create, create);
}

static void paging_set_range_user_flag(uint64_t start, uint64_t end, int user_accessible) {
    uint64_t page = start & ~0xfffull;

    while (page < end) {
        uint64_t cr3;
        uint64_t *pml4;
        uint64_t *pdpt;
        uint64_t *pd;
        uint64_t *pt;
        uint64_t pml4_index = (page >> 39) & 0x1ffu;
        uint64_t pdpt_index = (page >> 30) & 0x1ffu;
        uint64_t pd_index = (page >> 21) & 0x1ffu;
        uint64_t pt_index = (page >> 12) & 0x1ffu;

        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        pml4 = paging_root_table(cr3);
        if ((pml4[pml4_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }

        pdpt = paging_table_from_entry(pml4[pml4_index]);
        if ((pdpt[pdpt_index] & PAGING_FLAG_PRESENT) == 0 || (pdpt[pdpt_index] & PAGING_FLAG_PAGE_SIZE)) {
            page += 0x1000ull;
            continue;
        }

        pd = paging_table_from_entry(pdpt[pdpt_index]);
        if ((pd[pd_index] & PAGING_FLAG_PRESENT) == 0 || (pd[pd_index] & PAGING_FLAG_PAGE_SIZE)) {
            page += 0x1000ull;
            continue;
        }

        pt = paging_table_from_entry(pd[pd_index]);
        if ((pt[pt_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }

        if (user_accessible) {
            pml4[pml4_index] |= PAGING_FLAG_USER;
            pdpt[pdpt_index] |= PAGING_FLAG_USER;
            pd[pd_index] |= PAGING_FLAG_USER;
            pt[pt_index] |= PAGING_FLAG_USER;
        } else {
            pt[pt_index] &= ~PAGING_FLAG_USER;
        }
        paging_invalidate_page(page);
        page += 0x1000ull;
    }
}

static void paging_set_range_execute_flag(uint64_t start, uint64_t end, int executable) {
    uint64_t page = start & ~0xfffull;

    if (!g_paging_nx_enabled) {
        return;
    }
    while (page < end) {
        uint64_t cr3;
        uint64_t *pml4;
        uint64_t *pdpt;
        uint64_t *pd;
        uint64_t *pt;
        uint64_t pml4_index = (page >> 39) & 0x1ffu;
        uint64_t pdpt_index = (page >> 30) & 0x1ffu;
        uint64_t pd_index = (page >> 21) & 0x1ffu;
        uint64_t pt_index = (page >> 12) & 0x1ffu;

        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        pml4 = paging_root_table(cr3);
        if ((pml4[pml4_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }

        pdpt = paging_table_from_entry(pml4[pml4_index]);
        if ((pdpt[pdpt_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }
        if (pdpt[pdpt_index] & PAGING_FLAG_PAGE_SIZE) {
            if (executable) {
                pdpt[pdpt_index] &= ~PAGING_FLAG_NX;
            } else {
                pdpt[pdpt_index] |= PAGING_FLAG_NX;
            }
            paging_invalidate_page(page);
            page += 0x1000ull;
            continue;
        }

        pd = paging_table_from_entry(pdpt[pdpt_index]);
        if ((pd[pd_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }
        if (pd[pd_index] & PAGING_FLAG_PAGE_SIZE) {
            if (executable) {
                pd[pd_index] &= ~PAGING_FLAG_NX;
            } else {
                pd[pd_index] |= PAGING_FLAG_NX;
            }
            paging_invalidate_page(page);
            page += 0x1000ull;
            continue;
        }

        pt = paging_table_from_entry(pd[pd_index]);
        if ((pt[pt_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }
        if (executable) {
            pt[pt_index] &= ~PAGING_FLAG_NX;
        } else {
            pt[pt_index] |= PAGING_FLAG_NX;
        }
        paging_invalidate_page(page);
        page += 0x1000ull;
    }
}

static void paging_set_range_write_flag(uint64_t start, uint64_t end, int writable) {
    uint64_t page = start & ~0xfffull;

    while (page < end) {
        uint64_t cr3;
        uint64_t *pml4;
        uint64_t *pdpt;
        uint64_t *pd;
        uint64_t *pt;
        uint64_t pml4_index = (page >> 39) & 0x1ffu;
        uint64_t pdpt_index = (page >> 30) & 0x1ffu;
        uint64_t pd_index = (page >> 21) & 0x1ffu;
        uint64_t pt_index = (page >> 12) & 0x1ffu;

        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        pml4 = paging_root_table(cr3);
        if ((pml4[pml4_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }

        pdpt = paging_table_from_entry(pml4[pml4_index]);
        if ((pdpt[pdpt_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }
        if (pdpt[pdpt_index] & PAGING_FLAG_PAGE_SIZE) {
            if (writable) {
                pdpt[pdpt_index] |= PAGING_FLAG_RW;
            } else {
                pdpt[pdpt_index] &= ~PAGING_FLAG_RW;
            }
            paging_invalidate_page(page);
            page += 0x1000ull;
            continue;
        }

        pd = paging_table_from_entry(pdpt[pdpt_index]);
        if ((pd[pd_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }
        if (pd[pd_index] & PAGING_FLAG_PAGE_SIZE) {
            if (writable) {
                pd[pd_index] |= PAGING_FLAG_RW;
            } else {
                pd[pd_index] &= ~PAGING_FLAG_RW;
            }
            paging_invalidate_page(page);
            page += 0x1000ull;
            continue;
        }

        pt = paging_table_from_entry(pd[pd_index]);
        if ((pt[pt_index] & PAGING_FLAG_PRESENT) == 0) {
            page += 0x1000ull;
            continue;
        }
        if (writable) {
            pt[pt_index] |= PAGING_FLAG_RW;
        } else {
            pt[pt_index] &= ~PAGING_FLAG_RW;
        }
        paging_invalidate_page(page);
        page += 0x1000ull;
    }
}

void paging_make_page_user_accessible(uint64_t addr) {
    uint64_t end;

    if (!paging_validate_page_address(addr) ||
        !paging_checked_add(addr, 0x1000ull, &end) ||
        !paging_is_canonical(end - 1u)) {
        return;
    }
    paging_set_range_user_flag(addr, end, 1);
}

void paging_make_range_user_accessible(uint64_t start, uint64_t end) {
    if (!paging_validate_page_range(start, end)) {
        return;
    }
    paging_set_range_user_flag(start, end, 1);
}

void paging_make_range_supervisor_only(uint64_t start, uint64_t end) {
    if (!paging_validate_page_range(start, end)) {
        return;
    }
    paging_set_range_user_flag(start, end, 0);
}

int paging_map_page(uint64_t virt_addr, uint64_t phys_addr, int user_accessible, int writable) {
    return paging_map_page_with_exec(virt_addr, phys_addr, user_accessible, writable, 0);
}

int paging_map_page_with_exec(uint64_t virt_addr,
                              uint64_t phys_addr,
                              int user_accessible,
                              int writable,
                              int executable) {
    uint64_t *pte;
    uint64_t entry_flags = PAGING_FLAG_PRESENT;

    if (paging_is_kernel_guard_page(virt_addr) ||
        !paging_validate_page_address(virt_addr) ||
        !paging_is_page_aligned(phys_addr)) {
        return 0;
    }

    pte = paging_walk_to_pte(virt_addr, 1);
    if (pte == 0) {
        return 0;
    }

    if ((*pte & PAGING_FLAG_PRESENT) != 0u) {
        return 0;
    }

    if (writable) {
        entry_flags |= PAGING_FLAG_RW;
    }

    if (user_accessible) {
        entry_flags |= PAGING_FLAG_USER;
    }

    if (g_paging_nx_enabled && !executable) {
        entry_flags |= PAGING_FLAG_NX;
    }

    *pte = (phys_addr & PAGING_ADDR_MASK) | entry_flags;

    paging_invalidate_page(virt_addr);

    return 1;
}

int paging_set_write_combining(uint64_t virt_addr, uint64_t size) {
    uint64_t start;
    uint64_t raw_end;
    uint64_t end;
    uint64_t page;
    int mapped = 1;

    if (size == 0 ||
        !paging_is_canonical(virt_addr) ||
        !paging_checked_add(virt_addr, size, &raw_end) ||
        raw_end == 0 ||
        !paging_is_canonical(raw_end - 1u) ||
        !paging_enable_pat_write_combining()) {
        return 0;
    }
    start = virt_addr & ~0xfffull;
    if (!paging_align_up_checked(raw_end, 0x1000ull, &end) ||
        end <= start ||
        !paging_validate_page_range(start, end)) {
        return 0;
    }

    __asm__ __volatile__("wbinvd" : : : "memory");
    for (page = start; page < end; page += 0x1000ull) {
        uint64_t *pte = paging_walk_to_pte(page, 0);

        if (pte == 0 || (*pte & PAGING_FLAG_PRESENT) == 0) {
            mapped = 0;
            continue;
        }
        *pte = (*pte & ~(PAGING_FLAG_PWT | PAGING_FLAG_PCD | PAGING_FLAG_PAT_4K)) |
               PAGING_FLAG_PAT_4K;
        paging_invalidate_page(page);
    }
    __asm__ __volatile__("wbinvd" : : : "memory");
    return mapped;
}

int paging_unmap_page(uint64_t virt_addr, uint64_t *phys_addr) {
    uint64_t cr3;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t *pml4e;
    uint64_t *pdpte;
    uint64_t *pde;
    uint64_t *pte;
    uint64_t pml4_index;
    uint64_t pdpt_index;
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t pt_phys;
    uint64_t pd_phys;
    uint64_t pdpt_phys;

    if (!paging_validate_page_address(virt_addr)) {
        return 0;
    }

    pml4_index = (virt_addr >> 39) & 0x1ffu;
    pdpt_index = (virt_addr >> 30) & 0x1ffu;
    pd_index = (virt_addr >> 21) & 0x1ffu;
    pt_index = (virt_addr >> 12) & 0x1ffu;

    cr3 = paging_get_current_cr3();
    if (cr3 == 0u) {
        return 0;
    }

    pml4 = paging_root_table(cr3);
    pml4e = &pml4[pml4_index];

    if ((*pml4e & PAGING_FLAG_PRESENT) == 0u) {
        return 0;
    }

    pdpt_phys = *pml4e & PAGING_ADDR_MASK;
    pdpt = paging_table_from_entry(*pml4e);
    pdpte = &pdpt[pdpt_index];

    if ((*pdpte & PAGING_FLAG_PRESENT) == 0u) {
        return 0;
    }

    /*
     * 1 GiB huge page는 이 함수의 4 KiB unmap 대상이 아니다.
     */
    if ((*pdpte & PAGING_FLAG_PAGE_SIZE) != 0u) {
        return 0;
    }

    pd_phys = *pdpte & PAGING_ADDR_MASK;
    pd = paging_table_from_entry(*pdpte);
    pde = &pd[pd_index];

    if ((*pde & PAGING_FLAG_PRESENT) == 0u) {
        return 0;
    }

    /*
     * 2 MiB huge page 역시 별도 API가 필요하다.
     */
    if ((*pde & PAGING_FLAG_PAGE_SIZE) != 0u) {
        return 0;
    }

    pt_phys = *pde & PAGING_ADDR_MASK;
    pt = paging_table_from_entry(*pde);
    pte = &pt[pt_index];

    if ((*pte & PAGING_FLAG_PRESENT) == 0u) {
        return 0;
    }

    if (phys_addr != 0) {
        *phys_addr = *pte & PAGING_ADDR_MASK;
    }

    /*
     * 실제 physical frame ownership은 여기서 건드리지 않는다.
     * 이 함수는 mapping 제거 + page-table reclamation만 담당한다.
     */
    *pte = 0u;

    paging_invalidate_page(virt_addr);

    /*
     * PT가 비었으면 PT page 회수.
     */
    if (!paging_table_is_empty(pt)) {
        return 1;
    }

    *pde = 0u;

    if (!pmm_free_page(pt_phys)) {
        /*
         * PMM free 실패 시 이미 PDE를 없앴으므로 상태가 애매해진다.
         * 먼저 free 가능성을 보장하는 정책을 쓰는 게 가장 좋지만,
         * 정상적인 PMM 상태에서는 실패하면 안 된다.
         */
        return 1;
    }

    /*
     * PD가 아직 다른 PT/huge mapping을 가지고 있으면 여기서 끝.
     */
    if (!paging_table_is_empty(pd)) {
        return 1;
    }

    *pdpte = 0u;

    if (!pmm_free_page(pd_phys)) {
        return 1;
    }

    /*
     * PDPT도 완전히 비었으면 회수.
     */
    if (!paging_table_is_empty(pdpt)) {
        return 1;
    }

    *pml4e = 0u;

    (void)pmm_free_page(pdpt_phys);

    /*
     * PML4 자체는 CR3 root이므로 절대 여기서 free하지 않는다.
     */
    return 1;
}

int paging_get_mapping(uint64_t virt_addr, uint64_t *phys_addr) {
    struct paging_walk_info info;

    if (!paging_walk_in_root(paging_get_current_cr3(), virt_addr, &info)) {
        return 0;
    }
    if (phys_addr != 0) {
        *phys_addr = info.phys_addr;
    }
    return 1;
}

int paging_get_mapping_info(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags) {
    struct paging_walk_info info;

    if (!paging_walk_in_root(paging_get_current_cr3(), virt_addr, &info)) {
        return 0;
    }
    if (phys_addr != 0) {
        *phys_addr = info.phys_addr;
    }
    if (flags != 0) {
        *flags = info.flags;
    }
    return 1;
}

int paging_get_mapping_info_in_root(uint64_t root_cr3,
                                    uint64_t virt_addr,
                                    uint64_t *phys_addr,
                                    uint64_t *flags) {
    struct paging_walk_info info;

    if (!paging_walk_in_root(root_cr3, virt_addr, &info)) {
        return 0;
    }
    if (phys_addr != 0) {
        *phys_addr = info.phys_addr;
    }
    if (flags != 0) {
        *flags = info.flags;
    }
    return 1;
}

int paging_get_walk_entries(uint64_t virt_addr,
                            uint64_t *pml4e,
                            uint64_t *pdpte,
                            uint64_t *pde,
                            uint64_t *pte_out) {
    struct paging_walk_info info;

    if (!paging_walk_in_root(paging_get_current_cr3(), virt_addr, &info)) {
        return 0;
    }
    if (pml4e != 0) {
        *pml4e = info.pml4e;
    }
    if (pdpte != 0) {
        *pdpte = info.pdpte;
    }
    if (pde != 0) {
        *pde = info.pde;
    }
    if (pte_out != 0) {
        *pte_out = info.pte;
    }
    return 1;
}

int paging_get_walk_entries_in_root(uint64_t root_cr3,
                                    uint64_t virt_addr,
                                    uint64_t *pml4e,
                                    uint64_t *pdpte,
                                    uint64_t *pde,
                                    uint64_t *pte_out) {
    struct paging_walk_info info;

    if (!paging_walk_in_root(root_cr3, virt_addr, &info)) {
        return 0;
    }
    if (pml4e != 0) {
        *pml4e = info.pml4e;
    }
    if (pdpte != 0) {
        *pdpte = info.pdpte;
    }
    if (pde != 0) {
        *pde = info.pde;
    }
    if (pte_out != 0) {
        *pte_out = info.pte;
    }
    return 1;
}

int paging_get_walk_info_in_root(uint64_t root_cr3, uint64_t virt_addr, struct paging_walk_info *info_out) {
    return paging_walk_in_root(root_cr3, virt_addr, info_out);
}

void paging_get_last_switch_trace(struct paging_switch_trace *trace_out) {
    if (trace_out == 0) {
        return;
    }
    *trace_out = g_last_switch_trace;
}

void paging_get_last_clone_trace(struct paging_clone_trace *trace_out) {
    if (trace_out == 0) {
        return;
    }
    *trace_out = g_last_clone_trace;
}

int paging_cpu_supports_nx(void) {
    return g_paging_nx_supported != 0;
}

int paging_nx_enabled(void) {
    return g_paging_nx_enabled != 0;
}
