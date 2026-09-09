#include "test32_sec.h"

int test32_sec_kernel_ptr_case(void) {
    /* Try passing kernel space pointers (0xC0000000+ on i386) to write/read */
    void *kernel_ptr = (void *)0xC0001000u;
    int res1, res2;

    /* Write attempt reading from kernel memory */
    res1 = write(1, (const char *)kernel_ptr, 16);

    /* Read attempt writing into kernel memory */
    res2 = read(0, kernel_ptr, 16);

    /* Both must fail (return <= 0 or negative error code) */
    if (res1 > 0 || res2 > 0) {
        return 301;
    }

    if (puts("[test32] security kernel_ptr_case OK") == EOF) {
        return 302;
    }
    return 0;
}

int test32_sec_integer_overflow_case(void) {
    char buf[16];
    int res1, res2;

    /* Extremely large size that wraps around 32-bit addition */
    size_t overflow_size = (size_t)0xFFFFFFFFu;

    res1 = read(0, buf, overflow_size);
    res2 = write(1, buf, overflow_size);

    if (res1 > 0 || res2 > 0) {
        return 303;
    }

    if (puts("[test32] security integer_overflow_case OK") == EOF) {
        return 304;
    }
    return 0;
}

int test32_sec_invalid_fd_case(void) {
    char buf[16];
    int res1, res2, res3;

    res1 = read(-1, buf, sizeof(buf));
    res2 = read(9999, buf, sizeof(buf));
    res3 = close(-1);

    if (res1 >= 0 || res2 >= 0 || res3 == 0) {
        return 305;
    }

    if (puts("[test32] security invalid_fd_case OK") == EOF) {
        return 306;
    }
    return 0;
}

int test32_sec_unmapped_copy_case(void) {
    /* Address in user range but unmapped */
    void *unmapped_ptr = (void *)0x60000000u;
    int res;

    res = read(0, unmapped_ptr, 16);
    if (res > 0) {
        return 307;
    }

    if (puts("[test32] security unmapped_copy_case OK") == EOF) {
        return 308;
    }
    return 0;
}

int test32_sec_suite_case(void) {
    int rc;

    if ((rc = test32_sec_kernel_ptr_case()) != 0) {
        return rc;
    }
    if ((rc = test32_sec_integer_overflow_case()) != 0) {
        return rc;
    }
    if ((rc = test32_sec_invalid_fd_case()) != 0) {
        return rc;
    }
    if ((rc = test32_sec_unmapped_copy_case()) != 0) {
        return rc;
    }

    if (puts("[test32] security vulnerability suite PASS") == EOF) {
        return 309;
    }
    return 0;
}
