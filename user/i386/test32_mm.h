#pragma once

#include <nlibc.h>

int test32_shm_child(void);
int test32_mmap_kill_child(void);
int test32_shared_fault_child(void);
int test32_invalid_pointer_child(void);

int test32_mmap_fixed_partial(void);
int test32_mmap_prot_child(void);
int test32_mmap_protection(void);
int test32_mmap_fault_cleanup_case(void);
int test32_mmap_kill_cleanup_case(void);
int test32_shared_fault_cleanup_case(void);
int test32_invalid_pointer_cleanup_case(void);
int test32_mprotect_child(void);
int test32_mprotect_case(void);
int test32_mprotect_partial_child(void);
int test32_mprotect_partial_case(void);
int test32_mmap_shm_basic_case(void);
int test32_live_shared_mmap_case(void);
int test32_shm_lifecycle_case(void);
