#include "hal/x86/platform.h"

void hal_x86_paging_init_impl(uint64_t kernel_phys_addr) {
    paging_init(kernel_phys_addr);
}

uint64_t hal_x86_paging_current_root_impl(void) {
    return paging_get_current_cr3();
}

void hal_x86_paging_switch_root_impl(uint64_t cr3) {
    paging_set_current_cr3(cr3);
}

uint64_t hal_x86_paging_create_user_root_impl(void) {
    return paging_create_user_root();
}

void hal_x86_paging_allow_user_page_impl(uint64_t addr) {
    paging_make_page_user_accessible(addr);
}

void hal_x86_paging_allow_user_range_impl(uint64_t start, uint64_t end) {
    paging_make_range_user_accessible(start, end);
}

void hal_x86_paging_set_supervisor_range_impl(uint64_t start, uint64_t end) {
    paging_make_range_supervisor_only(start, end);
}

int hal_x86_paging_map_page_impl(uint64_t virt_addr, uint64_t phys_addr, int user_accessible, int writable) {
    return paging_map_page(virt_addr, phys_addr, user_accessible, writable);
}

int hal_x86_paging_map_page_with_exec_impl(uint64_t virt_addr,
                                           uint64_t phys_addr,
                                           int user_accessible,
                                           int writable,
                                           int executable) {
    return paging_map_page_with_exec(virt_addr, phys_addr, user_accessible, writable, executable);
}

int hal_x86_paging_unmap_page_impl(uint64_t virt_addr, uint64_t *phys_addr) {
    return paging_unmap_page(virt_addr, phys_addr);
}

int hal_x86_paging_get_mapping_impl(uint64_t virt_addr, uint64_t *phys_addr) {
    return paging_get_mapping(virt_addr, phys_addr);
}

int hal_x86_paging_get_mapping_info_impl(uint64_t virt_addr, uint64_t *phys_addr, uint64_t *flags) {
    return paging_get_mapping_info(virt_addr, phys_addr, flags);
}

void *hal_x86_paging_phys_direct_map_impl(uint64_t phys_addr) {
    return paging_phys_direct_map(phys_addr);
}
