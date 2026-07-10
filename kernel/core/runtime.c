#include "kernel/internal/core/runtime_internal.h"
#include "hal/hal.h"

void kernel_runtime_display_service_pending(void) {
    hal_display_service_pending();
}

int kernel_runtime_run_with_irqs_enabled(int (*fn)(void *ctx), void *ctx) {
    int result;

    if (fn == 0) {
        return 0;
    }
    hal_cpu_sti();
    result = fn(ctx);
    hal_cpu_cli();
    return result;
}
