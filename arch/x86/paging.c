#include "arch/x86/paging.h"
#include "kernel/public/mem/pmm.h"

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
    PAGING_FLAG_PAGE_SIZE = 1ull << 7,
    PAGING_FLAG_NX = 1ull << 63,
    PAGING_ADDR_MASK = 0x000ffffffffff000ull,
    PAGING_TABLE_ENTRIES = 512,
    PAGING_KERNEL_RANGE_LIMIT = 64,
    PAGING_MSR_EFER = 0xc0000080u,
    PAGING_EFER_NXE = 1ull << 11,
    PAGING_CPUID_EXT_FEATURES = 0x80000001u,
    PAGING_CPUID_NX_BIT = 1u << 20
};

struct paging_kernel_range {
    uint64_t phys_start;
    uint64_t phys_end;
    uint64_t virt_start;
};

static uint64_t g_kernel_phys_base;
static struct paging_switch_trace g_last_switch_trace;
static struct paging_clone_trace g_last_clone_trace;
static struct paging_kernel_range g_kernel_ranges[PAGING_KERNEL_RANGE_LIMIT];
static uint32_t g_kernel_range_count;
static uint8_t g_paging_nx_supported;
static uint8_t g_paging_nx_enabled;
static uint64_t *paging_table_from_entry(uint64_t entry);
static int paging_translate_in_root(uint64_t cr3, uint64_t virt_addr, uint64_t *phys_out);
static int paging_translate_current(uint64_t virt_addr, uint64_t *phys_out);
static uint64_t *paging_root_table(uint64_t cr3);
static uint64_t *paging_alloc_table(uint64_t *phys_out);
static uint64_t paging_clone_table_deep(uint64_t table_phys, uint32_t level);
static void paging_destroy_table_deep(uint64_t table_phys, uint32_t level);
static int paging_walk_in_root(uint64_t root_cr3, uint64_t virt_addr, struct paging_walk_info *info_out);
static uint64_t *paging_walk_to_pte_in_root(uint64_t root_cr3, uint64_t addr, int create, int user_accessible);
static int paging_map_page_in_root(uint64_t root_cr3,
                                   uint64_t virt_addr,
                                   uint64_t phys_addr,
                                   int user_accessible,
                                   int writable,
                                   int executable);
static void paging_build_kernel_ranges(void);
static void paging_set_range_write_flag(uint64_t start, uint64_t end, int writable);
static void paging_set_range_execute_flag(uint64_t start, uint64_t end, int executable);

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

static void paging_destroy_table_deep(uint64_t table_phys, uint32_t level) {
    uint64_t *table;

    if (table_phys == 0 || level == 0) {
        return;
    }

    table = (uint64_t *)paging_phys_direct_map(table_phys & PAGING_ADDR_MASK);
    if (level > 1) {
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            uint64_t entry = table[i];

            if ((entry & PAGING_FLAG_PRESENT) == 0 || (entry & PAGING_FLAG_PAGE_SIZE) != 0) {
                continue;
            }
            paging_destroy_table_deep(entry & PAGING_ADDR_MASK, level - 1);
        }
    }
    (void)pmm_free_page(table_phys & PAGING_ADDR_MASK);
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

static uint64_t *paging_walk_to_pte_in_root(uint64_t root_cr3, uint64_t addr, int create, int user_accessible) {
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t entry_flags = PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
    uint64_t pml4_index = (addr >> 39) & 0x1ffu;
    uint64_t pdpt_index = (addr >> 30) & 0x1ffu;
    uint64_t pd_index = (addr >> 21) & 0x1ffu;
    uint64_t pt_index = (addr >> 12) & 0x1ffu;

    if (user_accessible) {
        entry_flags |= PAGING_FLAG_USER;
    }
    if (root_cr3 == 0) {
        return 0;
    }

    pml4 = paging_root_table(root_cr3);
    if ((pml4[pml4_index] & PAGING_FLAG_PRESENT) == 0) {
        uint64_t new_pdpt_phys = 0;
        uint64_t *new_pdpt;

        if (!create) {
            return 0;
        }
        new_pdpt = paging_alloc_table(&new_pdpt_phys);
        if (new_pdpt == 0) {
            return 0;
        }
        pml4[pml4_index] = new_pdpt_phys | entry_flags;
    }

    pdpt = paging_table_from_entry(pml4[pml4_index]);
    if ((pdpt[pdpt_index] & PAGING_FLAG_PRESENT) == 0) {
        uint64_t new_pd_phys = 0;
        uint64_t *new_pd;

        if (!create) {
            return 0;
        }
        new_pd = paging_alloc_table(&new_pd_phys);
        if (new_pd == 0) {
            return 0;
        }
        pdpt[pdpt_index] = new_pd_phys | entry_flags;
    }
    if (pdpt[pdpt_index] & PAGING_FLAG_PAGE_SIZE) {
        return 0;
    }

    pd = paging_table_from_entry(pdpt[pdpt_index]);
    if ((pd[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        uint64_t new_pt_phys = 0;
        uint64_t *new_pt;

        if (!create) {
            return 0;
        }
        new_pt = paging_alloc_table(&new_pt_phys);
        if (new_pt == 0) {
            return 0;
        }
        pd[pd_index] = new_pt_phys | entry_flags;
    }
    if (pd[pd_index] & PAGING_FLAG_PAGE_SIZE) {
        return 0;
    }

    pt = paging_table_from_entry(pd[pd_index]);
    return &pt[pt_index];
}

static int paging_map_page_in_root(uint64_t root_cr3,
                                   uint64_t virt_addr,
                                   uint64_t phys_addr,
                                   int user_accessible,
                                   int writable,
                                   int executable) {
    uint64_t *pte = paging_walk_to_pte_in_root(root_cr3, virt_addr, 1, user_accessible);
    uint64_t entry_flags = PAGING_FLAG_PRESENT;

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
            return 0;
        }
        pdpt[table] = pd_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            uint64_t base = ((uint64_t)table * PAGING_TABLE_ENTRIES + (uint64_t)i) * 0x200000ull;
            pd[i] = base | PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_PAGE_SIZE;
        }
    }

    for (uint64_t virt = kernel_start; virt < kernel_end; virt += 0x1000ull) {
        uint64_t phys = g_kernel_phys_base + (virt - kernel_start);

        if (!paging_map_page_in_root(root_phys, virt, phys, 0, 1, 1)) {
            return 0;
        }
    }

    return root_phys;
}

uint64_t paging_clone_root_deep(uint64_t source_cr3) {
    return paging_clone_table_deep(source_cr3 & PAGING_ADDR_MASK, 4);
}

uint64_t paging_clone_current_root_deep(void) {
    return paging_clone_root_deep(paging_get_current_cr3());
}

uint64_t paging_create_user_root(void) {
    uint64_t source_cr3 = paging_get_current_cr3();
    uint64_t dst_pml4_phys = paging_clone_root_deep(source_cr3);
    uint64_t *dst_pml4;

    if (dst_pml4_phys == 0) {
        return 0;
    }

    dst_pml4 = (uint64_t *)paging_phys_direct_map(dst_pml4_phys);
    g_last_clone_trace.source_cr3 = source_cr3;
    g_last_clone_trace.clone_cr3 = dst_pml4_phys;
    g_last_clone_trace.source_pml4e0 = 0;
    g_last_clone_trace.source_pml4e511 = 0;
    {
        uint64_t *src_pml4 = paging_root_table(source_cr3);

        g_last_clone_trace.source_pml4e0 = src_pml4[0];
        g_last_clone_trace.source_pml4e511 = src_pml4[511];
    }
    g_last_clone_trace.fail_virt = 0;
    g_last_clone_trace.fail_phys = 0;
    g_last_clone_trace.fail_stage = 0;

    g_last_clone_trace.clone_pml4e0 = dst_pml4[0];
    g_last_clone_trace.clone_pml4e511 = dst_pml4[511];
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
    uint64_t cr3;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t *new_pd;
    uint64_t pml4_index = (addr >> 39) & 0x1ffu;
    uint64_t pdpt_index = (addr >> 30) & 0x1ffu;
    uint64_t pd_index = (addr >> 21) & 0x1ffu;
    uint64_t pdpt_base;
    uint64_t pd_base;
    uint64_t pt_base;
    uint64_t flags;

    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    pml4 = paging_root_table(cr3);
    if ((pml4[pml4_index] & PAGING_FLAG_PRESENT) == 0) {
        uint64_t *new_pdpt;
        uint64_t new_pdpt_phys;

        if (!create) {
            return 0;
        }
        new_pdpt = paging_alloc_table(&new_pdpt_phys);
        if (new_pdpt == 0) {
            return 0;
        }
        pml4[pml4_index] = new_pdpt_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER;
        paging_flush_tlb();
    } else if (create) {
        pml4[pml4_index] |= PAGING_FLAG_USER;
    }

    pdpt = paging_table_from_entry(pml4[pml4_index]);
    if ((pdpt[pdpt_index] & PAGING_FLAG_PRESENT) == 0) {
        uint64_t new_pd_phys;

        if (!create) {
            return 0;
        }
        new_pd = paging_alloc_table(&new_pd_phys);
        if (new_pd == 0) {
            return 0;
        }
        pdpt[pdpt_index] = new_pd_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER;
        paging_flush_tlb();
    } else if (create) {
        pdpt[pdpt_index] |= PAGING_FLAG_USER;
    }

    if (pdpt[pdpt_index] & PAGING_FLAG_PAGE_SIZE) {
        uint64_t new_pd_phys;

        if (!create) {
            return 0;
        }

        new_pd = paging_alloc_table(&new_pd_phys);
        if (new_pd == 0) {
            return 0;
        }

        pdpt_base = pdpt[pdpt_index] & 0x000fffffc0000000ull;
        flags = (pdpt[pdpt_index] & ~0x000fffffc0000000ull) & ~PAGING_FLAG_PAGE_SIZE;
        flags |= PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            new_pd[i] = (pdpt_base + (uint64_t)i * 0x200000ull) | flags | PAGING_FLAG_PAGE_SIZE;
        }
        pdpt[pdpt_index] = new_pd_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER;
        paging_flush_tlb();
    }

    pd = paging_table_from_entry(pdpt[pdpt_index]);
    if ((pd[pd_index] & PAGING_FLAG_PRESENT) == 0) {
        uint64_t *new_pt;
        uint64_t new_pt_phys;

        if (!create) {
            return 0;
        }
        new_pt = paging_alloc_table(&new_pt_phys);
        if (new_pt == 0) {
            return 0;
        }
        pd[pd_index] = new_pt_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER;
        paging_flush_tlb();
    } else if (pd[pd_index] & PAGING_FLAG_PAGE_SIZE) {
        uint64_t new_pt_phys;
        uint64_t *new_pt = paging_alloc_table(&new_pt_phys);

        if (new_pt == 0) {
            return 0;
        }

        pd_base = pd[pd_index] & 0x000fffffffe00000ull;
        flags = (pd[pd_index] & ~0x000fffffffe00000ull) & ~PAGING_FLAG_PAGE_SIZE;
        flags |= PAGING_FLAG_PRESENT | PAGING_FLAG_RW;
        for (uint32_t i = 0; i < PAGING_TABLE_ENTRIES; i++) {
            new_pt[i] = (pd_base + (uint64_t)i * 0x1000ull) | flags;
        }
        pd[pd_index] = new_pt_phys | PAGING_FLAG_PRESENT | PAGING_FLAG_RW | PAGING_FLAG_USER;
        paging_flush_tlb();
    } else if (create) {
        pd[pd_index] |= PAGING_FLAG_USER;
    }

    pt = paging_table_from_entry(pd[pd_index]);
    pt_base = (addr >> 12) & 0x1ffu;
    return &pt[pt_base];
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
        __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)page) : "memory");
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
            __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)page) : "memory");
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
            __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)page) : "memory");
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
        __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)page) : "memory");
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
            __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)page) : "memory");
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
            __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)page) : "memory");
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
        __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uintptr_t)page) : "memory");
        page += 0x1000ull;
    }
}

void paging_make_page_user_accessible(uint64_t addr) {
    paging_set_range_user_flag(addr, addr + 0x1000ull, 1);
}

void paging_make_range_user_accessible(uint64_t start, uint64_t end) {
    paging_set_range_user_flag(start, end, 1);
}

void paging_make_range_supervisor_only(uint64_t start, uint64_t end) {
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
    uint64_t *pte = paging_walk_to_pte(virt_addr, 1);
    uint64_t entry_flags = PAGING_FLAG_PRESENT;

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
    paging_flush_tlb();
    return 1;
}

int paging_unmap_page(uint64_t virt_addr, uint64_t *phys_addr) {
    uint64_t *pte = paging_walk_to_pte(virt_addr, 0);

    if (pte == 0 || (*pte & PAGING_FLAG_PRESENT) == 0) {
        return 0;
    }
    if (phys_addr != 0) {
        *phys_addr = *pte & PAGING_ADDR_MASK;
    }
    *pte = 0;
    paging_flush_tlb();
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
