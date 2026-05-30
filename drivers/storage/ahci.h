#pragma once

#include <stdint.h>
#include "kernel/public/driver/driver.h"

void ahci_init(void);
uint32_t ahci_device_count(void);

extern const struct kernel_driver ahci_kernel_driver;
