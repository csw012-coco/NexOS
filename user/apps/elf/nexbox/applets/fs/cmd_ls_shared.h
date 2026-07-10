#pragma once

#ifdef __i386__
#include "user/libc32/include/file.h"
#include "user/libc32/include/stdio.h"
#else
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/nexos/file.h"
#endif

int cmd_ls_path(const char *path, int long_format, int show_all);
