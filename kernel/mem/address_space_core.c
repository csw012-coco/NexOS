#include "kernel/internal/mem/address_space_internal.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/mem/pmm.h"

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static struct user_page_mapping *addrspace_find_mapping(uint64_t virt_addr) {
    uint64_t page = align_down(virt_addr, USER_PAGE_SIZE);

    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        if (g_bound_mappings[i].used && g_bound_mappings[i].virt_addr == page) {
            return &g_bound_mappings[i];
        }
    }
    return 0;
}

static struct user_page_mapping *addrspace_alloc_mapping_slot(void) {
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        if (!g_bound_mappings[i].used) {
            return &g_bound_mappings[i];
        }
    }
    return 0;
}

static void addrspace_clear_mapping(struct user_page_mapping *mapping) {
    if (mapping == 0) {
        return;
    }
    mapping->used = 0;
    mapping->virt_addr = 0;
    mapping->phys_addr = 0;
    mapping->reserved_pool = 0;
}

static int addrspace_try_alloc_phys_page(uint64_t *phys_addr_out, int *reserved_pool_out) {
    if (phys_addr_out == 0 || reserved_pool_out == 0) {
        return 0;
    }

    if (g_bound_session->address_space.reserved_phys_next != 0 &&
        g_bound_session->address_space.reserved_phys_next + USER_PAGE_SIZE <=
            g_bound_session->address_space.reserved_phys_limit) {
        *phys_addr_out = g_bound_session->address_space.reserved_phys_next;
        g_bound_session->address_space.reserved_phys_next += USER_PAGE_SIZE;
        *reserved_pool_out = 1;
        return 1;
    }

    *phys_addr_out = pmm_alloc_page();
    *reserved_pool_out = 0;
    return *phys_addr_out != 0;
}

static void addrspace_release_mapping(struct user_page_mapping *mapping) {
    uint64_t phys_addr;

    if (mapping == 0 || !mapping->used) {
        return;
    }
    if (vmm_unmap(mapping->virt_addr, &phys_addr) && !mapping->reserved_pool) {
        pmm_free_page(phys_addr);
    }
    addrspace_clear_mapping(mapping);
}

static int addrspace_map_page_at(uint64_t virt_addr, uint32_t perms) {
    struct user_page_mapping *mapping;
    uint64_t phys_addr;
    uint64_t page = align_down(virt_addr, USER_PAGE_SIZE);
    int reserved_pool = 0;

    if (addrspace_find_mapping(page) != 0) {
        return 1;
    }

    mapping = addrspace_alloc_mapping_slot();
    if (mapping == 0) {
        return 0;
    }

    if (!addrspace_try_alloc_phys_page(&phys_addr, &reserved_pool)) {
        return 0;
    }
    if (!vmm_map(page,
                 phys_addr,
                 VMM_PERM_USER | perms)) {
        if (!reserved_pool) {
            pmm_free_page(phys_addr);
        }
        return 0;
    }
    vmm_allow_user_page(page);

    mapping->virt_addr = page;
    mapping->phys_addr = phys_addr;
    mapping->used = 1;
    mapping->reserved_pool = reserved_pool ? 1u : 0u;
    (void)vmm_zero_range(page, USER_PAGE_SIZE);
    return 1;
}

int addrspace_map_range(uint64_t start, uint64_t end) {
    uint64_t page = align_down(start, USER_PAGE_SIZE);
    uint64_t page_end = align_up(end, USER_PAGE_SIZE);

    while (page < page_end) {
        if (!addrspace_map_page_at(page, VMM_PERM_WRITE)) {
            return 0;
        }
        page += USER_PAGE_SIZE;
    }
    return 1;
}

int addrspace_map_range_with_perms(uint64_t start, uint64_t end, uint32_t perms) {
    uint64_t page = align_down(start, USER_PAGE_SIZE);
    uint64_t page_end = align_up(end, USER_PAGE_SIZE);

    while (page < page_end) {
        if (!addrspace_map_page_at(page, perms)) {
            return 0;
        }
        page += USER_PAGE_SIZE;
    }
    return 1;
}

void addrspace_unmap_range_if_present(uint64_t start, uint64_t end) {
    vmm_unmap_range_if_present(start, end);
}

int addrspace_zero_range(uint64_t start, uint64_t size) {
    return vmm_zero_range(start, size);
}

int addrspace_copy_to_range(uint64_t dest, const uint8_t *src, uint64_t size) {
    return vmm_copy_to_range(dest, src, size);
}

void addrspace_release_dynamic_pages(void) {
    for (uint32_t i = 0; i < USER_DYNAMIC_PAGE_LIMIT; i++) {
        addrspace_release_mapping(&g_bound_mappings[i]);
    }
    g_next_user_alloc = USER_ALLOC_BASE;
    g_bound_session->address_space.reserved_phys_next = g_bound_session->address_space.reserved_phys_base;
}

uint64_t addrspace_alloc_page(void) {
    uint64_t addr = g_next_user_alloc;

    while (addr < USER_ALLOC_END) {
        if (addrspace_find_mapping(addr) == 0) {
            if (!addrspace_map_page_at(addr, VMM_PERM_WRITE)) {
                return 0;
            }
            g_next_user_alloc = addr + USER_PAGE_SIZE;
            return addr;
        }
        addr += USER_PAGE_SIZE;
    }
    return 0;
}

int addrspace_free_page(uint64_t virt_addr) {
    struct user_page_mapping *mapping = addrspace_find_mapping(virt_addr);

    if (mapping == 0) {
        return 0;
    }
    addrspace_release_mapping(mapping);
    return 1;
}
