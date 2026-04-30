#pragma once

#include "fcntl.h"
#include "unistd.h"

ssize_t write_stdout(const void *buf, size_t count);
ssize_t write_stderr(const void *buf, size_t count);
ssize_t nex_read(int fd, void *buf, size_t count, uint32_t flags);
uint32_t read_char_nonblock(char *ch);
uint32_t read_line(uint32_t fd, char *buf, uint32_t size);
int opendir(const char *path);
int readdir(uint32_t fd, struct syscall_dirent *entry);
int mkdir(const char *path);
int rmdir(const char *path);
int remove(const char *path);
