#pragma once

#include <stdint.h>
#include "bootx.h"

struct tty;
struct vfs;

void kernel_init_interrupts(void);
struct vfs *kernel_init_core_services(struct tty *shell_tty, volatile uint32_t *timer_ticks);
int kernel_try_run_init(struct vfs *vfs,
                        struct tty *shell_tty,
                        uint16_t *boot_trace_row,
                        const struct bootx_boot_info *boot_info);
