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
    ssize_t result;

    do {
        result = (ssize_t)__nlibc32_syscall4(
            SYS_WRITE,
            (uint32_t)fd,
            (uint32_t)(uintptr_t)buffer,
            (uint32_t)size,
            0u);
        if (result == -2) {
            yield();
        }
    } while (result == -2);
    return result;
}

int open(const char *path, int flags) {
    return (int)__nlibc32_syscall4(SYS_OPEN,
                                   (uint32_t)(uintptr_t)path,
                                   (uint32_t)flags,
                                   0u,
                                   0u);
}

ssize_t read(int fd, void *buffer, size_t size) {
    ssize_t result;

    do {
        result = (ssize_t)__nlibc32_syscall4(
            SYS_READ,
            (uint32_t)fd,
            (uint32_t)(uintptr_t)buffer,
            (uint32_t)size,
            SYS_READ_BLOCKING);
        if (result == -2) {
            yield();
        }
    } while (result == -2);
    return result;
}

int close(int fd) {
    return (int)__nlibc32_syscall4(SYS_CLOSE,
                                   (uint32_t)fd,
                                   0u,
                                   0u,
                                   0u);
}

int dup2(int old_fd, int new_fd) {
    return (int)__nlibc32_syscall4(SYS_DUP2,
                                   (uint32_t)old_fd,
                                   (uint32_t)new_fd,
                                   0u,
                                   0u);
}

int pipe(int pair[2]) {
    return (int)__nlibc32_syscall4(SYS_PIPE,
                                   (uint32_t)(uintptr_t)pair,
                                   0u,
                                   0u,
                                   0u);
}

int chdir(const char *path) {
    return (int)__nlibc32_syscall4(SYS_CHDIR,
                                   (uint32_t)(uintptr_t)path,
                                   0u,
                                   0u,
                                   0u);
}

int getcwd(char *buffer, size_t size) {
    return (int)__nlibc32_syscall4(SYS_GETCWD,
                                   (uint32_t)(uintptr_t)buffer,
                                   (uint32_t)size,
                                   0u,
                                   0u);
}

int opendir(const char *path) {
    return (int)__nlibc32_syscall4(SYS_OPENDIR,
                                   (uint32_t)(uintptr_t)path,
                                   0u,
                                   0u,
                                   0u);
}

int readdir(uint32_t fd, struct syscall_dirent *entry) {
    return (int)__nlibc32_syscall4(SYS_READDIR,
                                   fd,
                                   (uint32_t)(uintptr_t)entry,
                                   0u,
                                   0u);
}

pid_t getpid(void) {
    return (pid_t)__nlibc32_syscall4(SYS_GETPID, 0u, 0u, 0u, 0u);
}

pid_t spawn(const char *command) {
    return (pid_t)__nlibc32_syscall4(SYS_SPAWN,
                                     (uint32_t)(uintptr_t)command,
                                     SYS_SPAWN_ELF,
                                     0u,
                                     0u);
}

int exec(const char *command) {
    return (int)__nlibc32_syscall4(SYS_EXEC,
                                   (uint32_t)(uintptr_t)command,
                                   0u,
                                   0u,
                                   0u);
}

int waitpid(pid_t pid) {
    return (int)__nlibc32_syscall4(SYS_WAIT,
                                   (uint32_t)pid,
                                   0u,
                                   0u,
                                   0u);
}

uint32_t ticks(void) {
    return __nlibc32_syscall4(SYS_TICKS, 0u, 0u, 0u, 0u);
}

void yield(void) {
    (void)__nlibc32_syscall4(SYS_YIELD, 0u, 0u, 0u, 0u);
}

void *page_alloc(void) {
    return (void *)(uintptr_t)__nlibc32_syscall4(SYS_PAGE_ALLOC,
                                                 0u,
                                                 0u,
                                                 0u,
                                                 0u);
}

int page_free(void *page) {
    return (int)__nlibc32_syscall4(SYS_PAGE_FREE,
                                   (uint32_t)(uintptr_t)page,
                                   0u,
                                   0u,
                                   0u);
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
