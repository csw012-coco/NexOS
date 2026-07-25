#pragma once

#include "kernel/internal/proc/process_internal_base.h"

struct syscall_vm_info;

void addrspace_reset(struct address_space *address_space);
int addrspace_map_page_at(uint64_t virt_addr, uint32_t perms);
int addrspace_map_range(uint64_t start, uint64_t end);
void addrspace_release_dynamic_pages(void);
void addrspace_release_dynamic_pages_with_backend(
    int32_t (*page_free)(uint32_t user_page),
    int32_t (*shared_page_unmap)(uint32_t user_page));
void addrspace_release_dynamic_pages_for_pid_with_backend(
    uint32_t pid,
    int32_t (*page_free_pid)(uint32_t pid, uint32_t user_page),
    int32_t (*shared_page_unmap_pid)(uint32_t pid, uint32_t user_page));
void addrspace_unmap_range_if_present(uint64_t start, uint64_t end);
int addrspace_map_range_with_perms(uint64_t start, uint64_t end, uint32_t perms);
int addrspace_zero_range(uint64_t start, uint64_t size);
int addrspace_copy_to_range(uint64_t dest, const uint8_t *src, uint64_t size);
int addrspace_fork_retain_shared(uint64_t child_root,
                                struct user_page_mapping *child_mappings);
int addrspace_track_existing_page(uint64_t virt_addr,
                                  uint32_t perms,
                                  int shared,
                                  uint16_t shm_slot);
void addrspace_untrack_range(uint64_t start, uint64_t length);
int addrspace_note_protect_range(uint64_t start, uint64_t length, uint32_t perms);
int addrspace_page_is_shared(uint64_t virt_addr);
int addrspace_release_page_with_backend(
    uint64_t virt_addr,
    int32_t (*page_free)(uint32_t user_page),
    int32_t (*shared_page_unmap)(uint32_t user_page));
int addrspace_release_page_for_pid_with_backend(
    uint32_t pid,
    uint64_t virt_addr,
    int32_t (*page_free_pid)(uint32_t pid, uint32_t user_page),
    int32_t (*shared_page_unmap_pid)(uint32_t pid, uint32_t user_page));
void addrspace_vm_snapshot(struct syscall_vm_info *info);
int addrspace_shm_open(const char *name, uint64_t size, uint32_t flags);
int addrspace_shm_unlink(const char *name);
uint64_t addrspace_shm_frame(uint32_t handle, uint32_t page_index);
uint64_t addrspace_shm_size(uint32_t handle);
int addrspace_shm_note_mapping(uint32_t handle);
void addrspace_shm_note_unmapping(uint32_t handle);
uint64_t addrspace_mmap(uint64_t requested_addr,
                        uint64_t length,
                        uint32_t prot,
                        uint32_t flags,
                        uint32_t shm_handle,
                        uint64_t offset);
int addrspace_munmap(uint64_t addr, uint64_t length);
