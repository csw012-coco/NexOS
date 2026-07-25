#include "bootx.h"
#include "paging.h"
#include "pmm.h"

#include "kernel/public/mem/pmm.h"

static uint32_t highest_address;

static uint32_t i386_pmm_phys32_or_invalid(uint64_t phys) {
    return phys != 0u && phys <= 0xffffffffull
        ? (uint32_t)phys
        : I386_PMM_INVALID_PAGE;
}

static void i386_pmm_scan_highest_address(
    const struct bootx_memmap_entry *memmap,
    uint32_t memmap_count) {
    highest_address = 0u;
    for (uint32_t i = 0u; i < memmap_count; i++) {
        uint64_t base = memmap[i].base;
        uint64_t length = memmap[i].length;
        uint64_t end;
        uint32_t end32;

        if (memmap[i].type != BOOTX_MEMMAP_USABLE ||
            length == 0u ||
            base >= 0x100000000ull) {
            continue;
        }
        end = base + length;
        if (end < base || end > 0x100000000ull) {
            end = 0x100000000ull;
        }
        end32 = end == 0x100000000ull ? 0xffffffffu : (uint32_t)end;
        if (end32 > highest_address) {
            highest_address = end32;
        }
    }
}

int i386_pmm_init(const struct bootx_boot_info *boot_info) {
    const struct bootx_memmap_entry *memmap;

    if (boot_info == 0 ||
        boot_info->memmap == 0 ||
        boot_info->memmap_count == 0u) {
        return 0;
    }
    memmap = (const struct bootx_memmap_entry *)boot_info->memmap;
    i386_pmm_scan_highest_address(memmap, boot_info->memmap_count);
    pmm_init(memmap, boot_info->memmap_count, 0u, 0u);
    pmm_reserve_range(0u, I386_PAGING_IDENTITY_LIMIT);
    return pmm_free_pages() != 0u;
}

uint32_t i386_pmm_alloc_page(void) {
    return i386_pmm_phys32_or_invalid(pmm_alloc_page());
}

uint32_t i386_pmm_alloc_page_below(uint32_t max_phys_exclusive) {
    return i386_pmm_phys32_or_invalid(
        pmm_alloc_page_below(max_phys_exclusive));
}

uint32_t i386_pmm_alloc_contiguous_below(uint32_t page_count,
                                         uint32_t max_phys_exclusive) {
    return i386_pmm_phys32_or_invalid(
        pmm_alloc_contiguous_below(page_count, max_phys_exclusive));
}

int i386_pmm_retain_page(uint32_t physical_address) {
    return pmm_retain_page(physical_address);
}

int i386_pmm_free_page(uint32_t physical_address) {
    return pmm_free_page(physical_address);
}

uint32_t i386_pmm_refcount(uint32_t physical_address) {
    return pmm_ref_count(physical_address);
}

uint32_t i386_pmm_total_pages(void) {
    return pmm_total_pages();
}

uint32_t i386_pmm_free_pages(void) {
    return pmm_free_pages();
}

uint32_t i386_pmm_reserved_pages(void) {
    return pmm_used_pages();
}

uint32_t i386_pmm_highest_address(void) {
    return highest_address;
}
