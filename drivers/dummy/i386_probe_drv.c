#include "kernel/public/driver/driver_module.h"

static int i386_probe_drv_init(void) {
    driver_log("driver: I386TEST init\n");
    return 1;
}

const struct kernel_driver kernel_driver = {
    "I386TEST",
    KERNEL_DRIVER_KIND_UNKNOWN,
    i386_probe_drv_init,
    NULL
};
