#include <nlibc.h>

enum {
    MMSTRESS_PAGE = 4096u,
    MMSTRESS_BASE = 0x0000008001000000ull,
    MMSTRESS_SECOND = 0x0000008001001000ull,
    MMSTRESS_THIRD = 0x0000008001002000ull,
    MMSTRESS_COW = 0x0000008001010000ull,
    MMSTRESS_PROT = 0x0000008001020000ull,
    MMSTRESS_EXECFAIL = 0x0000008001030000ull,
    MMSTRESS_PMMVMM = 0x0000008001040000ull,
    MMSTRESS_PMMVMM_CHILD = 0x0000008001060000ull,
    MMSTRESS_INVALID = 0x0000008001801000ull
};

enum {
    MMSTRESS_PMMVMM_PAGES = 12u,
    MMSTRESS_PMMVMM_CHILD_PAGES = 8u,
    MMSTRESS_PMMVMM_ROUNDS = 4u,
    MMSTRESS_SOAK_DEFAULT_ITERATIONS = 256u,
    MMSTRESS_SOAK_MAX_ITERATIONS = 10000u,
    MMSTRESS_SOAK_PROGRESS_INTERVAL = 64u
};

static void cleanup_mmap(void *addr, size_t length) {
    if (addr != 0 && addr != MAP_FAILED && length != 0u) {
        (void)munmap(addr, length);
    }
}

static void cleanup_shm(const char *name) {
    if (name != 0 && name[0] != '\0') {
        (void)shm_unlink(name);
    }
}

static int vm_snapshot(struct syscall_vm_info *out) {
    return out != 0 && vm_query(out) > 0;
}

static int pmm_snapshot(struct syscall_pmm_info *out) {
    return out != 0 && pmm_query(out) > 0;
}

static int parse_iterations(const char *text, uint32_t *out) {
    char *end = 0;
    unsigned long value;

    if (text == 0 || out == 0 || text[0] == '\0') {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (end == text || end == 0 || *end != '\0' ||
        value == 0ul || value > MMSTRESS_SOAK_MAX_ITERATIONS) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static int vm_maps_equal(const struct syscall_vm_info *first,
                         const struct syscall_vm_info *second) {
    return first != 0 &&
           second != 0 &&
           first->mmap_regions == second->mmap_regions &&
           first->mmap_pages == second->mmap_pages &&
           first->shared_regions == second->shared_regions &&
           first->shm_objects == second->shm_objects &&
           first->shm_mapped_pages == second->shm_mapped_pages;
}

static int vm_not_above_baseline(const struct syscall_vm_info *after,
                                 const struct syscall_vm_info *before) {
    return before != 0 &&
           after != 0 &&
           after->mmap_regions <= before->mmap_regions &&
           after->mmap_pages <= before->mmap_pages &&
           after->shared_regions <= before->shared_regions &&
           after->shm_objects <= before->shm_objects &&
           after->shm_mapped_pages <= before->shm_mapped_pages;
}

static int vm_has_mapping_delta(const struct syscall_vm_info *before,
                                const struct syscall_vm_info *after,
                                uint32_t mmap_regions,
                                uint32_t mmap_pages,
                                uint32_t shared_regions,
                                uint32_t shm_objects,
                                uint32_t shm_mapped_pages) {
    return before != 0 &&
           after != 0 &&
           after->mmap_regions == before->mmap_regions + mmap_regions &&
           after->mmap_pages == before->mmap_pages + mmap_pages &&
           after->shared_regions == before->shared_regions + shared_regions &&
           after->shm_objects == before->shm_objects + shm_objects &&
           after->shm_mapped_pages == before->shm_mapped_pages + shm_mapped_pages;
}

static int wait_for_status_on(uint32_t wait_pid,
                              uint32_t expected_pid,
                              int expected_status) {
    struct syscall_process_info info;

    memset(&info, 0, sizeof(info));
    if (wait(wait_pid, &info) != 1) {
        printf("[mmstress] wait failed pid=%u\n", wait_pid);
        return 0;
    }
    if (info.pid != expected_pid || info.exit_code != expected_status) {
        printf("[mmstress] bad exit pid=%u got=%d want=%d\n",
               info.pid,
               info.exit_code,
               expected_status);
        return 0;
    }
    return 1;
}

static int wait_for_status(uint32_t pid, int expected_status) {
    return wait_for_status_on(pid, pid, expected_status);
}

static int wait_reaped_child_is_gone(uint32_t pid) {
    struct syscall_process_info info;
    int rc;

    memset(&info, 0, sizeof(info));
    rc = wait(pid, &info);
    if (rc != -NEX_ERR_CHILD) {
        printf("[mmstress] rewait pid=%u got=%d want=%d\n",
               pid,
               rc,
               -NEX_ERR_CHILD);
        return 0;
    }
    return 1;
}

static int fork_wait_expect_mode(int (*child_fn)(void),
                                 int expected_status,
                                 int use_wait_last,
                                 int verify_rewait) {
    pid_t child;

    child = fork();
    if (child < 0) {
        printf("[mmstress] fork failed rc=%d\n", child);
        return 0;
    }
    if (child == 0) {
        exit(child_fn());
    }
    if (!wait_for_status_on(use_wait_last ? NEX_WAIT_LAST_PID : (uint32_t)child,
                            (uint32_t)child,
                            expected_status)) {
        return 0;
    }
    return !verify_rewait || wait_reaped_child_is_gone((uint32_t)child);
}

static int fork_wait_expect(int (*child_fn)(void), int expected_status) {
    return fork_wait_expect_mode(child_fn, expected_status, 0, 1);
}

static int mmap_fixed_partial_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info mid;
    struct syscall_vm_info after;
    char *mapped;

    if (!vm_snapshot(&before)) {
        return 20;
    }
    mapped = (char *)mmap((void *)(uintptr_t)MMSTRESS_BASE,
                          MMSTRESS_PAGE * 3u,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          0,
                          0u);
    if (mapped != (char *)(uintptr_t)MMSTRESS_BASE) {
        return 21;
    }
    mapped[0] = 'a';
    mapped[MMSTRESS_PAGE * 2u] = 'z';
    if (!vm_snapshot(&mid) ||
        mid.mmap_regions != before.mmap_regions + 1u ||
        mid.mmap_pages != before.mmap_pages + 3u) {
        cleanup_mmap(mapped, MMSTRESS_PAGE * 3u);
        return 22;
    }
    if (munmap((void *)(uintptr_t)MMSTRESS_SECOND, MMSTRESS_PAGE) != 0 ||
        mapped[0] != 'a' ||
        mapped[MMSTRESS_PAGE * 2u] != 'z') {
        cleanup_mmap(mapped, MMSTRESS_PAGE);
        cleanup_mmap((void *)(uintptr_t)MMSTRESS_THIRD, MMSTRESS_PAGE);
        return 23;
    }
    if (munmap((void *)(uintptr_t)MMSTRESS_BASE, MMSTRESS_PAGE) != 0 ||
        munmap((void *)(uintptr_t)MMSTRESS_THIRD, MMSTRESS_PAGE) != 0 ||
        !vm_snapshot(&after) ||
        !vm_maps_equal(&before, &after)) {
        return 24;
    }
    puts("[mmstress] MAP_FIXED/partial munmap OK");
    return 0;
}

static int mmap_prot_child(void) {
    volatile char *mapped;

    mapped = (volatile char *)mmap((void *)(uintptr_t)MMSTRESS_PROT,
                                   MMSTRESS_PAGE,
                                   PROT_READ,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                   0,
                                   0u);
    if (mapped != (volatile char *)(uintptr_t)MMSTRESS_PROT) {
        return 30;
    }
    (void)mapped[0];
    mapped[0] = 'x';
    return 31;
}

static int invalid_pointer_child(void) {
    volatile uint32_t *bad = (volatile uint32_t *)(uintptr_t)MMSTRESS_INVALID;

    *bad = 0xc001d00du;
    return 40;
}

static int fault_cleanup_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info after;

    if (!vm_snapshot(&before)) {
        return 50;
    }
    for (uint32_t i = 0; i < 4u; i++) {
        if (!fork_wait_expect_mode(mmap_prot_child, -14, i == 1u, 1)) {
            return 51;
        }
    }
    if (!vm_snapshot(&after) || !vm_not_above_baseline(&after, &before)) {
        return 52;
    }
    puts("[mmstress] wait/reap contract OK");
    puts("[mmstress] mmap fault cleanup OK");
    return 0;
}

static int invalid_pointer_cleanup_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info after;

    if (!vm_snapshot(&before)) {
        return 60;
    }
    for (uint32_t i = 0; i < 4u; i++) {
        if (!fork_wait_expect(invalid_pointer_child, -14)) {
            return 61;
        }
    }
    if (!vm_snapshot(&after) || !vm_not_above_baseline(&after, &before)) {
        return 62;
    }
    puts("[mmstress] invalid pointer cleanup OK");
    return 0;
}

static int exec_fail_cleanup_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info mapped_info;
    struct syscall_vm_info after;
    char *private_map;
    int warm_rc;

    warm_rc = exec_replace("/cmd/NO_SUCH_EXEC64.ELF");
    if (warm_rc != -NEX_ERR_NOENT) {
        printf("[mmstress] exec fail warmup rc=%d\n", warm_rc);
        return 100;
    }
    if (!vm_snapshot(&before)) {
        return 101;
    }
    private_map = (char *)mmap((void *)(uintptr_t)MMSTRESS_EXECFAIL,
                               MMSTRESS_PAGE,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                               0,
                               0u);
    if (private_map != (char *)(uintptr_t)MMSTRESS_EXECFAIL) {
        return 102;
    }
    memcpy(private_map, "exec-fail-private", 18u);
    for (uint32_t i = 0; i < 4u; i++) {
        int rc = exec_replace("/cmd/NO_SUCH_EXEC64.ELF");

        if (rc != -NEX_ERR_NOENT ||
            strcmp(private_map, "exec-fail-private") != 0) {
            printf("[mmstress] exec fail state bad iter=%u rc=%d\n", i, rc);
            cleanup_mmap(private_map, MMSTRESS_PAGE);
            return 104;
        }
    }
    if (!vm_snapshot(&mapped_info) ||
        !vm_has_mapping_delta(&before, &mapped_info, 1u, 1u, 0u, 0u, 0u)) {
        printf("[mmstress] exec fail vm delta bad maps=%u/%u pages=%u/%u shared=%u/%u shm=%u/%u shmpages=%u/%u\n",
               mapped_info.mmap_regions,
               before.mmap_regions,
               mapped_info.mmap_pages,
               before.mmap_pages,
               mapped_info.shared_regions,
               before.shared_regions,
               mapped_info.shm_objects,
               before.shm_objects,
               mapped_info.shm_mapped_pages,
               before.shm_mapped_pages);
        cleanup_mmap(private_map, MMSTRESS_PAGE);
        return 105;
    }
    if (munmap(private_map, MMSTRESS_PAGE) != 0 ||
        !vm_snapshot(&after) ||
        !vm_maps_equal(&before, &after)) {
        printf("[mmstress] exec fail cleanup baseline bad maps=%u/%u pages=%u/%u shared=%u/%u shm=%u/%u shmpages=%u/%u\n",
               after.mmap_regions,
               before.mmap_regions,
               after.mmap_pages,
               before.mmap_pages,
               after.shared_regions,
               before.shared_regions,
               after.shm_objects,
               before.shm_objects,
               after.shm_mapped_pages,
               before.shm_mapped_pages);
        return 106;
    }
    puts("[mmstress] exec fail cleanup OK");
    return 0;
}

static int fork_cow_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    char name[48];
    int shm;
    char *cow_page;
    volatile uint32_t *signals;

    snprintf(name, sizeof(name), "mmstress.cow.%u", (unsigned)getpid());
    cleanup_shm(name);
    if (!vm_snapshot(&before)) {
        return 70;
    }
    shm = shm_open(name, MMSTRESS_PAGE, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        cleanup_shm(name);
        return 78;
    }
    signals = (volatile uint32_t *)mmap(0,
                                        MMSTRESS_PAGE,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED,
                                        shm,
                                        0u);
    if (signals == MAP_FAILED) {
        cleanup_shm(name);
        return 79;
    }
    cow_page = (char *)mmap((void *)(uintptr_t)MMSTRESS_COW,
                            MMSTRESS_PAGE,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                            0,
                            0u);
    if (cow_page != (char *)(uintptr_t)MMSTRESS_COW) {
        cleanup_mmap((void *)signals, MMSTRESS_PAGE);
        cleanup_shm(name);
        return 71;
    }
    memset(cow_page, 0, MMSTRESS_PAGE);
    memcpy(cow_page, "parent-cow", 11u);
    memset((void *)signals, 0, MMSTRESS_PAGE);
    for (uint32_t i = 0; i < 4u; i++) {
        pid_t child = fork();

        if (child < 0) {
            cleanup_mmap(cow_page, MMSTRESS_PAGE);
            cleanup_mmap((void *)signals, MMSTRESS_PAGE);
            cleanup_shm(name);
            return 72;
        }
        if (child == 0) {
            if (strcmp(cow_page, "parent-cow") != 0) {
                exit(73);
            }
            memcpy(cow_page, "child-cow", 10u);
            signals[i] = strcmp(cow_page, "child-cow") == 0 ? 1u : 2u;
            exit(signals[i] == 1u ? 74 : 75);
        }
        for (uint32_t spin = 0; signals[i] == 0u && spin < 50u; spin++) {
            sleep(1u);
        }
        if (!wait_for_status((uint32_t)child, 74) ||
            signals[i] != 1u ||
            strcmp(cow_page, "parent-cow") != 0) {
            cleanup_mmap(cow_page, MMSTRESS_PAGE);
            cleanup_mmap((void *)signals, MMSTRESS_PAGE);
            cleanup_shm(name);
            return 76;
        }
    }
    if (munmap(cow_page, MMSTRESS_PAGE) != 0 ||
        munmap((void *)signals, MMSTRESS_PAGE) != 0 ||
        shm_unlink(name) != 0 ||
        !vm_snapshot(&after) ||
        !vm_maps_equal(&before, &after)) {
        cleanup_shm(name);
        return 77;
    }
    puts("[mmstress] fork/COW cleanup OK");
    return 0;
}

static int shared_child_name(const char *name) {
    int shm;
    char *shared;

    shm = shm_open(name, MMSTRESS_PAGE, 0);
    if (shm <= 0) {
        return 80;
    }
    shared = (char *)mmap(0,
                          MMSTRESS_PAGE,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED,
                          shm,
                          0u);
    if (shared == MAP_FAILED) {
        return 81;
    }
    if (strcmp(shared, "parent-live") != 0) {
        cleanup_mmap(shared, MMSTRESS_PAGE);
        return 82;
    }
    memcpy(shared, "child-live", 11u);
    cleanup_mmap(shared, MMSTRESS_PAGE);
    return 83;
}

static int shared_mmap_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    char name[48];
    int shm;
    char *shared;
    pid_t child;

    snprintf(name, sizeof(name), "mmstress.live.%u", (unsigned)getpid());
    cleanup_shm(name);
    if (!vm_snapshot(&before)) {
        return 90;
    }
    shm = shm_open(name, MMSTRESS_PAGE, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        cleanup_shm(name);
        return 91;
    }
    shared = (char *)mmap(0,
                          MMSTRESS_PAGE,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED,
                          shm,
                          0u);
    if (shared == MAP_FAILED) {
        cleanup_shm(name);
        return 92;
    }
    memcpy(shared, "parent-live", 12u);
    child = fork();
    if (child < 0) {
        cleanup_mmap(shared, MMSTRESS_PAGE);
        cleanup_shm(name);
        return 93;
    }
    if (child == 0) {
        exit(shared_child_name(name));
    }
    for (uint32_t spin = 0; strcmp(shared, "child-live") != 0 && spin < 50u; spin++) {
        sleep(1u);
    }
    if (!wait_for_status((uint32_t)child, 83) ||
        strcmp(shared, "child-live") != 0) {
        cleanup_mmap(shared, MMSTRESS_PAGE);
        cleanup_shm(name);
        return 93;
    }
    cleanup_mmap(shared, MMSTRESS_PAGE);
    cleanup_shm(name);
    yield();
    if (!vm_snapshot(&after) || !vm_maps_equal(&before, &after)) {
        return 94;
    }
    puts("[mmstress] shared mmap lifecycle OK");
    return 0;
}

static int pmm_vmm_touch_pages(char *mapped, uint32_t pages, uint32_t seed) {
    for (uint32_t i = 0; i < pages; i++) {
        mapped[(size_t)i * MMSTRESS_PAGE] = (char)('A' + ((seed + i) % 26u));
    }
    for (uint32_t i = 0; i < pages; i++) {
        char expected = (char)('A' + ((seed + i) % 26u));

        if (mapped[(size_t)i * MMSTRESS_PAGE] != expected) {
            return 0;
        }
    }
    return 1;
}

static int pmm_vmm_pressure_once(uint64_t base, uint32_t pages, uint32_t seed) {
    struct syscall_vm_info vm_before;
    struct syscall_vm_info vm_mid;
    struct syscall_vm_info vm_after;
    struct syscall_pmm_info pmm_before;
    struct syscall_pmm_info pmm_mid;
    struct syscall_pmm_info pmm_after;
    size_t length = (size_t)pages * MMSTRESS_PAGE;
    char *mapped;

    if (!vm_snapshot(&vm_before) || !pmm_snapshot(&pmm_before)) {
        return 110;
    }
    mapped = (char *)mmap((void *)(uintptr_t)base,
                          length,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          0,
                          0u);
    if (mapped != (char *)(uintptr_t)base) {
        return 111;
    }
    if (!pmm_vmm_touch_pages(mapped, pages, seed)) {
        cleanup_mmap(mapped, length);
        return 112;
    }
    if (!vm_snapshot(&vm_mid) ||
        !pmm_snapshot(&pmm_mid) ||
        !vm_has_mapping_delta(&vm_before, &vm_mid, 1u, pages, 0u, 0u, 0u) ||
        pmm_before.free_pages < pmm_mid.free_pages + pages) {
        printf("[mmstress] PMM/VMM pressure mid bad maps=%u/%u pages=%u/%u free=%u/%u need=%u\n",
               vm_mid.mmap_regions,
               vm_before.mmap_regions,
               vm_mid.mmap_pages,
               vm_before.mmap_pages,
               pmm_mid.free_pages,
               pmm_before.free_pages,
               pages);
        cleanup_mmap(mapped, length);
        return 113;
    }
    if (munmap(mapped, length) != 0 ||
        !vm_snapshot(&vm_after) ||
        !pmm_snapshot(&pmm_after) ||
        !vm_maps_equal(&vm_before, &vm_after) ||
        pmm_after.free_pages < pmm_mid.free_pages + pages) {
        printf("[mmstress] PMM/VMM pressure cleanup bad maps=%u/%u pages=%u/%u free_after=%u free_mid=%u need=%u\n",
               vm_after.mmap_regions,
               vm_before.mmap_regions,
               vm_after.mmap_pages,
               vm_before.mmap_pages,
               pmm_after.free_pages,
               pmm_mid.free_pages,
               pages);
        return 114;
    }
    return 0;
}

static int pmm_vmm_child_pressure(void) {
    char *mapped;
    size_t length = (size_t)MMSTRESS_PMMVMM_CHILD_PAGES * MMSTRESS_PAGE;

    mapped = (char *)mmap((void *)(uintptr_t)MMSTRESS_PMMVMM_CHILD,
                          length,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          0,
                          0u);
    if (mapped != (char *)(uintptr_t)MMSTRESS_PMMVMM_CHILD) {
        return 120;
    }
    if (!pmm_vmm_touch_pages(mapped, MMSTRESS_PMMVMM_CHILD_PAGES, 7u)) {
        return 121;
    }
    return 0;
}

static int pmm_vmm_child_cleanup_run(int verbose) {
    struct syscall_pmm_info before;
    struct syscall_pmm_info after;

    if (!pmm_snapshot(&before)) {
        return 122;
    }
    for (uint32_t i = 0; i < 4u; i++) {
        if (!fork_wait_expect(pmm_vmm_child_pressure, 0)) {
            return 123;
        }
    }
    if (!pmm_snapshot(&after) || after.free_pages != before.free_pages) {
        printf("[mmstress] PMM/VMM child cleanup leak free=%u/%u\n",
               after.free_pages,
               before.free_pages);
        return 124;
    }
    if (verbose) {
        puts("[mmstress] PMM/VMM child cleanup OK");
    }
    return 0;
}

static int pmm_vmm_stress_run(int verbose) {
    int rc;

    for (uint32_t i = 0; i < MMSTRESS_PMMVMM_ROUNDS; i++) {
        uint64_t base = MMSTRESS_PMMVMM +
                        ((uint64_t)i * MMSTRESS_PMMVMM_PAGES * MMSTRESS_PAGE);

        rc = pmm_vmm_pressure_once(base, MMSTRESS_PMMVMM_PAGES, i);
        if (rc != 0) {
            return rc;
        }
    }
    if (verbose) {
        puts("[mmstress] PMM/VMM mmap pressure OK");
    }
    rc = pmm_vmm_child_cleanup_run(verbose);
    if (rc != 0) {
        return rc;
    }
    if (verbose) {
        puts("[mmstress] PMM/VMM stress PASS");
    }
    return 0;
}

static int pmm_vmm_stress_case(void) {
    return pmm_vmm_stress_run(1);
}

static int pmm_vmm_soak_case(uint32_t iterations) {
    struct syscall_pmm_info start;
    struct syscall_pmm_info before;
    struct syscall_pmm_info after;
    int rc;

    if (iterations == 0u || iterations > MMSTRESS_SOAK_MAX_ITERATIONS) {
        return 130;
    }
    if (!pmm_snapshot(&start)) {
        return 131;
    }
    printf("[mmstress] PMM/VMM soak begin iterations=%u free=%u\n",
           iterations,
           start.free_pages);
    for (uint32_t i = 0; i < iterations; i++) {
        if (!pmm_snapshot(&before)) {
            return 132;
        }
        rc = pmm_vmm_stress_run(0);
        if (rc != 0) {
            printf("[mmstress] PMM/VMM soak failed iteration=%u rc=%d\n",
                   i + 1u,
                   rc);
            return rc;
        }
        if (!pmm_snapshot(&after)) {
            return 133;
        }
        if (after.free_pages != before.free_pages ||
            after.free_pages != start.free_pages) {
            printf("[mmstress] PMM/VMM soak leak iteration=%u start=%u before=%u after=%u\n",
                   i + 1u,
                   start.free_pages,
                   before.free_pages,
                   after.free_pages);
            return 134;
        }
        if (((i + 1u) % MMSTRESS_SOAK_PROGRESS_INTERVAL) == 0u ||
            i + 1u == iterations) {
            printf("[mmstress] PMM/VMM soak progress %u/%u free=%u\n",
                   i + 1u,
                   iterations,
                   after.free_pages);
        }
    }
    if (!pmm_snapshot(&after)) {
        return 135;
    }
    if (after.free_pages != start.free_pages) {
        printf("[mmstress] PMM/VMM soak final leak start=%u after=%u\n",
               start.free_pages,
               after.free_pages);
        return 136;
    }
    printf("[mmstress] PMM/VMM soak PASS iterations=%u free=%u\n",
           iterations,
           after.free_pages);
    return 0;
}

static int strict_mm_case(void) {
    int rc;

    if ((rc = mmap_fixed_partial_case()) != 0 ||
        (rc = fault_cleanup_case()) != 0 ||
        (rc = invalid_pointer_cleanup_case()) != 0 ||
        (rc = exec_fail_cleanup_case()) != 0 ||
        (rc = fork_cow_case()) != 0 ||
        (rc = shared_mmap_case()) != 0) {
        return rc;
    }
    puts("[mmstress] strict MM PASS");
    return 0;
}

static int strict_pmm_vmm_case(void) {
    int rc;

    rc = strict_mm_case();
    if (rc != 0) {
        return rc;
    }
    return pmm_vmm_stress_case();
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "mmap-fixed") == 0) {
        return mmap_fixed_partial_case();
    }
    if (argc > 1 && strcmp(argv[1], "mmap-prot-child") == 0) {
        return mmap_prot_child();
    }
    if (argc > 1 && strcmp(argv[1], "invalid-pointer-child") == 0) {
        return invalid_pointer_child();
    }
    if (argc > 1 && strcmp(argv[1], "fault-cleanup") == 0) {
        return fault_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "invalid-pointer-cleanup") == 0) {
        return invalid_pointer_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "exec-fail-cleanup") == 0) {
        return exec_fail_cleanup_case();
    }
    if (argc > 1 && strcmp(argv[1], "fork-cow") == 0) {
        return fork_cow_case();
    }
    if (argc > 2 && strcmp(argv[1], "shared-child") == 0) {
        return shared_child_name(argv[2]);
    }
    if (argc > 1 && strcmp(argv[1], "shared-mmap") == 0) {
        return shared_mmap_case();
    }
    if (argc > 1 && strcmp(argv[1], "strict-mm") == 0) {
        return strict_mm_case();
    }
    if (argc > 1 && strcmp(argv[1], "pmm-vmm-only") == 0) {
        return pmm_vmm_stress_case();
    }
    if (argc > 1 && strcmp(argv[1], "pmm-vmm") == 0) {
        return strict_pmm_vmm_case();
    }
    if (argc > 1 &&
        (strcmp(argv[1], "soak") == 0 ||
         strcmp(argv[1], "pmm-vmm-soak") == 0)) {
        uint32_t iterations = MMSTRESS_SOAK_DEFAULT_ITERATIONS;

        if (argc > 2 && !parse_iterations(argv[2], &iterations)) {
            printf("[mmstress] usage: %s [1..%u]\n",
                   argv[1],
                   MMSTRESS_SOAK_MAX_ITERATIONS);
            return 129;
        }
        return pmm_vmm_soak_case(iterations);
    }
    return strict_pmm_vmm_case();
}
