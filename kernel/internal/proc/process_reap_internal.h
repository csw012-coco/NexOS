#pragma once

#include "kernel/internal/proc/process_lifecycle_internal.h"

void job_reset_runtime(struct job_runtime *runtime);
struct job_runtime *job_get_runtime(uint32_t slot);
struct job_runtime *job_find_runtime_by_pid(uint32_t pid);
