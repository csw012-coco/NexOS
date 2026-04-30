#pragma once

#include <stdint.h>

uint64_t syscall4_raw(uint64_t number,
                      uint64_t arg0,
                      uint64_t arg1,
                      uint64_t arg2,
                      uint64_t arg3);
