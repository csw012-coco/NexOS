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
    uint32_t done = 0u;

    if (dev == 0 || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    while (done < count) {
        uint8_t cmd[10];
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;
        uint32_t chunk = count - done;

        if (chunk > XHCI_PAGE_SIZE / XHCI_SECTOR_SIZE) {
            chunk = XHCI_PAGE_SIZE / XHCI_SECTOR_SIZE;
        }

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_READ_10;
        usb_write_u32be(cmd + 2, (uint32_t)(lba + done));
        cmd[7] = (uint8_t)((chunk >> 8) & 0xffu);
        cmd[8] = (uint8_t)(chunk & 0xffu);
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_RW_RETRIES; attempt++) {
            if (xhci_msc_command(dev,
                                 cmd,
                                 10u,
                                 out + done * XHCI_SECTOR_SIZE,
                                 chunk * XHCI_SECTOR_SIZE,
                                 1u)) {
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
            kprint("xhci: MSC read lba=%lx count=%u failed phase=%u status=%u sense=%x/%x/%x\n",
                   lba + done,
                   chunk,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            result = -1;
            break;
        }
        done += chunk;
    }
    xhci_end_busy();
    return result;
}

static int xhci_msc_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    const uint8_t *in = (const uint8_t *)buffer;
    int result = 0;
    uint32_t done = 0u;

    if (dev == 0 || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    while (done < count) {
        uint8_t cmd[10];
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;
        uint32_t chunk = count - done;

        if (chunk > XHCI_PAGE_SIZE / XHCI_SECTOR_SIZE) {
            chunk = XHCI_PAGE_SIZE / XHCI_SECTOR_SIZE;
        }

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = SCSI_WRITE_10;
        usb_write_u32be(cmd + 2, (uint32_t)(lba + done));
        cmd[7] = (uint8_t)((chunk >> 8) & 0xffu);
        cmd[8] = (uint8_t)(chunk & 0xffu);
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_RW_RETRIES; attempt++) {
            if (xhci_msc_command(dev,
                                 cmd,
                                 10u,
                                 (void *)(in + done * XHCI_SECTOR_SIZE),
                                 chunk * XHCI_SECTOR_SIZE,
                                 0u)) {
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
            kprint("xhci: MSC write lba=%lx count=%u failed phase=%u status=%u sense=%x/%x/%x\n",
                   lba + done,
                   chunk,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            result = -1;
            break;
        }
        done += chunk;
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
