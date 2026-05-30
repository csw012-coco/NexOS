#include "kernel/public/driver/driver_module.h"

#define AC97_PCI_CLASS_MULTIMEDIA 0x04u
#define AC97_PCI_SUBCLASS_AUDIO 0x01u
#define AC97_PCI_COMMAND_OFFSET 0x04u
#define AC97_GLOB_STA 0x30u

static void (*volatile ac97_drv_log)(const char *fmt, ...) = driver_log;

static int ac97_drv_init(void) {
    struct driver_pci_device ac97;
    uint16_t bus_master_base;
    uint16_t command;
    uint16_t command_after;
    uint32_t global_status;

    if (!driver_pci_find_by_class(AC97_PCI_CLASS_MULTIMEDIA,
                                  AC97_PCI_SUBCLASS_AUDIO,
                                  0u,
                                  &ac97)) {
        ac97_drv_log("driver: AC97MOD controller not found\n");
        return 0;
    }
    command = driver_pci_read16(&ac97, AC97_PCI_COMMAND_OFFSET);
    driver_pci_write16(&ac97, AC97_PCI_COMMAND_OFFSET, command);
    command_after = driver_pci_read16(&ac97, AC97_PCI_COMMAND_OFFSET);
    bus_master_base = (uint16_t)(ac97.bar[1] & 0xfffcu);
    global_status = driver_io_in32((uint16_t)(bus_master_base + AC97_GLOB_STA));
    ac97_drv_log("driver: AC97MOD bdf=%u:%u.%u vendor=%x device=%x cmd=%x cmd2=%x sta=%x\n",
                 (uint32_t)ac97.bus,
                 (uint32_t)ac97.slot,
                 (uint32_t)ac97.function,
                 (uint32_t)ac97.vendor_id,
                 (uint32_t)ac97.device_id,
                 (uint32_t)command,
                 (uint32_t)command_after,
                 global_status);
    return 1;
}

const struct kernel_driver kernel_driver = {
    "AC97MOD",
    KERNEL_DRIVER_KIND_AUDIO,
    ac97_drv_init,
    NULL
};
