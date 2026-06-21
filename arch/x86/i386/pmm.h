#pragma once

typedef unsigned int uint32_t;

struct bootx_boot_info;

enum {
    I386_PMM_PAGE_SIZE = 4096u,
    I386_PMM_INVALID_PAGE = 0xffffffffu
};

int i386_pmm_init(const struct bootx_boot_info *boot_info);
uint32_t i386_pmm_alloc_page(void);
int i386_pmm_free_page(uint32_t physical_address);
uint32_t i386_pmm_total_pages(void);
uint32_t i386_pmm_free_pages(void);
uint32_t i386_pmm_reserved_pages(void);
uint32_t i386_pmm_highest_address(void);
