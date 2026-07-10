#pragma once

#include <stdint.h>

#include "abi/syscall_abi.h"
#include "fcntl.h"
#include "unistd.h"

#define NEX_READ_BLOCKING SYS_READ_BLOCKING
#define NEX_READ_NONBLOCK SYS_READ_NONBLOCK
#define NEX_READ_CHAR SYS_READ_CHAR

#ifndef NEXOS_FILE_IO_WOULD_BLOCK
#define NEXOS_FILE_IO_WOULD_BLOCK (-2)
#endif

#ifndef NEXOS_FILE_IO_BROKEN_PIPE
#define NEXOS_FILE_IO_BROKEN_PIPE (-3)
#endif

ssize_t write_stdout(const void *buffer, size_t size);
ssize_t write_stderr(const void *buffer, size_t size);
ssize_t nex_read(int fd, void *buffer, size_t size, uint32_t flags);
uint32_t read_char_nonblock(char *ch);
uint32_t read_line(uint32_t fd, char *buffer, uint32_t size);
int opendir(const char *path);
int readdir(uint32_t fd, struct syscall_dirent *entry);
