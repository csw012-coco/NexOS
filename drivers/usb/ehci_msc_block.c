#include "drivers/usb/ehci_internal.h"

int ehci_msc_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct ehci_msc_device *dev = (struct ehci_msc_device *)bdev->driver_data;
    uint8_t *out = (uint8_t *)buffer;
    uint8_t cmd[10];

    if (dev == 0 || !dev->present || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (count == 1u && dev->read_cache_valid &&
        lba >= dev->read_cache_lba &&
        lba < dev->read_cache_lba + dev->read_cache_count) {
        const uint8_t *cached = dev->read_cache + (uint32_t)(lba - dev->read_cache_lba) * EHCI_SECTOR_SIZE;

        if (ehci_msc_buffer_has_transport_signature(cached)) {
            kprint("ehci: MSC read cache lba=%lx contains transport signature, invalidating\n", lba);
            dev->read_cache_valid = 0u;
        } else {
            memcpy(out, cached, EHCI_SECTOR_SIZE);
            return 0;
        }
    }
    if (count == 1u && dev->read_cache != 0) {
        uint32_t readahead = dev->read_ahead_sectors;

        if (readahead > EHCI_MSC_READAHEAD_MAX_SECTORS) {
            readahead = EHCI_MSC_READAHEAD_MAX_SECTORS;
        }
        if ((uint64_t)readahead > dev->sector_count - lba) {
            readahead = (uint32_t)(dev->sector_count - lba);
        }
        if (readahead > 1u) {
            uint8_t cache_ok = 0u;

            for (uint32_t attempt = 0; attempt < EHCI_MSC_RW_RETRIES; attempt++) {
                memset(cmd, 0, sizeof(cmd));
                cmd[0] = SCSI_READ_10;
                usb_write_u32be(cmd + 2, (uint32_t)lba);
                cmd[7] = (uint8_t)((readahead >> 8) & 0xffu);
                cmd[8] = (uint8_t)(readahead & 0xffu);
                if (ehci_msc_command_recover(dev,
                                             cmd,
                                             10u,
                                             dev->read_cache,
                                             readahead * EHCI_SECTOR_SIZE,
                                             1u)) {
                    if (ehci_msc_buffer_has_transport_signature(dev->read_cache)) {
                        kprint("ehci: MSC read lba=%lx returned transport signature in data buffer\n", lba);
                        dev->read_cache_valid = 0u;
                        (void)ehci_msc_reset_recovery(dev);
                        ehci_msc_retry_delay(dev);
                        continue;
                    }
                    cache_ok = 1u;
                    break;
                }
                if (dev->last_msc_phase == 2u) {
                    ehci_msc_reduce_readahead(dev);
                    break;
                }
                ehci_msc_retry_delay(dev);
            }
            if (cache_ok) {
                dev->read_cache_lba = lba;
                dev->read_cache_count = (uint8_t)readahead;
                dev->read_cache_valid = 1u;
                memcpy(out, dev->read_cache, EHCI_SECTOR_SIZE);
                return 0;
            }
            dev->read_cache_valid = 0u;
        }
    }
    if (count > 1u) {
        uint32_t done = 0u;

        dev->read_cache_valid = 0u;
        while (done < count) {
            uint8_t ok = 0u;
            uint32_t chunk = count - done;

            if (chunk > EHCI_PAGE_SIZE / EHCI_SECTOR_SIZE) {
                chunk = EHCI_PAGE_SIZE / EHCI_SECTOR_SIZE;
            }
            for (uint32_t attempt = 0; attempt < EHCI_MSC_RW_RETRIES; attempt++) {
                memset(cmd, 0, sizeof(cmd));
                cmd[0] = SCSI_READ_10;
                usb_write_u32be(cmd + 2, (uint32_t)(lba + done));
                cmd[7] = (uint8_t)((chunk >> 8) & 0xffu);
                cmd[8] = (uint8_t)(chunk & 0xffu);
                if (ehci_msc_command_recover(dev,
                                             cmd,
                                             10u,
                                             out + done * EHCI_SECTOR_SIZE,
                                             chunk * EHCI_SECTOR_SIZE,
                                             1u)) {
                    if (ehci_msc_buffer_has_transport_signature(out + done * EHCI_SECTOR_SIZE)) {
                        kprint("ehci: MSC read lba=%lx count=%u returned transport signature in data buffer\n",
                               lba + done,
                               chunk);
                        (void)ehci_msc_reset_recovery(dev);
                        ehci_msc_retry_delay(dev);
                        continue;
                    }
                    ok = 1u;
                    break;
                }
                ehci_msc_retry_delay(dev);
            }
            if (!ok) {
                uint8_t failed_phase = dev->last_msc_phase;
                uint8_t failed_status = dev->last_msc_status;

                (void)ehci_msc_request_sense(dev, dev->data, 18u);
                kprint("ehci: MSC read lba=%lx count=%u failed phase=%u status=%u sense=%x/%x/%x\n",
                       lba + done,
                       chunk,
                       (uint32_t)failed_phase,
                       (uint32_t)failed_status,
                       (uint32_t)dev->last_sense_key,
                       (uint32_t)dev->last_sense_asc,
                       (uint32_t)dev->last_sense_ascq);
                return -1;
            }
            done += chunk;
        }
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t ok = 0u;
        uint8_t verify_failed = 0u;

        for (uint32_t attempt = 0; attempt < EHCI_MSC_RW_RETRIES; attempt++) {
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = SCSI_READ_10;
            usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
            cmd[7] = 0u;
            cmd[8] = 1u;
            if (ehci_msc_command_recover(dev, cmd, 10u, out + i * EHCI_SECTOR_SIZE, EHCI_SECTOR_SIZE, 1u)) {
                if (ehci_msc_buffer_has_transport_signature(out + i * EHCI_SECTOR_SIZE)) {
                    kprint("ehci: MSC read lba=%lx returned transport signature in data buffer\n", lba + i);
                    verify_failed = 1u;
                    (void)ehci_msc_reset_recovery(dev);
                    ehci_msc_retry_delay(dev);
                    continue;
                }
                memset(cmd, 0, sizeof(cmd));
                cmd[0] = SCSI_READ_10;
                usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
                cmd[7] = 0u;
                cmd[8] = 1u;
                if (ehci_msc_command_recover(dev,
                                             cmd,
                                             10u,
                                             dev->data,
                                             EHCI_SECTOR_SIZE,
                                             1u) &&
                    !ehci_msc_buffer_has_transport_signature(dev->data) &&
                    ehci_bytes_equal(out + i * EHCI_SECTOR_SIZE, dev->data, EHCI_SECTOR_SIZE)) {
                    ok = 1u;
                    break;
                }
                verify_failed = 1u;
            }
            ehci_msc_retry_delay(dev);
        }
        if (!ok) {
            uint8_t failed_phase = dev->last_msc_phase;
            uint8_t failed_status = dev->last_msc_status;

            (void)ehci_msc_request_sense(dev, dev->data, 18u);
            kprint("ehci: MSC read lba=%lx failed phase=%u status=%u verify=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)verify_failed,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            return -1;
        }
        if (count == 1u && dev->read_cache != 0) {
            memcpy(dev->read_cache, out, EHCI_SECTOR_SIZE);
            dev->read_cache_lba = lba;
            dev->read_cache_count = 1u;
            dev->read_cache_valid = 1u;
        }
    }
    return 0;
}

int ehci_msc_write_impl(struct block_device *bdev, uint64_t lba, uint32_t count, const void *buffer) {
    struct ehci_msc_device *dev = (struct ehci_msc_device *)bdev->driver_data;
    const uint8_t *in = (const uint8_t *)buffer;
    uint8_t cmd[10];

    if (dev == 0 || !dev->present || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    dev->read_cache_valid = 0u;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t ok = 0u;
        uint8_t verify_failed = 0u;

        for (uint32_t attempt = 0; attempt < EHCI_MSC_RW_RETRIES; attempt++) {
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = SCSI_WRITE_10;
            usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
            cmd[7] = 0u;
            cmd[8] = 1u;
            if (ehci_msc_command_recover(dev, cmd, 10u, (void *)(in + i * EHCI_SECTOR_SIZE), EHCI_SECTOR_SIZE, 0u)) {
                if (!ehci_msc_sync_cache(dev, lba + i, 1u)) {
                    ehci_msc_retry_delay(dev);
                    continue;
                }
                memset(cmd, 0, sizeof(cmd));
                cmd[0] = SCSI_READ_10;
                usb_write_u32be(cmd + 2, (uint32_t)(lba + i));
                cmd[7] = 0u;
                cmd[8] = 1u;
                if (ehci_msc_command_recover(dev,
                                             cmd,
                                             10u,
                                             dev->data,
                                             EHCI_SECTOR_SIZE,
                                             1u) &&
                    !ehci_msc_buffer_has_transport_signature(dev->data) &&
                    ehci_bytes_equal(dev->data, in + i * EHCI_SECTOR_SIZE, EHCI_SECTOR_SIZE)) {
                    ok = 1u;
                    break;
                }
                if (ehci_msc_buffer_has_transport_signature(dev->data)) {
                    kprint("ehci: MSC write verify lba=%lx returned transport signature in data buffer\n", lba + i);
                    (void)ehci_msc_reset_recovery(dev);
                }
                verify_failed = 1u;
            }
            ehci_msc_retry_delay(dev);
        }
        if (!ok) {
            uint8_t failed_phase = dev->last_msc_phase;
            uint8_t failed_status = dev->last_msc_status;

            (void)ehci_msc_request_sense(dev, dev->data, 18u);
            kprint("ehci: MSC write lba=%lx failed phase=%u status=%u verify=%u sense=%x/%x/%x\n",
                   lba + i,
                   (uint32_t)failed_phase,
                   (uint32_t)failed_status,
                   (uint32_t)verify_failed,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq);
            return -1;
        }
    }
    return 0;
}
