#include <stdint.h>
#include <stddef.h>
#include "user/libc/include/sys/mman.h"
#include "user/public/sysapi.h"

static long sys_write(int fd, const void *buf, size_t len) {
    register long rax __asm__("rax") = SYS_WRITE;
    register long rbx __asm__("rbx") = fd;
    register long rcx __asm__("rcx") = (long)buf;
    register long rdx __asm__("rdx") = len;

    __asm__ volatile (
        "int $0x40"
        : "+a"(rax)
        : "b"(rbx), "c"(rcx), "d"(rdx)
        : "memory"
    );

    return rax;
}

int main() {
    char *page = mmap(NULL,
                      NOS_PAGE_SIZE * 2u,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1,
                      0);

    if (page == MAP_FAILED) {
        return 1;
    }
    if (munmap(page + NOS_PAGE_SIZE, NOS_PAGE_SIZE) != 0) {
        return 2;
    }
    for (int i = 0; i < 4096; i++) {
        page[i] = 'B';
    }

    page[4095] = '\n';

    /* Exactly through the last mapped byte: succeeds. */
    if (sys_write(1, page + 4088, 8) != 8) {
        return 3;
    }

    /* Includes one byte from the unmapped guard page: rejected. */
    return (int)sys_write(1, page + 4088, 9);
}
