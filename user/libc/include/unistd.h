#pragma once

#include "sys/types.h"
#include "user/public/sysapi.h"

#define STDIN_FILENO SYS_FD_STDIN
#define STDOUT_FILENO SYS_FD_STDOUT
#define STDERR_FILENO SYS_FD_STDERR

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);
int chdir(const char *path);
int getcwd(char *buffer, uint32_t size);
pid_t getpid(void);
