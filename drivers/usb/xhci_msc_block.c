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
    if (count == 1u && dev->read_cache_valid &&
        lba >= dev->read_cache_lba &&
        lba < dev->read_cache_lba + dev->read_cache_count) {
        memcpy(out,
               dev->read_cache + (uint32_t)(lba - dev->read_cache_lba) * XHCI_SECTOR_SIZE,
               XHCI_SECTOR_SIZE);
        xhci_end_busy();
        return 0;
    }
    while (done < count) {
        uint8_t cmd[10];
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;
        uint32_t chunk = count - done;
        uint8_t *chunk_out = out + done * XHCI_SECTOR_SIZE;

        if (chunk > XHCI_MSC_READAHEAD_SECTORS) {
            chunk = XHCI_MSC_READAHEAD_SECTORS;
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
                                 chunk_out,
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
        if (count == 1u && dev->read_cache != 0) {
            memcpy(dev->read_cache, out, XHCI_SECTOR_SIZE);
            dev->read_cache_lba = lba;
            dev->read_cache_count = 1u;
            dev->read_cache_valid = 1u;
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
    dev->read_cache_valid = 0u;
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

static int xhci_msc_flush_impl(struct block_device *bdev) {
    struct xhci_enum_device *dev = bdev != 0
        ? (struct xhci_enum_device *)bdev->driver_data
        : 0;
    uint8_t cmd[10];
    int ok;

    if (dev == 0 || !xhci_try_begin_busy()) {
        return -1;
    }
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_SYNCHRONIZE_CACHE_10;
    ok = xhci_msc_command(dev, cmd, 10u, 0, 0u, 0u);
    if (!ok) {
        (void)xhci_msc_request_sense(dev);
        if (dev->last_sense_key == 0x05u) {
            ok = 1;
        }
    }
    xhci_end_busy();
    return ok ? 0 : -1;
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
    dev->blockdev.flush = xhci_msc_flush_impl;
    dev->blockdev.driver_data = dev;
    if (blockdev_register(&dev->blockdev) != 0) {
        kprint("xhci: slot%u MSC block register failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    return 1;
}
