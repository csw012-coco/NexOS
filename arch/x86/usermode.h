#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"

void usermode_enter(uint64_t entry, uint64_t user_stack);
void usermode_resume_frame(const struct syscall_frame *frame);
void usermode_resume_from_syscall(void);
