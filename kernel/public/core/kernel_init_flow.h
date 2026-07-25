#pragma once

#include <stdint.h>

struct kernel_init_step {
    const char *name;
    int (*run)(void *context);
};

int kernel_init_flow_run(const struct kernel_init_step *steps,
                         uint32_t count,
                         void *context);
