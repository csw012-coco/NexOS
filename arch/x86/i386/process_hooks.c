#include <stdint.h>
#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "scheduler_internal.h"

void process_wake_file_waiters(void *private_data, uint8_t file_kind) {
    if (private_data == 0) {
        return;
    }
    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        (void)process_lifecycle_wake_file_waiter(&tasks[slot].process,
                                                 private_data,
                                                 file_kind);
    }
}
