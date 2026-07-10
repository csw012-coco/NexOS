#pragma once

void kernel_runtime_display_service_pending(void);
int kernel_runtime_run_with_irqs_enabled(int (*fn)(void *ctx), void *ctx);
