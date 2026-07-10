#pragma once

#include <stddef.h>
#include <stdint.h>

#include "abi/syscall_abi.h"

#define PROT_NONE 0
#define PROT_READ SYS_PROT_READ
#define PROT_WRITE SYS_PROT_WRITE
#define PROT_EXEC SYS_PROT_EXEC

#define MAP_PRIVATE SYS_MAP_PRIVATE
#define MAP_SHARED SYS_MAP_SHARED
#define MAP_ANONYMOUS SYS_MAP_ANONYMOUS
#define MAP_FIXED SYS_MAP_FIXED
#define MAP_FAILED ((void *)-1)

#define SHM_CREATE SYS_SHM_CREATE
#define SHM_EXCL SYS_SHM_EXCL

void *mmap(void *addr,
           size_t length,
           int prot,
           int flags,
           int shm_handle,
           uint64_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t length, int prot);
int shm_open(const char *name, size_t size, int flags);
int shm_unlink(const char *name);
