#pragma once

#include <stdint.h>

#include "abi/syscall_abi.h"

int opendir(const char *path);
int readdir(uint32_t fd, struct syscall_dirent *entry);
