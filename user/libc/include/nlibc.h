#pragma once

#include "fcntl.h"
#include "file.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "strings.h"
#include "nexos/audio.h"
#include "nexos/file.h"
#include "nexos/fs.h"
#include "nexos/net.h"
#include "nexos/process.h"
#include "nexos/string.h"
#include "nexos/system.h"
#include "user/public/sysapi.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

uint64_t syscall4(uint64_t number,
                  uint64_t arg0,
                  uint64_t arg1,
                  uint64_t arg2,
                  uint64_t arg3);

int veprintf(const char *fmt, va_list ap);
int eprintf(const char *fmt, ...);
int vfdprintf(uint32_t fd, const char *fmt, va_list ap);
int fdprintf(uint32_t fd, const char *fmt, ...);
