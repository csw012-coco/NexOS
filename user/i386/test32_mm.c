#include "test32_mm.h"

int test32_shm_child(void) {
    int shm;
    void *shared;

    shm = shm_open("test32.live", 4096u, 0);
    if (shm <= 0) {
        return 141;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED) {
        return 142;
    }
    if (strcmp((char *)shared, "parent-live") != 0) {
        return 143;
    }
    memcpy(shared, "child-live", 11u);
    if (munmap(shared, 4096u) != 0) {
        return 144;
    }
    return 0;
}

int test32_mmap_kill_child(void) {
    int shm;
    void *mapped;
    void *shared;

    mapped = mmap((void *)(uintptr_t)0x51040000u,
                  8192u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                  0,
                  0u);
    if (mapped != (void *)(uintptr_t)0x51040000u) {
        return 176;
    }
    shm = shm_open("test32.kill", 4096u, SHM_CREATE);
    if (shm <= 0) {
        return 177;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED) {
        return 178;
    }
    ((char *)mapped)[0] = 'k';
    memcpy(shared, "kill-live", 10u);
    for (;;) {
        sleep(10u);
    }
    return 0;
}

int test32_shared_fault_child(void) {
    int shm;
    void *private_map;
    void *shared_map;
    volatile uint32_t *bad;

    shm = shm_open("test32.sharedfault", 4096u, 0);
    if (shm <= 0) {
        return 182;
    }
    private_map = mmap((void *)(uintptr_t)0x51080000u,
                       4096u,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                       0,
                       0u);
    if (private_map != (void *)(uintptr_t)0x51080000u) {
        return 183;
    }
    shared_map = mmap(0,
                      4096u,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      shm,
                      0u);
    if (shared_map == MAP_FAILED) {
        return 184;
    }
    ((char *)private_map)[0] = 'p';
    memcpy(shared_map, "shared-fault", 13u);
    bad = (volatile uint32_t *)(uintptr_t)0x70000000u;
    *bad = 0xdeadbeefu;
    return 185;
}

int test32_invalid_pointer_child(void) {
    void *mapped;
    volatile uint32_t *bad;

    mapped = mmap((void *)(uintptr_t)0x51090000u,
                  8192u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                  0,
                  0u);
    if (mapped != (void *)(uintptr_t)0x51090000u) {
        return 186;
    }
    ((char *)mapped)[0] = 'i';
    ((char *)mapped)[4096] = 'p';
    bad = (volatile uint32_t *)(uintptr_t)0x70001000u;
    *bad = 0xc001d00du;
    return 187;
}

int test32_mmap_fixed_partial(void) {
    struct syscall_vm_info vm_info;
    uint32_t base_regions;
    uint32_t base_pages;
    void *mapped;

    if (vm_query(&vm_info) <= 0) {
        return 153;
    }
    base_regions = vm_info.mmap_regions;
    base_pages = vm_info.mmap_pages;
    mapped = mmap((void *)(uintptr_t)0x51000000u,
                  12288u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                  0,
                  0u);
    if (mapped != (void *)(uintptr_t)0x51000000u ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 1u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 153;
    }
    ((char *)mapped)[0] = 'f';
    ((char *)mapped)[8192] = 't';
    if (munmap((void *)(uintptr_t)0x51001000u, 4096u) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 2u ||
        vm_info.mmap_pages != base_pages + 2u ||
        ((char *)mapped)[0] != 'f' ||
        ((char *)mapped)[8192] != 't') {
        return 154;
    }
    if (munmap((void *)(uintptr_t)0x51000000u, 4096u) != 0 ||
        munmap((void *)(uintptr_t)0x51002000u, 4096u) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions ||
        vm_info.mmap_pages != base_pages) {
        return 155;
    }
    if (puts("[test32] libc32 MAP_FIXED/partial munmap OK") == EOF) {
        return 156;
    }
    return 0;
}

int test32_mmap_prot_child(void) {
    volatile char *mapped;

    mapped = (volatile char *)mmap((void *)(uintptr_t)0x51010000u,
                                   4096u,
                                   PROT_READ,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                   0,
                                   0u);
    if (mapped != (volatile char *)(uintptr_t)0x51010000u) {
        return 157;
    }
    (void)mapped[0];
    mapped[0] = 'x';
    return 158;
}

int test32_mmap_protection(void) {
    void *mapped;
    pid_t child;

    mapped = mmap((void *)(uintptr_t)0x51010000u,
                  4096u,
                  PROT_READ,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                  0,
                  0u);
    if (mapped != (void *)(uintptr_t)0x51010000u) {
        return 157;
    }
    (void)((volatile char *)mapped)[0];
    if (munmap(mapped, 4096u) != 0) {
        return 158;
    }
    child = spawn_ex("/cmd/test32 mmap-prot-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != -14) {
        return 159;
    }
    if (puts("[test32] libc32 mmap protection fault OK") == EOF) {
        return 160;
    }
    return 0;
}

int test32_mmap_fault_cleanup_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    pid_t child;

    if (vm_query(&before) <= 0) {
        return 172;
    }
    for (uint32_t i = 0u; i < 4u; i++) {
        child = spawn_ex("/cmd/test32 mmap-prot-child",
                         SYS_SPAWN_ELF,
                         SYS_SPAWN_BACKGROUND);
        if (child <= 0 || waitpid(child) != -14) {
            return 173;
        }
    }
    if (vm_query(&after) <= 0 ||
        after.mmap_regions != before.mmap_regions ||
        after.mmap_pages != before.mmap_pages ||
        after.shared_regions != before.shared_regions ||
        after.shm_mapped_pages != before.shm_mapped_pages) {
        return 174;
    }
    if (puts("[test32] libc32 mmap fault cleanup OK") == EOF) {
        return 175;
    }
    return 0;
}

int test32_mmap_kill_cleanup_case(void) {
    struct syscall_pmm_info pmm_before;
    struct syscall_pmm_info pmm_after;
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    pid_t child;

    shm_unlink("test32.kill");
    if (pmm_query(&pmm_before) <= 0 || vm_query(&before) <= 0) {
        return 176;
    }
    child = spawn_ex("/cmd/test32 mmap-kill-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0) {
        shm_unlink("test32.kill");
        return 177;
    }
    sleep(2u);
    if (kill(child) != 1 ||
        waitpid(child) != -9) {
        shm_unlink("test32.kill");
        return 178;
    }
    if (shm_unlink("test32.kill") != 0) {
        return 179;
    }
    if (pmm_query(&pmm_after) <= 0 ||
        vm_query(&after) <= 0 ||
        pmm_after.free_pages != pmm_before.free_pages ||
        after.mmap_regions != before.mmap_regions ||
        after.mmap_pages != before.mmap_pages ||
        after.shared_regions != before.shared_regions ||
        after.shm_objects != before.shm_objects ||
        after.shm_mapped_pages != before.shm_mapped_pages) {
        return 180;
    }
    if (puts("[test32] libc32 mmap kill cleanup OK") == EOF) {
        return 181;
    }
    return 0;
}

int test32_shared_fault_cleanup_case(void) {
    struct syscall_pmm_info pmm_before;
    struct syscall_pmm_info pmm_after;
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    int shm;
    pid_t child;

    (void)shm_unlink("test32.sharedfault");
    if (pmm_query(&pmm_before) <= 0 || vm_query(&before) <= 0) {
        return 188;
    }
    shm = shm_open("test32.sharedfault", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        return 189;
    }
    child = spawn_ex("/cmd/test32 shared-fault-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 || waitpid(child) != -14) {
        (void)shm_unlink("test32.sharedfault");
        return 190;
    }
    if (shm_unlink("test32.sharedfault") != 0) {
        return 191;
    }
    if (pmm_query(&pmm_after) <= 0 ||
        vm_query(&after) <= 0 ||
        pmm_after.free_pages != pmm_before.free_pages ||
        after.mmap_regions != before.mmap_regions ||
        after.mmap_pages != before.mmap_pages ||
        after.shared_regions != before.shared_regions ||
        after.shm_objects != before.shm_objects ||
        after.shm_mapped_pages != before.shm_mapped_pages) {
        return 192;
    }
    if (puts("[test32] libc32 shared fault cleanup OK") == EOF) {
        return 193;
    }
    return 0;
}

int test32_invalid_pointer_cleanup_case(void) {
    struct syscall_pmm_info pmm_before;
    struct syscall_pmm_info pmm_after;
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    pid_t child;

    if (pmm_query(&pmm_before) <= 0 || vm_query(&before) <= 0) {
        return 194;
    }
    child = spawn_ex("/cmd/test32 invalid-pointer-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 || waitpid(child) != -14) {
        return 195;
    }
    if (pmm_query(&pmm_after) <= 0 ||
        vm_query(&after) <= 0 ||
        pmm_after.free_pages != pmm_before.free_pages ||
        after.mmap_regions != before.mmap_regions ||
        after.mmap_pages != before.mmap_pages ||
        after.shared_regions != before.shared_regions ||
        after.shm_objects != before.shm_objects ||
        after.shm_mapped_pages != before.shm_mapped_pages) {
        return 196;
    }
    if (puts("[test32] libc32 invalid pointer fault cleanup OK") == EOF) {
        return 197;
    }
    return 0;
}

int test32_mprotect_child(void) {
    volatile char *mapped;

    mapped = (volatile char *)mmap((void *)(uintptr_t)0x51020000u,
                                   4096u,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                   0,
                                   0u);
    if (mapped != (volatile char *)(uintptr_t)0x51020000u) {
        return 161;
    }
    mapped[0] = 'w';
    if (mprotect((void *)(uintptr_t)0x51020000u, 4096u, PROT_READ) != 0) {
        return 162;
    }
    (void)mapped[0];
    mapped[0] = 'x';
    return 163;
}

int test32_mprotect_case(void) {
    char *mapped;
    pid_t child;

    mapped = (char *)mmap((void *)(uintptr_t)0x51020000u,
                          4096u,
                          PROT_READ,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          0,
                          0u);
    if (mapped != (char *)(uintptr_t)0x51020000u) {
        return 161;
    }
    if (mprotect(mapped, 4096u, PROT_READ | PROT_WRITE) != 0) {
        return 162;
    }
    mapped[0] = 'm';
    if (mapped[0] != 'm' ||
        mprotect(mapped, 4096u, PROT_READ) != 0 ||
        munmap(mapped, 4096u) != 0) {
        return 163;
    }
    child = spawn_ex("/cmd/test32 mprotect-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != -14) {
        return 164;
    }
    if (puts("[test32] libc32 mprotect OK") == EOF) {
        return 165;
    }
    return 0;
}

int test32_mprotect_partial_child(void) {
    volatile char *mapped;

    mapped = (volatile char *)mmap((void *)(uintptr_t)0x51030000u,
                                   12288u,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                   0,
                                   0u);
    if (mapped != (volatile char *)(uintptr_t)0x51030000u) {
        return 166;
    }
    mapped[0] = 'a';
    mapped[8192] = 'c';
    if (mprotect((void *)(uintptr_t)0x51031000u,
                 4096u,
                 PROT_READ) != 0) {
        return 167;
    }
    mapped[0] = 'A';
    mapped[8192] = 'C';
    mapped[4096] = 'B';
    return 168;
}

int test32_mprotect_partial_case(void) {
    struct syscall_vm_info vm_info;
    uint32_t base_regions;
    uint32_t base_pages;
    char *mapped;
    pid_t child;

    if (vm_query(&vm_info) <= 0) {
        return 166;
    }
    base_regions = vm_info.mmap_regions;
    base_pages = vm_info.mmap_pages;
    mapped = (char *)mmap((void *)(uintptr_t)0x51030000u,
                          12288u,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          0,
                          0u);
    if (mapped != (char *)(uintptr_t)0x51030000u ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 1u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 166;
    }
    mapped[0] = 'a';
    mapped[4096] = 'b';
    mapped[8192] = 'c';
    if (mprotect((void *)(uintptr_t)0x51031000u,
                 4096u,
                 PROT_READ) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 3u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 167;
    }
    mapped[0] = 'A';
    mapped[8192] = 'C';
    if (mapped[0] != 'A' ||
        mapped[4096] != 'b' ||
        mapped[8192] != 'C' ||
        mprotect((void *)(uintptr_t)0x51031000u,
                 4096u,
                 PROT_READ | PROT_WRITE) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions + 3u ||
        vm_info.mmap_pages != base_pages + 3u) {
        return 168;
    }
    mapped[4096] = 'B';
    if (mapped[4096] != 'B' ||
        munmap((void *)(uintptr_t)0x51030000u, 4096u) != 0 ||
        munmap((void *)(uintptr_t)0x51031000u, 4096u) != 0 ||
        munmap((void *)(uintptr_t)0x51032000u, 4096u) != 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != base_regions ||
        vm_info.mmap_pages != base_pages) {
        return 169;
    }
    child = spawn_ex("/cmd/test32 mprotect-partial-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != -14) {
        return 170;
    }
    if (puts("[test32] libc32 mprotect partial OK") == EOF) {
        return 171;
    }
    return 0;
}


int test32_shm_lifecycle_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info mid;
    struct syscall_vm_info after;
    void *shared_map;
    int shm;
    pid_t child;
    int status;

    (void)shm_unlink("test32.life");
    if (vm_query(&before) <= 0) {
        return 229;
    }
    shm = shm_open("test32.life", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        return 230;
    }
    shared_map = mmap(0,
                      4096u,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      shm,
                      0u);
    if (shared_map == MAP_FAILED) {
        (void)shm_unlink("test32.life");
        return 231;
    }
    memcpy(shared_map, "life-parent", 12u);
    if (shm_unlink("test32.life") != 0) {
        (void)munmap(shared_map, 4096u);
        return 232;
    }
    if (shm_open("test32.life", 4096u, 0) >= 0) {
        (void)munmap(shared_map, 4096u);
        return 233;
    }
    child = fork();
    if (child < 0) {
        (void)munmap(shared_map, 4096u);
        return 234;
    }
    if (child == 0) {
        if (strcmp((char *)shared_map, "life-parent") != 0) {
            _exit(235);
        }
        memcpy(shared_map, "life-child", 11u);
        _exit(52);
    }
    status = waitpid(child);
    if (status != 52 || strcmp((char *)shared_map, "life-child") != 0) {
        (void)munmap(shared_map, 4096u);
        return status != 52 ? status : 236;
    }
    if (vm_query(&mid) <= 0 ||
        mid.mmap_regions != before.mmap_regions + 1u ||
        mid.mmap_pages != before.mmap_pages + 1u ||
        mid.shared_regions != before.shared_regions + 1u ||
        mid.shm_mapped_pages != before.shm_mapped_pages + 1u ||
        mid.shm_objects != before.shm_objects + 1u) {
        (void)munmap(shared_map, 4096u);
        return 237;
    }
    if (munmap(shared_map, 4096u) != 0) {
        return 238;
    }
    if (vm_query(&after) <= 0 ||
        after.mmap_regions != before.mmap_regions ||
        after.mmap_pages != before.mmap_pages ||
        after.shared_regions != before.shared_regions ||
        after.shm_mapped_pages != before.shm_mapped_pages ||
        after.shm_objects != before.shm_objects) {
        return 239;
    }
    if (puts("[test32] libc32 shm unlink lifecycle OK") == EOF) {
        return 240;
    }
    return 0;
}

int test32_mmap_shm_basic_case(void) {
    struct syscall_vm_info vm_info;
    void *mapped;
    void *shared;
    int shm;

    mapped = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  0,
                  0u);
    if (mapped == MAP_FAILED ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions == 0u ||
        vm_info.mmap_pages == 0u) {
        return 90;
    }
    ((char *)mapped)[0] = 'm';
    ((char *)mapped)[4095] = 'p';
    if (((char *)mapped)[0] != 'm' ||
        ((char *)mapped)[4095] != 'p' ||
        munmap(mapped, 4096u) != 0) {
        return 91;
    }
    if (vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 0u ||
        vm_info.mmap_pages != 0u) {
        return 151;
    }
    mapped = mmap(0,
                  8192u,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  0,
                  0u);
    if (mapped == MAP_FAILED ||
        vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 1u ||
        vm_info.mmap_pages != 2u) {
        return 98;
    }
    ((char *)mapped)[0] = '2';
    ((char *)mapped)[4096] = 'p';
    ((char *)mapped)[8191] = 'g';
    if (((char *)mapped)[0] != '2' ||
        ((char *)mapped)[4096] != 'p' ||
        ((char *)mapped)[8191] != 'g' ||
        munmap(mapped, 8192u) != 0) {
        return 99;
    }
    if (vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 0u ||
        vm_info.mmap_pages != 0u) {
        return 152;
    }
    shm = shm_open("test32.shm", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0 ||
        vm_query(&vm_info) <= 0 ||
        vm_info.shm_objects == 0u ||
        shm_open("test32.shm", 4096u, SHM_CREATE | SHM_EXCL) >= 0) {
        return 92;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED ||
        vm_query(&vm_info) <= 0 ||
        vm_info.shared_regions != 1u ||
        vm_info.shm_mapped_pages != 1u) {
        return 100;
    }
    memcpy(shared, "shared32", 9u);
    if (munmap(shared, 4096u) != 0) {
        return 101;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED ||
        strcmp((char *)shared, "shared32") != 0 ||
        shm_unlink("test32.shm") != 0 ||
        shm_open("test32.shm", 4096u, 0) >= 0 ||
        munmap(shared, 4096u) != 0) {
        return 102;
    }
    if (vm_query(&vm_info) <= 0 ||
        vm_info.mmap_regions != 0u ||
        vm_info.shm_objects != 0u ||
        vm_info.shm_mapped_pages != 0u ||
        vm_info.user_stack_pages == 0u) {
        return 153;
    }
    if (puts("[test32] libc32 mmap/shm syscalls OK") == EOF) {
        return 93;
    }
    return 0;
}

int test32_live_shared_mmap_case(void) {
    void *shared;
    int shm;
    pid_t child;

    shm = shm_open("test32.live", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        return 138;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED) {
        return 139;
    }
    memcpy(shared, "parent-live", 12u);
    child = spawn_ex("/cmd/test32 shm-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        waitpid(child) != 0 ||
        strcmp((char *)shared, "child-live") != 0 ||
        shm_unlink("test32.live") != 0 ||
        munmap(shared, 4096u) != 0) {
        return 140;
    }
    if (puts("[test32] libc32 live shared mmap OK") == EOF) {
        return 145;
    }
    return 0;
}
