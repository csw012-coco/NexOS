#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/public/mem/pmm.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_reference_lifecycle(void) {
    const struct bootx_memmap_entry map[] = {
        { .base = 0x100000u, .length = 8u * 4096u, .type = BOOTX_MEMMAP_USABLE }
    };
    uint64_t page;

    pmm_init(map, 1u, 0, 0);
    CHECK(pmm_total_pages() == 8u);
    CHECK(pmm_free_pages() == 8u);

    page = pmm_alloc_page();
    CHECK(page != 0);
    CHECK(pmm_ref_count(page) == 1u);
    CHECK(pmm_free_pages() == 7u);
    CHECK(pmm_retain_page(page));
    CHECK(pmm_ref_count(page) == 2u);
    CHECK(pmm_release_page(page));
    CHECK(pmm_ref_count(page) == 1u);
    CHECK(pmm_free_pages() == 7u);
    CHECK(pmm_free_page(page));
    CHECK(pmm_ref_count(page) == 0u);
    CHECK(pmm_free_pages() == 8u);
    CHECK(!pmm_free_page(page));
    CHECK(!pmm_retain_page(page));
    CHECK(!pmm_release_page(page + 1u));
    return 0;
}

static int test_contiguous_and_reserve(void) {
    const struct bootx_memmap_entry map[] = {
        { .base = 0x200000u, .length = 16u * 4096u, .type = BOOTX_MEMMAP_USABLE }
    };
    uint64_t base;

    pmm_init(map, 1u, 0, 0);
    pmm_reserve_range(0x204000u, 2u * 4096u);
    CHECK(pmm_free_pages() == 14u);
    CHECK(pmm_ref_count(0x204000u) == 1u);
    CHECK(pmm_ref_count(0x205000u) == 1u);

    base = pmm_alloc_contiguous(3u);
    CHECK(base != 0);
    CHECK(pmm_ref_count(base) == 1u);
    CHECK(pmm_ref_count(base + 4096u) == 1u);
    CHECK(pmm_ref_count(base + 8192u) == 1u);
    CHECK(pmm_release_page(base));
    CHECK(pmm_release_page(base + 4096u));
    CHECK(pmm_release_page(base + 8192u));
    CHECK(pmm_free_pages() == 14u);
    return 0;
}

int main(void) {
    if (test_reference_lifecycle() != 0) {
        return EXIT_FAILURE;
    }
    if (test_contiguous_and_reserve() != 0) {
        return EXIT_FAILURE;
    }
    puts("PMM reference-count tests passed");
    return EXIT_SUCCESS;
}
