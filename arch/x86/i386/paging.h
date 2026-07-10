#pragma once

typedef unsigned int uint32_t;

enum {
    I386_PAGE_SIZE = 4096u,
    I386_PAGING_IDENTITY_LIMIT = 16u * 1024u * 1024u,
    I386_PAGING_DYNAMIC_BASE = 0xd0000000u
};

int i386_paging_init(void);
int i386_paging_enabled(void);
uint32_t i386_paging_root(void);
int i386_paging_translate(uint32_t virtual_address, uint32_t *physical_address);
int i386_paging_map_page(uint32_t virtual_address,
                         uint32_t physical_address,
                         int writable,
                         int user_accessible);
int i386_paging_unmap_page(uint32_t virtual_address, uint32_t *physical_address);
uint32_t i386_paging_kernel_root(void);
uint32_t i386_paging_create_address_space(void);
int i386_paging_map_page_in(uint32_t root,
                            uint32_t virtual_address,
                            uint32_t physical_address,
                            int writable,
                            int user_accessible);
int i386_paging_unmap_page_in(uint32_t root,
                              uint32_t virtual_address,
                              uint32_t *physical_address);
int i386_paging_protect_page_in(uint32_t root,
                                uint32_t virtual_address,
                                int writable,
                                int user_accessible);
int i386_paging_translate_in(uint32_t root,
                             uint32_t virtual_address,
                             uint32_t *physical_address);
int i386_paging_user_accessible_in(uint32_t root,
                                   uint32_t virtual_address,
                                   int writable);
int i386_paging_temporary_map(uint32_t physical_address,
                              uint32_t slot,
                              void **virtual_address);
void i386_paging_temporary_unmap(uint32_t slot);
void i386_paging_switch(uint32_t root);
