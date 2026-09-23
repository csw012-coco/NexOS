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
    const struct janus_memmap_entry map[] = {
        { .base = 0x100000u, .length = 8u * 4096u, .type = JANUS_MEMMAP_USABLE }
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
    const struct janus_memmap_entry map[] = {
        { .base = 0x200000u, .length = 16u * 4096u, .type = JANUS_MEMMAP_USABLE }
    };
    uint64_t base;

    pmm_init(map, 1u, 0, 0);
    pmm_reserve_range(0x204000u, 2u * 4096u);
    CHECK(pmm_free_pages() == 14u);
    CHECK(pmm_ref_count(0x204000u) == 0u);
    CHECK(pmm_ref_count(0x205000u) == 0u);
    CHECK(!pmm_retain_page(0x204000u));
    CHECK(!pmm_free_page(0x205000u));

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

static int test_exhaustion_reuse_stress(void) {
    enum {
        PAGE_COUNT = 64
    };
    const struct janus_memmap_entry map[] = {
        { .base = 0x400000u, .length = PAGE_COUNT * 4096u, .type = JANUS_MEMMAP_USABLE }
    };
    uint64_t pages[PAGE_COUNT];

    pmm_init(map, 1u, 0, 0);
    CHECK(pmm_total_pages() == PAGE_COUNT);
    CHECK(pmm_free_pages() == PAGE_COUNT);

    for (uint32_t i = 0; i < PAGE_COUNT; i++) {
        pages[i] = pmm_alloc_page();
        CHECK(pages[i] != 0);
        CHECK((pages[i] & 0xfffu) == 0u);
        CHECK(pmm_ref_count(pages[i]) == 1u);
    }
    CHECK(pmm_free_pages() == 0u);
    CHECK(pmm_alloc_page() == 0u);

    for (uint32_t round = 0; round < 8u; round++) {
        for (uint32_t i = 0; i < PAGE_COUNT; i += 3u) {
            CHECK(pmm_retain_page(pages[i]));
            CHECK(pmm_ref_count(pages[i]) == 2u);
            CHECK(pmm_release_page(pages[i]));
            CHECK(pmm_ref_count(pages[i]) == 1u);
        }
    }

    for (uint32_t i = 0; i < PAGE_COUNT; i += 2u) {
        CHECK(pmm_release_page(pages[i]));
        CHECK(pmm_ref_count(pages[i]) == 0u);
    }
    CHECK(pmm_free_pages() == PAGE_COUNT / 2u);

    for (uint32_t i = 0; i < PAGE_COUNT; i += 2u) {
        uint64_t page = pmm_alloc_page();

        CHECK(page != 0);
        CHECK(pmm_ref_count(page) == 1u);
        CHECK(pmm_release_page(page));
    }
    CHECK(pmm_free_pages() == PAGE_COUNT / 2u);

    for (uint32_t i = 1u; i < PAGE_COUNT; i += 2u) {
        CHECK(pmm_release_page(pages[i]));
    }
    CHECK(pmm_free_pages() == PAGE_COUNT);
    return 0;
}

static int test_fragmented_contiguous_stress(void) {
    enum {
        PAGE_COUNT = 32
    };
    const struct janus_memmap_entry map[] = {
        { .base = 0x800000u, .length = PAGE_COUNT * 4096u, .type = JANUS_MEMMAP_USABLE }
    };
    uint64_t pages[PAGE_COUNT];
    uint64_t span;

    pmm_init(map, 1u, 0, 0);

    for (uint32_t i = 0; i < PAGE_COUNT; i++) {
        pages[i] = pmm_alloc_page();
        CHECK(pages[i] != 0);
    }

    for (uint32_t i = 0; i < PAGE_COUNT; i += 2u) {
        CHECK(pmm_release_page(pages[i]));
    }
    CHECK(pmm_alloc_contiguous(2u) == 0u);

    for (uint32_t i = 1u; i < PAGE_COUNT; i += 2u) {
        CHECK(pmm_release_page(pages[i]));
    }
    CHECK(pmm_free_pages() == PAGE_COUNT);

    pmm_init(map, 1u, 0, 0);
    span = pmm_alloc_contiguous(8u);
    CHECK(span != 0);
    for (uint32_t i = 0; i < 8u; i++) {
        CHECK(pmm_ref_count(span + (uint64_t)i * 4096u) == 1u);
    }
    for (uint32_t i = 0; i < 8u; i++) {
        CHECK(pmm_release_page(span + (uint64_t)i * 4096u));
    }
    CHECK(pmm_free_pages() == PAGE_COUNT);
    return 0;
}

int main(void) {
    if (test_reference_lifecycle() != 0) {
        return EXIT_FAILURE;
    }
    if (test_contiguous_and_reserve() != 0) {
        return EXIT_FAILURE;
    }
    if (test_exhaustion_reuse_stress() != 0) {
        return EXIT_FAILURE;
    }
    if (test_fragmented_contiguous_stress() != 0) {
        return EXIT_FAILURE;
    }
    puts("PMM reference-count/stress tests passed");
    return EXIT_SUCCESS;
}
