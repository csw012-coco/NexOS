#include "drivers/audio/ac97.h"
#include "drivers/audio/hda.h"
#include "drivers/net/rtl8139.h"
#include "drivers/storage/ahci.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/xhci.h"
#include "fs/vfs.h"
#include "arch/x86/i386/services/shared_services.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/driver/driver.h"

static int driver_services_builtin_active(void) {
    return 1;
}

static const struct kernel_driver driver_services_ata_driver = {
    "ata",
    KERNEL_DRIVER_KIND_STORAGE,
    driver_services_builtin_active,
    0
};

static const struct kernel_driver driver_services_ramdisk_driver = {
    "ramdisk",
    KERNEL_DRIVER_KIND_STORAGE,
    driver_services_builtin_active,
    0
};

static const struct kernel_driver driver_services_devfs_driver = {
    "devfs",
    KERNEL_DRIVER_KIND_UNKNOWN,
    driver_services_builtin_active,
    0
};

static const struct kernel_driver driver_services_procfs_driver = {
    "procfs",
    KERNEL_DRIVER_KIND_UNKNOWN,
    driver_services_builtin_active,
    0
};

static const struct kernel_driver driver_services_eventfs_driver = {
    "eventfs",
    KERNEL_DRIVER_KIND_UNKNOWN,
    driver_services_builtin_active,
    0
};

static int driver_services_builtins_registered;

static void driver_services_register_builtins(void) {
    if (driver_services_builtins_registered) {
        return;
    }
    driver_manager_init();
    (void)driver_register(&driver_services_ata_driver);
    (void)driver_register(&ahci_kernel_driver);
    (void)driver_register(&rtl8139_kernel_driver);
    (void)driver_register(&ehci_kernel_driver);
    (void)driver_register(&xhci_kernel_driver);
    (void)driver_register(&driver_services_ramdisk_driver);
    (void)driver_register(&driver_services_devfs_driver);
    (void)driver_register(&driver_services_procfs_driver);
    (void)driver_register(&driver_services_eventfs_driver);
    driver_services_builtins_registered = 1;
}

void driver_services_init_builtins(int verbose) {
    driver_set_boot_verbose(verbose);
    driver_services_register_builtins();
    (void)driver_init_all();
}

void driver_services_init(struct vfs *vfs, int verbose) {
    uint32_t active;
    uint32_t discovered;

    boot_services_log("kernel: discover drivers");
    driver_set_boot_verbose(verbose);
    driver_services_register_builtins();
    discovered = driver_discover_root(vfs, "/drivers");
    discovered += driver_discover_root(vfs, "/DRIVERS");
    discovered += driver_discover_root(vfs, "/ram/DRIVERS");
    (void)discovered;
    (void)driver_load_all(vfs);
    active = driver_init_all();
    if (verbose) {
        kprint("driver: builtin active=%u total=%u files=%u\n",
               active,
               driver_count(),
               driver_file_count());
    }
}
