#include "drivers/usb/xhci_internal.h"

static void xhci_write_msc_name(char *dst, uint32_t index) {
    dst[0] = 'x';
    dst[1] = 'u';
    dst[2] = 's';
    dst[3] = 'b';
    dst[4] = 'm';
    dst[5] = 's';
    dst[6] = 'c';
    dst[7] = (char)('0' + (index % 10u));
    dst[8] = '\0';
}

static int xhci_msc_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    uint8_t *out = (uint8_t *)buffer;
    int result = 0;

    if (dev == 0 || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t cmd[10];
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_READ_10;
        usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
        cmd[8] = 1u;
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_RW_RETRIES; attempt++) {
            if (xhci_msc_command(dev, cmd, 10u, out + i * XHCI_SECTOR_SIZE, XHCI_SECTOR_SIZE, 1u)) {
                ok = 1u;
                break;
            }
            failed_phase = dev->last_msc_phase;
            failed_status = dev->last_msc_status;
            (void)xhci_msc_request_sense(dev);
            if (xhci_msc_medium_not_present(dev)) {
                break;
            }
            xhci_msc_retry_delay(failed_phase, failed_status);
        }
        if (!ok) {
            kprint("xhci: MSC read lba=%lx failed phase=%u status=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            result = -1;
            break;
        }
    }
    xhci_end_busy();
    return result;
}

static int xhci_msc_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    const uint8_t *in = (const uint8_t *)buffer;
    int result = 0;

    if (dev == 0 || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t cmd[10];
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_WRITE_10;
        usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
        cmd[8] = 1u;
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_RW_RETRIES; attempt++) {
            if (xhci_msc_command(dev, cmd, 10u, (void *)(in + i * XHCI_SECTOR_SIZE), XHCI_SECTOR_SIZE, 0u)) {
                ok = 1u;
                break;
            }
            failed_phase = dev->last_msc_phase;
            failed_status = dev->last_msc_status;
            (void)xhci_msc_request_sense(dev);
            if (xhci_msc_medium_not_present(dev)) {
                break;
            }
            xhci_msc_retry_delay(failed_phase, failed_status);
        }
        if (!ok) {
            kprint("xhci: MSC write lba=%lx failed phase=%u status=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            result = -1;
            break;
        }
    }
    xhci_end_busy();
    return result;
}

int xhci_msc_register_blockdev(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    xhci_write_msc_name(dev->name, g_xhci_msc_count);
    dev->blockdev.name = dev->name;
    dev->blockdev.block_size = XHCI_SECTOR_SIZE;
    dev->blockdev.block_count = dev->sector_count;
    dev->blockdev.read = xhci_msc_read_impl;
    dev->blockdev.write = xhci_msc_write_impl;
    dev->blockdev.driver_data = dev;
    if (blockdev_register(&dev->blockdev) != 0) {
        kprint("xhci: slot%u MSC block register failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    return 1;
}
