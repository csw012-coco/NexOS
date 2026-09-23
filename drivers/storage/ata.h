#pragma once

#include <stdint.h>
#include "block/blockdev.h"
#include "kernel/public/driver/driver.h"

struct ata_device {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t slave;
    uint8_t present;
    uint8_t state;
    uint8_t last_status;
    uint32_t sector_count;
    uint32_t reset_count;
    uint32_t read_count;
    uint32_t write_count;
    uint32_t flush_count;
    uint32_t error_count;
    uint32_t last_error;
    char model[41];
    struct block_device blockdev;
};

void ata_init(void);
struct ata_device *ata_get_primary_master(void);

extern const struct kernel_driver ata_kernel_driver;
