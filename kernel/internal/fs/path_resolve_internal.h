#pragma once

#include <stdint.h>

struct process;

int fs_resolve_process_path(const struct process *proc,
                            const char *input,
                            char *out,
                            uint32_t out_size);
