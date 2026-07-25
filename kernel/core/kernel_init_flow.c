#include "kernel/public/core/kernel_init_flow.h"

int kernel_init_flow_run(const struct kernel_init_step *steps,
                         uint32_t count,
                         void *context) {
    if (steps == 0) {
        return count == 0u;
    }
    for (uint32_t i = 0u; i < count; i++) {
        if (steps[i].run == 0) {
            continue;
        }
        if (!steps[i].run(context)) {
            return 0;
        }
    }
    return 1;
}
