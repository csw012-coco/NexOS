#pragma once

#include <stdint.h>

struct kernel_boot_state {
    const char *cmdline;
    const char *os_name;
    const char *kernel_name;
    const char *kernel_version;
    const char *arch_name;
    const char *build_date;
};

struct kernel_irq_state {
    uint32_t total;
    uint32_t user;
    uint32_t lines[16];
};

void kernel_boot_state_init(const char *cmdline,
                            const char *kernel_name,
                            const char *kernel_version,
                            const char *arch_name);
const struct kernel_boot_state *kernel_boot_state_get(void);
void kernel_irq_state_reset(void);
void kernel_irq_state_record(uint32_t irq_line, int from_user);
void kernel_irq_state_snapshot(struct kernel_irq_state *out);
