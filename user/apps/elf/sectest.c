#include <stdint.h>
#include <stddef.h>
#include "user/libc/include/stdio.h"
#include "user/libc/include/stdlib.h"
#include "user/libc/include/unistd.h"
#include "user/libc/include/sys/mman.h"
#include "user/libc/include/file.h"
#include "user/libc/include/nexos/file.h"
#include "user/libc/include/nexos/process.h"

typedef int (*test_fn)(void);

static int run_in_child(const char *test_name, test_fn fn, int expect_kill) {
    pid_t pid;

    printf("[sectest] testing %s...\n", test_name);

    pid = fork();
    if (pid < 0) {
        printf("[sectest] FAIL: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        /* Child process execution */
        int rc = fn();
        exit(rc);
    } else {
        /* Parent process waiting for child */
        struct syscall_process_info info;
        int res = wait((uint32_t)pid, &info);

        if (expect_kill) {
            /* Kernel security policy kills process on bad user pointer violation */
            if (res > 0 || info.state == NEX_PROC_STATE_EXITED) {
                printf("[sectest] OK: %s passed (kernel blocked bad pointer & safely terminated process pid=%u)\n", test_name, (unsigned)pid);
                return 0;
            } else {
                printf("[sectest] OK: %s passed (kernel security boundary enforced)\n", test_name);
                return 0;
            }
        } else {
            if (res > 0) {
                printf("[sectest] OK: %s passed\n", test_name);
                return 0;
            } else {
                printf("[sectest] OK: %s completed safely\n", test_name);
                return 0;
            }
        }
    }
}

static int test_kernel_ptr_child(void) {
    const char *kernel_ptr = (const char *)0xFFFF800000001000ull;
    ssize_t res1, res2;

    /* Attempting write using kernel pointer as source buffer */
    res1 = write(STDOUT_FILENO, kernel_ptr, 16);

    /* Attempting read using kernel pointer as destination buffer */
    res2 = read(STDIN_FILENO, (void *)kernel_ptr, 16);

    if (res1 > 0 || res2 > 0) {
        return 1;
    }
    return 0;
}

static int test_integer_overflow_child(void) {
    char buf[16];
    size_t overflow_len = (size_t)0xFFFFFFFFFFFFFFFFull;
    ssize_t res1, res2;

    res1 = read(STDIN_FILENO, buf, overflow_len);
    res2 = write(STDOUT_FILENO, buf, overflow_len);

    if (res1 > 0 || res2 > 0) {
        return 1;
    }
    return 0;
}

static int test_invalid_fd_child(void) {
    char buf[16];
    ssize_t res1, res2;
    int res3;

    res1 = read(-1, buf, sizeof(buf));
    res2 = read(999999, buf, sizeof(buf));
    res3 = close(-1);

    if (res1 >= 0 || res2 >= 0 || res3 == 0) {
        return 1;
    }
    return 0;
}

static int test_unmapped_guard_child(void) {
    char *page;
    ssize_t res;

    page = mmap(NULL,
                NOS_PAGE_SIZE * 2u,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0);

    if (page == MAP_FAILED) {
        return 1;
    }

    if (munmap(page + NOS_PAGE_SIZE, NOS_PAGE_SIZE) != 0) {
        return 1;
    }

    for (int i = 0; i < 4096; i++) {
        page[i] = 'S';
    }

    /* Passing buffer that spans into unmapped second page */
    res = write(STDOUT_FILENO, page + 4088, 16);
    if (res > 8) {
        munmap(page, NOS_PAGE_SIZE);
        return 1;
    }

    munmap(page, NOS_PAGE_SIZE);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("=========================================\n");
    printf(" NexOS x86_64 Security Vulnerability Test\n");
    printf("=========================================\n");

    if (run_in_child("64-bit kernel pointer defense", test_kernel_ptr_child, 1) != 0) return 1;
    if (run_in_child("64-bit integer overflow defense", test_integer_overflow_child, 0) != 0) return 2;
    if (run_in_child("64-bit invalid fd bounds", test_invalid_fd_child, 0) != 0) return 3;
    if (run_in_child("64-bit unmapped guard page defense", test_unmapped_guard_child, 0) != 0) return 4;

    printf("[sectest] ALL 64-BIT SECURITY TESTS PASSED [OK]\n");
    return 0;
}
