#include "test32_proc.h"

int test32_kill_child(void) {
    for (;;) {
        sleep(10u);
    }
    return 0;
}

static int test32_fork_child_case(void) {
    _exit(42);
}

static char test32_cow_probe[4096] __attribute__((aligned(4096)));

int test32_fork_case(void) {
    pid_t child = fork();

    if (child < 0) {
        return 1;
    }
    if (child == 0) {
        test32_fork_child_case();
    }
    if (waitpid(child) != 42) {
        return 2;
    }
    if (puts("[test32] libc32 fork/COW OK") == EOF) {
        return 3;
    }
    return 0;
}

int test32_fork_cow_cleanup_case(void) {
    struct syscall_vm_info vm_before;
    struct syscall_vm_info vm_after;
    char *cow_page;

    if (vm_query(&vm_before) <= 0) {
        return 192;
    }
    cow_page = (char *)mmap((void *)(uintptr_t)0x510a0000u,
                            4096u,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                            0,
                            0u);
    if (cow_page != (char *)(uintptr_t)0x510a0000u) {
        return 198;
    }
    memset(cow_page, 0, 4096u);
    memcpy(cow_page, "parent-cow", 11u);
    for (uint32_t i = 0u; i < 3u; i++) {
        pid_t child = fork();
        int status;

        if (child < 0) {
            (void)munmap(cow_page, 4096u);
            return 193;
        }
        if (child == 0) {
            memcpy(cow_page, "child-cow", 10u);
            _exit(cow_page[0] == 'c' ? 48 : 194);
        }
        status = waitpid(child);
        if (status != 48 || strcmp(cow_page, "parent-cow") != 0) {
            (void)munmap(cow_page, 4096u);
            return 195;
        }
    }
    if (munmap(cow_page, 4096u) != 0) {
        return 199;
    }
    yield();
    if (vm_query(&vm_after) <= 0 ||
        vm_after.mmap_regions != vm_before.mmap_regions ||
        vm_after.mmap_pages != vm_before.mmap_pages ||
        vm_after.shared_regions != vm_before.shared_regions ||
        vm_after.shm_objects != vm_before.shm_objects ||
        vm_after.shm_mapped_pages != vm_before.shm_mapped_pages) {
        printf("[test32] cow cleanup mismatch vm r=%u/%u p=%u/%u sr=%u/%u so=%u/%u sp=%u/%u\n",
               vm_before.mmap_regions,
               vm_after.mmap_regions,
               vm_before.mmap_pages,
               vm_after.mmap_pages,
               vm_before.shared_regions,
               vm_after.shared_regions,
               vm_before.shm_objects,
               vm_after.shm_objects,
               vm_before.shm_mapped_pages,
               vm_after.shm_mapped_pages);
        return 196;
    }
    if (puts("[test32] libc32 fork/COW cleanup OK") == EOF) {
        return 197;
    }
    return 0;
}

int test32_cow_parent_exit_child(void) {
    int shm;
    void *shared;
    char *cow_page;
    pid_t child;

    shm = shm_open("test32.cowowner", 4096u, 0);
    if (shm <= 0) {
        return 248;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED) {
        return 249;
    }
    cow_page = (char *)mmap((void *)(uintptr_t)0x510b0000u,
                            4096u,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                            0,
                            0u);
    if (cow_page != (char *)(uintptr_t)0x510b0000u) {
        return 268;
    }
    memset(cow_page, 0, 4096u);
    memcpy(cow_page, "owner-base", 11u);
    child = fork();
    if (child < 0) {
        return 250;
    }
    if (child == 0) {
        sleep(3u);
        if (strcmp(cow_page, "owner-base") != 0) {
            _exit(251);
        }
        memcpy(cow_page, "owner-child", 12u);
        memcpy(shared, "child-survived", 15u);
        _exit(62);
    }
    memcpy(cow_page, "owner-parent", 13u);
    _exit(61);
}

int test32_fork_cow_ownership_case(void) {
    struct syscall_vm_info vm_before;
    struct syscall_vm_info vm_after;
    char *cow_page;
    pid_t child;
    int status;

    if (vm_query(&vm_before) <= 0) {
        return 252;
    }
    cow_page = (char *)mmap((void *)(uintptr_t)0x510c0000u,
                            4096u,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                            0,
                            0u);
    if (cow_page != (char *)(uintptr_t)0x510c0000u) {
        return 269;
    }
    memset(cow_page, 0, 4096u);
    memcpy(cow_page, "cow-parent", 11u);

    child = fork();
    if (child < 0) {
        (void)munmap(cow_page, 4096u);
        return 253;
    }
    if (child == 0) {
        if (strcmp(cow_page, "cow-parent") != 0) {
            _exit(254);
        }
        memcpy(cow_page, "cow-child", 10u);
        _exit(strcmp(cow_page, "cow-child") == 0 ? 63 : 255);
    }
    memcpy(cow_page, "cow-parent2", 12u);
    status = waitpid(child);
    if (status != 63 || strcmp(cow_page, "cow-parent2") != 0) {
        (void)munmap(cow_page, 4096u);
        return status != 63 ? status : 256;
    }

    memset(test32_cow_probe, 0, sizeof(test32_cow_probe));
    memset(cow_page, 0, 4096u);
    memcpy(cow_page, "multi-parent", 13u);
    for (uint32_t i = 0u; i < 3u; i++) {
        child = fork();
        if (child < 0) {
            (void)munmap(cow_page, 4096u);
            return 262;
        }
        if (child == 0) {
            char expected[16];

            if (strcmp(cow_page, "multi-parent") != 0) {
                _exit(263);
            }
            snprintf(expected, sizeof(expected), "child-%u", i);
            memcpy(cow_page, expected, strlen(expected) + 1u);
            _exit(strcmp(cow_page, expected) == 0 ? 64 : 264);
        }
        status = waitpid(child);
        if (status != 64 || strcmp(cow_page, "multi-parent") != 0) {
            (void)munmap(cow_page, 4096u);
            return status != 64 ? status : 265;
        }
    }
    if (munmap(cow_page, 4096u) != 0) {
        return 270;
    }
    yield();
    if (vm_query(&vm_after) <= 0 ||
        vm_after.mmap_regions != vm_before.mmap_regions ||
        vm_after.mmap_pages != vm_before.mmap_pages ||
        vm_after.shared_regions != vm_before.shared_regions ||
        vm_after.shm_objects != vm_before.shm_objects ||
        vm_after.shm_mapped_pages != vm_before.shm_mapped_pages) {
        return 266;
    }
    if (puts("[test32] libc32 fork/COW ownership OK") == EOF) {
        return 267;
    }
    return 0;
}

int test32_fork_shared_mmap_case(void) {
    int shm = shm_open("test32.forkshare", 4096u, SHM_CREATE | SHM_EXCL);
    void *shared;
    pid_t child;
    int status;

    if (shm <= 0) {
        return 180;
    }
    shared = mmap(0,
                  4096u,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm,
                  0u);
    if (shared == MAP_FAILED) {
        (void)shm_unlink("test32.forkshare");
        return 181;
    }
    memcpy(shared, "parent-fork", 12u);
    child = fork();
    if (child < 0) {
        (void)munmap(shared, 4096u);
        (void)shm_unlink("test32.forkshare");
        return 182;
    }
    if (child == 0) {
        if (strcmp((char *)shared, "parent-fork") != 0) {
            _exit(183);
        }
        memcpy(shared, "child-fork", 11u);
        _exit(45);
    }
    status = waitpid(child);
    if (status != 45 ||
        strcmp((char *)shared, "child-fork") != 0 ||
        munmap(shared, 4096u) != 0 ||
        shm_unlink("test32.forkshare") != 0) {
        return 184;
    }
    if (puts("[test32] libc32 fork shared mmap OK") == EOF) {
        return 185;
    }
    return 0;
}

int test32_fork_mapping_table_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info child_info;
    struct syscall_vm_info after;
    void *private_map;
    void *shared_map;
    int shm;
    pid_t child;
    int status;

    (void)shm_unlink("test32.forkmap");
    private_map = mmap((void *)(uintptr_t)0x51050000u,
                       8192u,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                       0,
                       0u);
    if (private_map != (void *)(uintptr_t)0x51050000u) {
        return 198;
    }
    shm = shm_open("test32.forkmap", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        (void)munmap(private_map, 8192u);
        return 199;
    }
    shared_map = mmap(0,
                      4096u,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      shm,
                      0u);
    if (shared_map == MAP_FAILED) {
        (void)munmap(private_map, 8192u);
        (void)shm_unlink("test32.forkmap");
        return 200;
    }
    ((char *)private_map)[0] = 'p';
    ((char *)private_map)[4096] = 'q';
    memcpy(shared_map, "map-parent", 11u);
    if (vm_query(&before) <= 0 ||
        before.mmap_regions < 2u ||
        before.mmap_pages < 3u ||
        before.shared_regions < 1u ||
        before.shm_mapped_pages < 1u) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkmap");
        return 201;
    }

    child = fork();
    if (child < 0) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkmap");
        return 202;
    }
    if (child == 0) {
        if (vm_query(&child_info) <= 0 ||
            child_info.mmap_regions != before.mmap_regions ||
            child_info.mmap_pages != before.mmap_pages ||
            child_info.shared_regions != before.shared_regions ||
            child_info.shm_mapped_pages != before.shm_mapped_pages ||
            ((char *)private_map)[0] != 'p' ||
            ((char *)private_map)[4096] != 'q' ||
            strcmp((char *)shared_map, "map-parent") != 0) {
            _exit(203);
        }
        ((char *)private_map)[0] = 'c';
        memcpy(shared_map, "map-child", 10u);
        if (munmap((void *)(uintptr_t)0x51051000u, 4096u) != 0 ||
            vm_query(&child_info) <= 0 ||
            child_info.mmap_regions != before.mmap_regions ||
            child_info.mmap_pages != before.mmap_pages - 1u ||
            child_info.shm_mapped_pages != before.shm_mapped_pages) {
            _exit(204);
        }
        _exit(49);
    }

    status = waitpid(child);
    if (status != 49) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkmap");
        return status;
    }
    if (((char *)private_map)[0] != 'p' ||
        ((char *)private_map)[4096] != 'q') {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkmap");
        return 208;
    }
    if (strcmp((char *)shared_map, "map-child") != 0) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkmap");
        return 209;
    }
    if (vm_query(&after) <= 0) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkmap");
        return 210;
    }
    if (after.mmap_regions != before.mmap_regions ||
        after.mmap_pages != before.mmap_pages ||
        after.shared_regions != before.shared_regions ||
        after.shm_mapped_pages != before.shm_mapped_pages) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkmap");
        return 211;
    }
    if (munmap(private_map, 8192u) != 0 ||
        munmap(shared_map, 4096u) != 0 ||
        shm_unlink("test32.forkmap") != 0) {
        return 206;
    }
    if (puts("[test32] libc32 fork mapping table OK") == EOF) {
        return 207;
    }
    return 0;
}

int test32_exec_target_case(int argc, char **argv) {
    struct syscall_vm_info vm_info;

    if (argc < 3 || strcmp(argv[2], "wait-exec") != 0) {
        if (argc >= 3 && strcmp(argv[2], "mmap-exec") == 0) {
            if (vm_query(&vm_info) <= 0 ||
                vm_info.mmap_regions != 0u ||
                vm_info.mmap_pages != 0u ||
                vm_info.shared_regions != 0u ||
                vm_info.shm_mapped_pages != 0u) {
                return 212;
            }
            return 50;
        }
        return 186;
    }
    return 47;
}

int test32_fork_wait_exec_case(void) {
    pid_t child = fork();
    int status;

    if (child < 0) {
        return 187;
    }
    if (child == 0) {
        if (exec_replace("/cmd/test32 exec-target wait-exec") != 0) {
            _exit(188);
        }
        _exit(189);
    }
    status = waitpid(child);
    if (status != 47) {
        return 190;
    }
    if (puts("[test32] libc32 fork/wait/exec OK") == EOF) {
        return 191;
    }
    return 0;
}

int test32_fork_mmap_exec_case(void) {
    struct syscall_vm_info parent_before;
    struct syscall_vm_info child_info;
    void *private_map;
    void *shared_map;
    int shm;
    pid_t child;
    int status;

    (void)shm_unlink("test32.forkexec");
    if (vm_query(&parent_before) <= 0) {
        return 213;
    }
    private_map = mmap((void *)(uintptr_t)0x51060000u,
                       8192u,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                       0,
                       0u);
    if (private_map != (void *)(uintptr_t)0x51060000u) {
        return 214;
    }
    shm = shm_open("test32.forkexec", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        (void)munmap(private_map, 8192u);
        return 215;
    }
    shared_map = mmap(0,
                      4096u,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      shm,
                      0u);
    if (shared_map == MAP_FAILED) {
        (void)munmap(private_map, 8192u);
        (void)shm_unlink("test32.forkexec");
        return 216;
    }
    ((char *)private_map)[0] = 'p';
    ((char *)private_map)[4096] = 'q';
    memcpy(shared_map, "exec-parent", 12u);
    child = fork();
    if (child < 0) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkexec");
        return 217;
    }
    if (child == 0) {
        if (vm_query(&child_info) <= 0 ||
            child_info.mmap_regions < parent_before.mmap_regions + 2u ||
            child_info.mmap_pages < parent_before.mmap_pages + 3u ||
            strcmp((char *)shared_map, "exec-parent") != 0) {
            _exit(218);
        }
        ((char *)private_map)[0] = 'x';
        if (exec_replace("/cmd/test32 exec-target mmap-exec") != 0) {
            _exit(219);
        }
        _exit(220);
    }
    status = waitpid(child);
    if (status != 50 ||
        ((char *)private_map)[0] != 'p' ||
        ((char *)private_map)[4096] != 'q' ||
        strcmp((char *)shared_map, "exec-parent") != 0) {
        (void)munmap(private_map, 8192u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.forkexec");
        return status != 50 ? status : 221;
    }
    if (munmap(private_map, 8192u) != 0 ||
        munmap(shared_map, 4096u) != 0 ||
        shm_unlink("test32.forkexec") != 0) {
        return 222;
    }
    if (vm_query(&child_info) <= 0) {
        return 226;
    }
    if (child_info.mmap_regions != parent_before.mmap_regions ||
        child_info.mmap_pages != parent_before.mmap_pages) {
        return 227;
    }
    if (child_info.shared_regions != parent_before.shared_regions ||
        child_info.shm_mapped_pages != parent_before.shm_mapped_pages) {
        return 228;
    }
    if (puts("[test32] libc32 fork/mmap/exec cleanup OK") == EOF) {
        return 224;
    }
    return 0;
}

int test32_exec_fail_cleanup_case(void) {
    struct syscall_vm_info before;
    struct syscall_vm_info after;
    void *private_map;
    void *shared_map;
    int shm;

    (void)shm_unlink("test32.execfail");
    if (vm_query(&before) <= 0) {
        return 239;
    }
    private_map = mmap((void *)(uintptr_t)0x51070000u,
                       4096u,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                       0,
                       0u);
    if (private_map != (void *)(uintptr_t)0x51070000u) {
        return 240;
    }
    shm = shm_open("test32.execfail", 4096u, SHM_CREATE | SHM_EXCL);
    if (shm <= 0) {
        (void)munmap(private_map, 4096u);
        return 241;
    }
    shared_map = mmap(0,
                      4096u,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      shm,
                      0u);
    if (shared_map == MAP_FAILED) {
        (void)munmap(private_map, 4096u);
        (void)shm_unlink("test32.execfail");
        return 242;
    }
    memcpy(private_map, "exec-fail-private", 18u);
    memcpy(shared_map, "exec-fail-shared", 17u);
    if (exec_replace("/cmd/NO_SUCH_EXEC32.ELF") == 0) {
        (void)munmap(private_map, 4096u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.execfail");
        return 243;
    }
    if (strcmp((char *)private_map, "exec-fail-private") != 0 ||
        strcmp((char *)shared_map, "exec-fail-shared") != 0) {
        (void)munmap(private_map, 4096u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.execfail");
        return 244;
    }
    if (vm_query(&after) <= 0 ||
        after.mmap_regions != before.mmap_regions + 2u ||
        after.mmap_pages != before.mmap_pages + 2u ||
        after.shared_regions != before.shared_regions + 1u ||
        after.shm_mapped_pages != before.shm_mapped_pages + 1u) {
        (void)munmap(private_map, 4096u);
        (void)munmap(shared_map, 4096u);
        (void)shm_unlink("test32.execfail");
        return 245;
    }
    if (munmap(private_map, 4096u) != 0 ||
        munmap(shared_map, 4096u) != 0 ||
        shm_unlink("test32.execfail") != 0) {
        return 246;
    }
    if (puts("[test32] libc32 exec failure cleanup OK") == EOF) {
        return 247;
    }
    return 0;
}

int test32_proc_query_case(pid_t pid) {
    struct syscall_process_info proc_info;

    memset(&proc_info, 0, sizeof(proc_info));
    for (uint32_t i = 0u; i < SYS_PROC_SLOTS_MAX; i++) {
        struct syscall_process_info candidate;

        if (proc_query(SYS_PROC_QUERY_ALL, i, &candidate) > 0 &&
            candidate.pid == (uint32_t)pid) {
            proc_info = candidate;
            break;
        }
    }
    if (proc_info.pid != (uint32_t)pid) {
        return 53;
    }
    if (proc_info.state != SYS_PROC_STATE_RUNNING) {
        return 55;
    }
    if (proc_info.image_kind != SYS_PROC_IMAGE_ELF) {
        return 56;
    }
    if (puts("[test32] libc32 proc_query syscall OK") == EOF) {
        return 54;
    }
    return 0;
}

int test32_proc_getpid_write_puts_case(pid_t pid, const void *ptr) {
    char target[16];

    if (printf("[test32] printf pid=%u hex=%08x ptr=%p\n",
               (uint32_t)pid,
               0x386u,
               ptr) < 0) {
        return 23;
    }
    if (snprintf(target,
                 sizeof(target),
                 "%s-%d",
                 "i386",
                 -32) != 8 ||
        strcmp(target, "i386--32") != 0) {
        return 24;
    }
    if (puts("[test32] libc32 getpid/write/puts OK") == EOF) {
        return 13;
    }
    return 0;
}

int test32_proc_kill_wait_case(void) {
    pid_t child;

    child = spawn_ex("/cmd/test32 kill-child", SYS_SPAWN_ELF, 0u);
    if (child <= 0 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 58;
    }
    if (puts("[test32] libc32 kill/wait syscall OK") == EOF) {
        return 59;
    }

    {
        struct syscall_process_info wait_info;

        memset(&wait_info, 0, sizeof(wait_info));
        child = spawn_ex("/cmd/test32 kill-child",
                         SYS_SPAWN_ELF,
                         SYS_SPAWN_BACKGROUND);
        if (child <= 0 ||
            kill(child) != 1 ||
            wait((uint32_t)child, &wait_info) != 1 ||
            wait_info.pid != (uint32_t)child ||
            wait_info.exit_code != -9 ||
            wait_info.state != SYS_PROC_STATE_EXITED) {
            return 156;
        }
        memset(&wait_info, 0, sizeof(wait_info));
        child = spawn_ex("/cmd/test32 kill-child",
                         SYS_SPAWN_ELF,
                         SYS_SPAWN_BACKGROUND);
        if (child <= 0 ||
            kill(child) != 1 ||
            wait(SYS_WAIT_LAST_PID, &wait_info) != 1 ||
            wait_info.pid != (uint32_t)child ||
            wait_info.exit_code != -9 ||
            wait_info.state != SYS_PROC_STATE_EXITED) {
            return 157;
        }
        if (puts("[test32] libc32 wait info syscall OK") == EOF) {
            return 158;
        }
    }
    return 0;
}

int test32_proc_spawn_background_case(void) {
    pid_t child;

    child = spawn_ex("/cmd/test32 kill-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 60;
    }
    if (puts("[test32] libc32 background spawn syscall OK") == EOF) {
        return 61;
    }

    child = spawn_ex("./cmd/test32 'kill-child'",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 136;
    }
    if (puts("[test32] libc32 relative/quoted spawn OK") == EOF) {
        return 137;
    }
    return 0;
}

int test32_proc_fork_surface_case(void) {
    pid_t child;

    if (test32_fork_case() != 0) {
        return 98;
    }
    child = spawn_ex("/cmd/test32 kill-child",
                     SYS_SPAWN_ELF,
                     SYS_SPAWN_BACKGROUND);
    if (child <= 0 ||
        fg((uint32_t)child) != 1 ||
        bg((uint32_t)child) != 1 ||
        kill(child) != 1 ||
        waitpid(child) != -9) {
        return 99;
    }
    if (puts("[test32] libc32 fork/fg/bg syscall surface OK") == EOF) {
        return 100;
    }
    return 0;
}
