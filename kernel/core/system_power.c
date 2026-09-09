#include "kernel/internal/core/system_power_internal.h"

#include "abi/syscall_abi.h"
#include "hal/hal.h"
#include "kernel/public/core/kprint.h"
#include "drivers/bus/acpi.h"

static void kernel_reboot_io_delay(void) {
    for (uint32_t i = 0; i < 0x10000u; i++) {
        __asm__ __volatile__("pause");
    }
}

static int kernel_reboot_wait_kbc_input_clear(void) {
    for (uint32_t i = 0; i < 0x100000u; i++) {
        if ((hal_io_in8(0x64u) & 0x02u) == 0u) {
            return 1;
        }
        __asm__ __volatile__("pause");
    }
    return 0;
}

static void kernel_reboot_try_cf9(void) {
    hal_io_out8(0xcf9u, 0x02u);
    kernel_reboot_io_delay();
    hal_io_out8(0xcf9u, 0x06u);
    kernel_reboot_io_delay();
}

static void kernel_reboot_try_kbc(void) {
    if (kernel_reboot_wait_kbc_input_clear()) {
        hal_io_out8(0x64u, 0xfeu);
    }
    kernel_reboot_io_delay();
}

static void kernel_reboot_triple_fault(void) {
    hal_cpu_trigger_triple_fault();
}

uint64_t kernel_reboot(void) {
    kprint("kernel: reboot requested\n");
    hal_cpu_cli();

    if (acpi_reset()) {
        for (;;) {
            hal_cpu_halt();
        }
    }

    kernel_reboot_try_cf9();
    kernel_reboot_try_kbc();
    kernel_reboot_triple_fault();

    for (;;) {
        hal_cpu_halt();
    }
}

uint64_t kernel_poweroff(void) {
    kprint("kernel: poweroff requested\n");
    hal_cpu_cli();
    if (acpi_poweroff()) {
        return 0u;
    }
    kprint("kernel: ACPI poweroff unavailable\n");
    return (uint64_t)(int64_t)-NEX_ERR_NOSYS;
}
