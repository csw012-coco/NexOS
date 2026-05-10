#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/system_power_internal.h"

uint64_t syscall_handle_reboot(void) {
    return kernel_reboot();
}
