#pragma once

#include <stdint.h>

void kernel_runtime_display_service_pending(void);
void kernel_runtime_display_present(void);
uint32_t kernel_runtime_timer_hz(void);
void kernel_runtime_wait_for_interrupt(void);
int kernel_runtime_run_with_irqs_enabled(int (*fn)(void *ctx), void *ctx);
