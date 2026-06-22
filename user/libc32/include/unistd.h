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
int dup2(int old_fd, int new_fd);
int pipe(int pair[2]);
int chdir(const char *path);
int getcwd(char *buffer, size_t size);
pid_t getpid(void);
pid_t spawn(const char *command);
int exec(const char *command);
int waitpid(pid_t pid);
uint32_t ticks(void);
void yield(void);
void *page_alloc(void);
int page_free(void *page);
__attribute__((noreturn)) void _exit(int status);
