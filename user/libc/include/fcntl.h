#pragma once

#include "user/public/sysapi.h"

#define O_CREAT SYS_OPEN_CREAT
#define O_TRUNC SYS_OPEN_TRUNC
#define O_APPEND SYS_OPEN_APPEND

int open(const char *path, int flags);
