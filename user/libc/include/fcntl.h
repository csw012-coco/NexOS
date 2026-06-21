#pragma once

#include "user/public/sysapi.h"

#define O_CREAT SYS_OPEN_CREAT
#define O_TRUNC SYS_OPEN_TRUNC
#define O_APPEND SYS_OPEN_APPEND
#define O_RDONLY SYS_OPEN_READ
#define O_WRONLY SYS_OPEN_WRITE
#define O_RDWR (SYS_OPEN_READ | SYS_OPEN_WRITE)

int open(const char *path, int flags);
