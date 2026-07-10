#include "kernel/internal/core/boot_state_internal.h"

static struct kernel_boot_state g_kernel_boot_state = {
    "",
    "NexOS",
    "kernel",
    "unknown",
    "unknown",
    __DATE__ " " __TIME__
};

static volatile uint32_t g_kernel_irq_total;
static volatile uint32_t g_kernel_irq_user;
static volatile uint32_t g_kernel_irq_lines[16];

void kernel_boot_state_init(const char *cmdline,
                            const char *kernel_name,
                            const char *kernel_version,
                            const char *arch_name) {
    g_kernel_boot_state.cmdline = cmdline != 0 ? cmdline : "";
    g_kernel_boot_state.os_name = "NexOS";
    g_kernel_boot_state.kernel_name = kernel_name != 0 ? kernel_name : "kernel";
    g_kernel_boot_state.kernel_version = kernel_version != 0 ? kernel_version : "unknown";
    g_kernel_boot_state.arch_name = arch_name != 0 ? arch_name : "unknown";
    g_kernel_boot_state.build_date = __DATE__ " " __TIME__;
}

const struct kernel_boot_state *kernel_boot_state_get(void) {
    return &g_kernel_boot_state;
}

void kernel_irq_state_reset(void) {
    g_kernel_irq_total = 0u;
    g_kernel_irq_user = 0u;
    for (uint32_t i = 0u; i < 16u; i++) {
        g_kernel_irq_lines[i] = 0u;
    }
}

void kernel_irq_state_record(uint32_t irq_line, int from_user) {
    g_kernel_irq_total++;
    if (from_user) {
        g_kernel_irq_user++;
    }
    if (irq_line < 16u) {
        g_kernel_irq_lines[irq_line]++;
    }
}

void kernel_irq_state_snapshot(struct kernel_irq_state *out) {
    if (out == 0) {
        return;
    }
    out->total = g_kernel_irq_total;
    out->user = g_kernel_irq_user;
    for (uint32_t i = 0u; i < 16u; i++) {
        out->lines[i] = g_kernel_irq_lines[i];
    }
}
