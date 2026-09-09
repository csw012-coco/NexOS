#pragma once

typedef unsigned int uint32_t;

typedef int (*i386_paging_shared_page_fn)(uint32_t virtual_address,
                                          void *context);

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
uint32_t i386_paging_clone_user_eager(uint32_t source_root);
uint32_t i386_paging_clone_user_cow_ex(uint32_t source_root,
                                       i386_paging_shared_page_fn is_shared,
                                       void *context);
uint32_t i386_paging_clone_user_cow(uint32_t source_root);
void i386_paging_destroy_user_space(uint32_t root);
int i386_paging_resolve_cow_fault(uint32_t root,
                                  uint32_t fault_address,
                                  uint32_t error_code);
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
