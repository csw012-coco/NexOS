#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "abi/syscall_abi.h"

extern uint32_t __nlibc32_syscall4(uint32_t number,
                                   uint32_t arg0,
                                   uint32_t arg1,
                                   uint32_t arg2,
                                   uint32_t arg3);

ssize_t write(int fd, const void *buffer, size_t size) {
    return (ssize_t)__nlibc32_syscall4(SYS_WRITE,
                                       (uint32_t)fd,
                                       (uint32_t)(uintptr_t)buffer,
                                       (uint32_t)size,
                                       0u);
}

int open(const char *path, int flags) {
    return (int)__nlibc32_syscall4(SYS_OPEN,
                                   (uint32_t)(uintptr_t)path,
                                   (uint32_t)flags,
                                   0u,
                                   0u);
}

ssize_t read(int fd, void *buffer, size_t size) {
    return (ssize_t)__nlibc32_syscall4(SYS_READ,
                                       (uint32_t)fd,
                                       (uint32_t)(uintptr_t)buffer,
                                       (uint32_t)size,
                                       SYS_READ_BLOCKING);
}

int close(int fd) {
    return (int)__nlibc32_syscall4(SYS_CLOSE,
                                   (uint32_t)fd,
                                   0u,
                                   0u,
                                   0u);
}

pid_t getpid(void) {
    return (pid_t)__nlibc32_syscall4(SYS_GETPID, 0u, 0u, 0u, 0u);
}

uint32_t ticks(void) {
    return __nlibc32_syscall4(SYS_TICKS, 0u, 0u, 0u, 0u);
}

void yield(void) {
    (void)__nlibc32_syscall4(SYS_YIELD, 0u, 0u, 0u, 0u);
}

void _exit(int status) {
    (void)__nlibc32_syscall4(SYS_EXIT, (uint32_t)status, 0u, 0u, 0u);
    for (;;) {
        __asm__ volatile("ud2");
    }
}

void exit(int status) {
    _exit(status);
}
