#include "test32_helpers.h"

int test32_make_unique_name(char *out, size_t out_size, const char *prefix) {
    if (out == 0 || out_size == 0u || prefix == 0 || prefix[0] == '\0') {
        return 0;
    }
    return snprintf(out, out_size, "%s.%u", prefix, (unsigned)getpid()) > 0;
}

void test32_cleanup_mmap(void *addr, size_t length) {
    if (addr != 0 && addr != MAP_FAILED && length != 0u) {
        (void)munmap(addr, length);
    }
}

void test32_cleanup_shm(const char *name) {
    if (name != 0 && name[0] != '\0') {
        (void)shm_unlink(name);
    }
}

void test32_cleanup_mmap_shm(void *addr, size_t length, const char *name) {
    test32_cleanup_mmap(addr, length);
    test32_cleanup_shm(name);
}

void test32_cleanup_two_mmaps(void *first,
                              size_t first_length,
                              void *second,
                              size_t second_length) {
    test32_cleanup_mmap(first, first_length);
    test32_cleanup_mmap(second, second_length);
}

void test32_cleanup_two_mmaps_shm(void *first,
                                  size_t first_length,
                                  void *second,
                                  size_t second_length,
                                  const char *name) {
    test32_cleanup_two_mmaps(first, first_length, second, second_length);
    test32_cleanup_shm(name);
}

pid_t test32_spawn_background(const char *command) {
    if (command == 0 || command[0] == '\0') {
        return -1;
    }
    return spawn_ex(command, SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND);
}

int test32_wait_expect(pid_t child, int expected) {
    if (child <= 0) {
        return 0;
    }
    return waitpid(child) == expected;
}

int test32_spawn_wait_expect(const char *command, int expected) {
    return test32_wait_expect(test32_spawn_background(command), expected);
}

int test32_run_subtests(const struct test32_subtest *tests,
                        size_t count,
                        int fail_code) {
    if (tests == 0) {
        return fail_code;
    }
    for (size_t i = 0u; i < count; i++) {
        if (!test32_spawn_wait_expect(tests[i].command,
                                      tests[i].expected_status)) {
            return fail_code;
        }
    }
    return 0;
}

int test32_puts_pass(const char *line, int fail_code) {
    if (line == 0 || puts(line) == EOF) {
        return fail_code;
    }
    return 0;
}

int test32_vm_snapshot(struct syscall_vm_info *out) {
    return out != 0 && vm_query(out) > 0;
}

int test32_vm_private_maps_equal(const struct syscall_vm_info *first,
                                 const struct syscall_vm_info *second) {
    return first != 0 &&
           second != 0 &&
           first->mmap_regions == second->mmap_regions &&
           first->mmap_pages == second->mmap_pages;
}

int test32_vm_shared_maps_equal(const struct syscall_vm_info *first,
                                const struct syscall_vm_info *second) {
    return first != 0 &&
           second != 0 &&
           first->shared_regions == second->shared_regions &&
           first->shm_objects == second->shm_objects &&
           first->shm_mapped_pages == second->shm_mapped_pages;
}

int test32_vm_maps_equal(const struct syscall_vm_info *first,
                         const struct syscall_vm_info *second) {
    return test32_vm_private_maps_equal(first, second) &&
           test32_vm_shared_maps_equal(first, second);
}

int test32_vm_has_delta(const struct syscall_vm_info *before,
                        const struct syscall_vm_info *after,
                        uint32_t mmap_regions_delta,
                        uint32_t mmap_pages_delta,
                        uint32_t shared_regions_delta,
                        uint32_t shm_objects_delta,
                        uint32_t shm_mapped_pages_delta) {
    return before != 0 &&
           after != 0 &&
           after->mmap_regions == before->mmap_regions + mmap_regions_delta &&
           after->mmap_pages == before->mmap_pages + mmap_pages_delta &&
           after->shared_regions == before->shared_regions + shared_regions_delta &&
           after->shm_objects == before->shm_objects + shm_objects_delta &&
           after->shm_mapped_pages == before->shm_mapped_pages + shm_mapped_pages_delta;
}

int test32_vm_has_mapping_delta(const struct syscall_vm_info *before,
                                const struct syscall_vm_info *after,
                                uint32_t mmap_regions_delta,
                                uint32_t mmap_pages_delta,
                                uint32_t shared_regions_delta,
                                uint32_t shm_mapped_pages_delta) {
    return before != 0 &&
           after != 0 &&
           after->mmap_regions == before->mmap_regions + mmap_regions_delta &&
           after->mmap_pages == before->mmap_pages + mmap_pages_delta &&
           after->shared_regions == before->shared_regions + shared_regions_delta &&
           after->shm_mapped_pages == before->shm_mapped_pages + shm_mapped_pages_delta;
}

int test32_vm_not_above_mapping_baseline(const struct syscall_vm_info *after,
                                         const struct syscall_vm_info *before) {
    return before != 0 &&
           after != 0 &&
           after->mmap_regions <= before->mmap_regions &&
           after->mmap_pages <= before->mmap_pages &&
           after->shared_regions <= before->shared_regions &&
           after->shm_objects <= before->shm_objects &&
           after->shm_mapped_pages <= before->shm_mapped_pages;
}

void test32_vm_print_maps_mismatch(const char *prefix,
                                   const struct syscall_vm_info *before,
                                   const struct syscall_vm_info *after) {
    if (before == 0 || after == 0) {
        return;
    }
    printf("%s r=%u/%u p=%u/%u sr=%u/%u so=%u/%u sp=%u/%u\n",
           prefix != 0 ? prefix : "[test32] vm mismatch",
           before->mmap_regions,
           after->mmap_regions,
           before->mmap_pages,
           after->mmap_pages,
           before->shared_regions,
           after->shared_regions,
           before->shm_objects,
           after->shm_objects,
           before->shm_mapped_pages,
           after->shm_mapped_pages);
}
