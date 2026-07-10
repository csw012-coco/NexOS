#include "bootx.h"
#include "paging.h"
#include "pmm.h"

enum {
    PMM_MAX_PAGES = 1024u * 1024u,
    PMM_BITMAP_BYTES = PMM_MAX_PAGES / 8u,
    PMM_MANAGED_BASE = I386_PAGING_IDENTITY_LIMIT
};

static unsigned char frame_bitmap[PMM_BITMAP_BYTES];
static uint32_t managed_page_count;
static uint32_t free_page_count;
static uint32_t next_free_page;
static uint32_t highest_address;

static void bitmap_mark_used(uint32_t page) {
    frame_bitmap[page >> 3] |= (unsigned char)(1u << (page & 7u));
}

static void bitmap_mark_free(uint32_t page) {
    frame_bitmap[page >> 3] &= (unsigned char)~(1u << (page & 7u));
}

static int bitmap_is_used(uint32_t page) {
    return (frame_bitmap[page >> 3] & (unsigned char)(1u << (page & 7u))) != 0;
}

static uint32_t align_up_page(uint32_t value) {
    return (value + I386_PMM_PAGE_SIZE - 1u) & ~(I386_PMM_PAGE_SIZE - 1u);
}

static uint32_t align_down_page(uint32_t value) {
    return value & ~(I386_PMM_PAGE_SIZE - 1u);
}

static void mark_usable_range(uint32_t base, uint32_t end) {
    uint32_t first_page;
    uint32_t end_page;

    if (base < PMM_MANAGED_BASE) {
        base = PMM_MANAGED_BASE;
    }
    base = align_up_page(base);
    end = align_down_page(end);
    if (end <= base) {
        return;
    }

    first_page = base / I386_PMM_PAGE_SIZE;
    end_page = end / I386_PMM_PAGE_SIZE;
    if (end_page > PMM_MAX_PAGES) {
        end_page = PMM_MAX_PAGES;
    }

    for (uint32_t page = first_page; page < end_page; page++) {
        if (bitmap_is_used(page)) {
            bitmap_mark_free(page);
            free_page_count++;
        }
    }
}

int i386_pmm_init(const struct bootx_boot_info *boot_info) {
    const struct bootx_memmap_entry *memmap;

    if (boot_info == 0 || boot_info->memmap == 0 || boot_info->memmap_count == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < PMM_BITMAP_BYTES; i++) {
        frame_bitmap[i] = 0xffu;
    }

    managed_page_count = 0;
    free_page_count = 0;
    highest_address = 0;
    memmap = (const struct bootx_memmap_entry *)boot_info->memmap;

    for (uint32_t i = 0; i < boot_info->memmap_count; i++) {
        unsigned long long base = memmap[i].base;
        unsigned long long length = memmap[i].length;
        unsigned long long end;
        uint32_t end32;

        if (memmap[i].type != BOOTX_MEMMAP_USABLE || length == 0 ||
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
        mark_usable_range((uint32_t)base, end32);
    }

    managed_page_count = highest_address / I386_PMM_PAGE_SIZE;
    if (managed_page_count > PMM_MAX_PAGES) {
        managed_page_count = PMM_MAX_PAGES;
    }
    next_free_page = PMM_MANAGED_BASE / I386_PMM_PAGE_SIZE;
    return free_page_count != 0;
}

uint32_t i386_pmm_alloc_page(void) {
    if (free_page_count == 0 || managed_page_count == 0) {
        return I386_PMM_INVALID_PAGE;
    }

    for (uint32_t page = next_free_page; page < managed_page_count; page++) {
        if (!bitmap_is_used(page)) {
            bitmap_mark_used(page);
            free_page_count--;
            next_free_page = page + 1u;
            return page * I386_PMM_PAGE_SIZE;
        }
    }

    for (uint32_t page = PMM_MANAGED_BASE / I386_PMM_PAGE_SIZE;
         page < next_free_page && page < managed_page_count;
         page++) {
        if (!bitmap_is_used(page)) {
            bitmap_mark_used(page);
            free_page_count--;
            next_free_page = page + 1u;
            return page * I386_PMM_PAGE_SIZE;
        }
    }

    return I386_PMM_INVALID_PAGE;
}

int i386_pmm_free_page(uint32_t physical_address) {
    uint32_t page;

    if ((physical_address & (I386_PMM_PAGE_SIZE - 1u)) != 0 ||
        physical_address < PMM_MANAGED_BASE) {
        return 0;
    }

    page = physical_address / I386_PMM_PAGE_SIZE;
    if (page >= managed_page_count || !bitmap_is_used(page)) {
        return 0;
    }

    bitmap_mark_free(page);
    free_page_count++;
    if (page < next_free_page) {
        next_free_page = page;
    }
    return 1;
}

uint32_t i386_pmm_total_pages(void) {
    return managed_page_count;
}

uint32_t i386_pmm_free_pages(void) {
    return free_page_count;
}

uint32_t i386_pmm_reserved_pages(void) {
    return managed_page_count - free_page_count;
}

uint32_t pmm_total_pages(void) {
    return i386_pmm_total_pages();
}

uint32_t pmm_free_pages(void) {
    return i386_pmm_free_pages();
}

uint32_t pmm_used_pages(void) {
    return i386_pmm_reserved_pages();
}

uint32_t pmm_dropped_pages(void) {
    return 0u;
}

uint32_t i386_pmm_highest_address(void) {
    return highest_address;
}
