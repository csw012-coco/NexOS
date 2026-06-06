#include "kernel/public/mem/pmm.h"

enum {
    PMM_PAGE_SIZE = 4096,
    PMM_MAX_TRACKED_PAGES = 131072
};

static uint64_t free_page_stack[PMM_MAX_TRACKED_PAGES];
static uint32_t free_page_count;
static uint32_t tracked_page_count;
static uint32_t dropped_page_count;

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static int range_overlaps(uint64_t base_a, uint64_t end_a, uint64_t base_b, uint64_t end_b) {
    return base_a < end_b && base_b < end_a;
}

void pmm_init(const struct bootx_memmap_entry *memmap,
              uint32_t memmap_count,
              uint64_t kernel_phys_addr,
              uint64_t kernel_phys_size) {
    uint64_t kernel_base = align_down(kernel_phys_addr, PMM_PAGE_SIZE);
    uint64_t kernel_end = align_up(kernel_phys_addr + kernel_phys_size, PMM_PAGE_SIZE);

    free_page_count = 0;
    tracked_page_count = 0;
    dropped_page_count = 0;

    for (uint32_t i = 0; i < memmap_count; i++) {
        uint64_t region_base;
        uint64_t region_end;

        if (memmap[i].type != BOOTX_MEMMAP_USABLE) {
            continue;
        }

        region_base = align_up(memmap[i].base, PMM_PAGE_SIZE);
        region_end = align_down(memmap[i].base + memmap[i].length, PMM_PAGE_SIZE);
        if (region_end <= region_base) {
            continue;
        }

        if (region_base < 0x100000ull) {
            region_base = 0x100000ull;
        }

        for (uint64_t page = region_end; page > region_base; page -= PMM_PAGE_SIZE) {
            uint64_t page_base = page - PMM_PAGE_SIZE;

            if (range_overlaps(page_base, page_base + PMM_PAGE_SIZE, kernel_base, kernel_end)) {
                continue;
            }
            if (tracked_page_count >= PMM_MAX_TRACKED_PAGES) {
                dropped_page_count++;
                continue;
            }
            free_page_stack[free_page_count++] = page_base;
            tracked_page_count++;
        }
    }
}

uint64_t pmm_alloc_page(void) {
    if (free_page_count == 0) {
        return 0;
    }
    return free_page_stack[--free_page_count];
}

uint64_t pmm_alloc_page_below(uint64_t max_phys_exclusive) {
    uint32_t index;
    uint64_t phys;

    if (max_phys_exclusive <= PMM_PAGE_SIZE || free_page_count == 0) {
        return 0;
    }

    index = free_page_count;
    while (index > 0u) {
        index--;
        phys = free_page_stack[index];
        if (phys + PMM_PAGE_SIZE <= max_phys_exclusive) {
            for (uint32_t move = index + 1u; move < free_page_count; move++) {
                free_page_stack[move - 1u] = free_page_stack[move];
            }
            free_page_count--;
            return phys;
        }
    }

    return 0;
}

uint64_t pmm_alloc_contiguous(uint32_t page_count) {
    uint32_t run;
    uint32_t start_index;
    uint32_t write_index;
    uint64_t base;

    if (page_count == 0u || free_page_count < page_count) {
        return 0;
    }

    run = 1u;
    start_index = free_page_count - 1u;
    while (start_index > 0u) {
        uint64_t current = free_page_stack[start_index];
        uint64_t previous = free_page_stack[start_index - 1u];

        if (previous == current + PMM_PAGE_SIZE) {
            run++;
            if (run == page_count) {
                uint32_t alloc_start = start_index - 1u;
                uint32_t alloc_end = alloc_start + page_count - 1u;

                base = free_page_stack[alloc_end];
                write_index = alloc_start;
                for (uint32_t read_index = alloc_start + page_count; read_index < free_page_count; read_index++) {
                    free_page_stack[write_index++] = free_page_stack[read_index];
                }
                free_page_count -= page_count;
                return base;
            }
        } else {
            run = 1u;
        }
        start_index--;
    }

    return 0;
}

uint64_t pmm_alloc_contiguous_below(uint32_t page_count, uint64_t max_phys_exclusive) {
    uint32_t run;
    uint32_t start_index;
    uint32_t write_index;
    uint64_t base;

    if (page_count == 0u || free_page_count < page_count) {
        return 0;
    }
    if (page_count == 1u) {
        return pmm_alloc_page_below(max_phys_exclusive);
    }
    if (max_phys_exclusive <= (uint64_t)page_count * PMM_PAGE_SIZE) {
        return 0;
    }

    run = 1u;
    start_index = free_page_count - 1u;
    while (start_index > 0u) {
        uint64_t current = free_page_stack[start_index];
        uint64_t previous = free_page_stack[start_index - 1u];
        int current_ok = current + PMM_PAGE_SIZE <= max_phys_exclusive;
        int previous_ok = previous + PMM_PAGE_SIZE <= max_phys_exclusive;

        if (current_ok && previous_ok && previous == current + PMM_PAGE_SIZE) {
            run++;
            if (run == page_count) {
                uint32_t alloc_start = start_index - 1u;
                uint32_t alloc_end = alloc_start + page_count - 1u;

                base = free_page_stack[alloc_end];
                write_index = alloc_start;
                for (uint32_t read_index = alloc_start + page_count; read_index < free_page_count; read_index++) {
                    free_page_stack[write_index++] = free_page_stack[read_index];
                }
                free_page_count -= page_count;
                return base;
            }
        } else {
            run = 1u;
        }
        start_index--;
    }

    return 0;
}

int pmm_free_page(uint64_t phys_addr) {
    if ((phys_addr & (PMM_PAGE_SIZE - 1u)) != 0) {
        return 0;
    }
    if (free_page_count >= PMM_MAX_TRACKED_PAGES) {
        return 0;
    }
    free_page_stack[free_page_count++] = phys_addr;
    return 1;
}

void pmm_reserve_range(uint64_t base, uint64_t size) {
    uint64_t reserve_base;
    uint64_t reserve_end;
    uint32_t write_index = 0;

    if (size == 0) {
        return;
    }

    reserve_base = align_down(base, PMM_PAGE_SIZE);
    reserve_end = align_up(base + size, PMM_PAGE_SIZE);
    if (reserve_end <= reserve_base) {
        return;
    }

    for (uint32_t read_index = 0; read_index < free_page_count; read_index++) {
        uint64_t page_base = free_page_stack[read_index];

        if (range_overlaps(page_base, page_base + PMM_PAGE_SIZE, reserve_base, reserve_end)) {
            continue;
        }
        free_page_stack[write_index++] = page_base;
    }
    free_page_count = write_index;
}

uint32_t pmm_total_pages(void) {
    return tracked_page_count;
}

uint32_t pmm_free_pages(void) {
    return free_page_count;
}

uint32_t pmm_used_pages(void) {
    return tracked_page_count - free_page_count;
}

uint32_t pmm_dropped_pages(void) {
    return dropped_page_count;
}
