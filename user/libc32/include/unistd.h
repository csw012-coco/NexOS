#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum {
    STDIN_FILENO = 0,
    STDOUT_FILENO = 1,
    STDERR_FILENO = 2
};

ssize_t write(int fd, const void *buffer, size_t size);
ssize_t read(int fd, void *buffer, size_t size);
int close(int fd);
pid_t getpid(void);
uint32_t ticks(void);
void yield(void);
__attribute__((noreturn)) void _exit(int status);
