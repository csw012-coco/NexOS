#pragma once

#include <stdint.h>

#include "kernel/public/sys/syscall.h"

enum {
    KERNEL_PROFILE_MAX_COUNTERS = 64u
};

uint32_t kernel_profile_register(const char *name);
uint64_t kernel_profile_clock(void);
void kernel_profile_record(uint32_t handle, uint64_t cycles, uint64_t units);
int kernel_profile_query(uint32_t index, struct syscall_profile_info *info);
void kernel_profile_reset(void);
