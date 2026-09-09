#pragma once

#include <nlibc.h>

#define TEST32_ELF_PATH "/cmd/test32"

struct test32_subtest {
    const char *command;
    int expected_status;
};

int test32_make_unique_name(char *out, size_t out_size, const char *prefix);
void test32_cleanup_mmap(void *addr, size_t length);
void test32_cleanup_shm(const char *name);
void test32_cleanup_mmap_shm(void *addr, size_t length, const char *name);
void test32_cleanup_two_mmaps(void *first,
                              size_t first_length,
                              void *second,
                              size_t second_length);
void test32_cleanup_two_mmaps_shm(void *first,
                                  size_t first_length,
                                  void *second,
                                  size_t second_length,
                                  const char *name);
pid_t test32_spawn_background(const char *command);
int test32_wait_expect(pid_t child, int expected);
int test32_spawn_wait_expect(const char *command, int expected);
int test32_run_subtests(const struct test32_subtest *tests,
                        size_t count,
                        int fail_code);
int test32_puts_pass(const char *line, int fail_code);
int test32_vm_snapshot(struct syscall_vm_info *out);
int test32_vm_private_maps_equal(const struct syscall_vm_info *first,
                                 const struct syscall_vm_info *second);
int test32_vm_shared_maps_equal(const struct syscall_vm_info *first,
                                const struct syscall_vm_info *second);
int test32_vm_maps_equal(const struct syscall_vm_info *first,
                         const struct syscall_vm_info *second);
int test32_vm_has_delta(const struct syscall_vm_info *before,
                        const struct syscall_vm_info *after,
                        uint32_t mmap_regions_delta,
                        uint32_t mmap_pages_delta,
                        uint32_t shared_regions_delta,
                        uint32_t shm_objects_delta,
                        uint32_t shm_mapped_pages_delta);
int test32_vm_has_mapping_delta(const struct syscall_vm_info *before,
                                const struct syscall_vm_info *after,
                                uint32_t mmap_regions_delta,
                                uint32_t mmap_pages_delta,
                                uint32_t shared_regions_delta,
                                uint32_t shm_mapped_pages_delta);
int test32_vm_not_above_mapping_baseline(const struct syscall_vm_info *after,
                                         const struct syscall_vm_info *before);
void test32_vm_print_maps_mismatch(const char *prefix,
                                   const struct syscall_vm_info *before,
                                   const struct syscall_vm_info *after);
