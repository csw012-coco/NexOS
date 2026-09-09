#include "drivers/usb/xhci_internal.h"

enum {
    XHCI_MSC_BLOCK_IO_OFFLINE_FAILURES = 8u,
    XHCI_MSC_READ_RETRIES = 12u,
    XHCI_MSC_WRITE_RETRIES = 4u,
    XHCI_MSC_WRITE_CHUNK_SECTORS = 1u,
    XHCI_MSC_SINGLE_READ_COOLDOWN_TICKS = 250u,
    XHCI_MSC_WRITE_READY_ATTEMPTS = 3u,
    XHCI_MSC_WRITE_READY_DELAY_MS = 20u,
    XHCI_MSC_SENSE_RETRY = 0u,
    XHCI_MSC_SENSE_FAIL = 1u,
    XHCI_MSC_SENSE_OFFLINE = 2u
};

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

static int xhci_msc_failure_can_request_sense(uint8_t failed_phase, uint8_t failed_status) {
    if (failed_status == MSC_STATUS_TRANSPORT_ERROR) {
        return 0;
    }
    return failed_phase == 0u;
}

static uint8_t xhci_msc_classify_sense(const struct xhci_enum_device *dev) {
    if (xhci_msc_medium_not_present(dev)) {
        return XHCI_MSC_SENSE_OFFLINE;
    }
    switch (dev->last_sense_key) {
        case 0x02u: /* NOT READY: becoming ready or temporarily busy. */
        case 0x06u: /* UNIT ATTENTION: media/reset state changed. */
        case 0x0bu: /* ABORTED COMMAND: retry the command. */
            return XHCI_MSC_SENSE_RETRY;
        case 0x03u: /* MEDIUM ERROR: the requested sector failed. */
        case 0x04u: /* HARDWARE ERROR: do not repeat indefinitely. */
        case 0x05u: /* ILLEGAL REQUEST: command or parameter is unsupported. */
        case 0x07u: /* DATA PROTECT: the medium rejects writes. */
            return XHCI_MSC_SENSE_FAIL;
        default:
            return XHCI_MSC_SENSE_FAIL;
    }
}

static void xhci_msc_block_mark_offline(struct xhci_enum_device *dev, const char *reason) {
    if (dev == 0) {
        return;
    }
    dev->msc_offline = blockdev_is_rootfs_protected(&dev->blockdev) ? 0u : 1u;
    blockdev_record_failure(&dev->blockdev, -1, reason);
    (void)blockdev_set_state(&dev->blockdev, BLOCKDEV_STATE_OFFLINE);
    dev->read_cache_valid = 0u;
    if (dev->msc_transport_failure_logged == 0u) {
        dev->msc_transport_failure_logged = 1u;
        kprint("xhci: MSC slot=%u %s reason=%s\n",
               (uint32_t)dev->slot_id,
               dev->msc_offline ? "offline" : "rootfs-protected",
               reason != 0 ? reason : "block-io");
    }
}

static void xhci_msc_note_block_io_success(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return;
    }
    dev->msc_transport_error_count = 0u;
    dev->msc_recovery_failure_count = 0u;
}

static int xhci_msc_time_before(uint32_t lhs, uint32_t rhs) {
    return (int32_t)(lhs - rhs) < 0;
}

static void xhci_msc_enter_single_read_mode(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return;
    }
    dev->msc_single_read_until =
        hal_timer_current_ticks() + XHCI_MSC_SINGLE_READ_COOLDOWN_TICKS;
}

static int xhci_msc_single_read_mode_active(const struct xhci_enum_device *dev) {
    if (dev == 0 || dev->msc_single_read_until == 0u) {
        return 0;
    }
    return xhci_msc_time_before(hal_timer_current_ticks(), dev->msc_single_read_until);
}

static int xhci_msc_write_ready_preflight(struct xhci_enum_device *dev) {
    uint8_t cmd[6];

    if (dev == 0 || dev->msc_offline) {
        return 0;
    }
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    for (uint32_t attempt = 0u; attempt < XHCI_MSC_WRITE_READY_ATTEMPTS; attempt++) {
        if (xhci_msc_command(dev, cmd, 6u, 0, 0u, 0u)) {
            return 1;
        }
        if (dev->last_msc_status == MSC_STATUS_TRANSPORT_ERROR) {
            if (!xhci_msc_recover_transport_now(dev, cmd[0], "write-ready")) {
                return 0;
            }
            xhci_delay_ms(XHCI_MSC_WRITE_READY_DELAY_MS);
            continue;
        }
        if (!xhci_msc_request_sense(dev)) {
            return 0;
        }
        if (xhci_msc_classify_sense(dev) != XHCI_MSC_SENSE_RETRY) {
            return 0;
        }
        xhci_delay_ms(XHCI_MSC_WRITE_READY_DELAY_MS);
    }
    return 0;
}

static void xhci_msc_note_block_io_failure(struct xhci_enum_device *dev,
                                           uint8_t failed_status,
                                           const char *reason) {
    if (dev == 0 || dev->msc_offline) {
        return;
    }
    if (failed_status != MSC_STATUS_TRANSPORT_ERROR) {
        return;
    }
    dev->msc_transport_error_count++;
    if (dev->msc_transport_error_count >= XHCI_MSC_BLOCK_IO_OFFLINE_FAILURES) {
        xhci_msc_block_mark_offline(dev, reason);
    }
}

static int xhci_msc_read_impl(struct block_device *bdev, uint64_t lba, uint32_t count, void *buffer) {
    struct xhci_enum_device *dev = (struct xhci_enum_device *)bdev->driver_data;
    uint8_t *out = (uint8_t *)buffer;
    int result = 0;
    uint32_t done = 0u;
    uint32_t max_chunk = XHCI_MSC_READAHEAD_SECTORS;

    if (dev == 0 || dev->msc_offline || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (xhci_msc_single_read_mode_active(dev)) {
        max_chunk = 1u;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    if (count == 1u && dev->read_cache_valid &&
        lba >= dev->read_cache_lba &&
        lba < dev->read_cache_lba + dev->read_cache_count) {
        const uint8_t *cached =
            dev->read_cache + (uint32_t)(lba - dev->read_cache_lba) * XHCI_SECTOR_SIZE;

        if (xhci_msc_buffer_has_transport_signature(cached)) {
            kprint("xhci: MSC read cache lba=%lx contains transport signature, invalidating\n", lba);
            xhci_msc_block_mark_offline(dev, "cache-transport-signature");
            xhci_end_busy();
            return -1;
        } else {
            memcpy(out, cached, XHCI_SECTOR_SIZE);
            xhci_end_busy();
            return 0;
        }
    }
    while (done < count) {
        uint8_t cmd[16];
        uint8_t cmd_len = 10u;
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;
        uint32_t chunk = count - done;
        uint8_t *chunk_out = out + done * XHCI_SECTOR_SIZE;

        if (chunk > max_chunk) {
            chunk = max_chunk;
        }

        memset(cmd, 0, sizeof(cmd));
        if (lba + done > 0xffffffffull ||
            (uint64_t)(chunk - 1u) > 0xffffffffull - (lba + done)) {
            cmd[0] = SCSI_READ_16;
            usb_write_u64be(cmd + 2, lba + done);
            usb_write_u32be(cmd + 10, chunk);
            cmd_len = 16u;
        } else {
            cmd[0] = SCSI_READ_10;
            usb_write_u32be(cmd + 2, (uint32_t)(lba + done));
            cmd[7] = (uint8_t)((chunk >> 8) & 0xffu);
            cmd[8] = (uint8_t)(chunk & 0xffu);
        }
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_READ_RETRIES; attempt++) {
            if (xhci_msc_command(dev,
                                 cmd,
                                 cmd_len,
                                 chunk_out,
                                 chunk * XHCI_SECTOR_SIZE,
                                 1u)) {
                if (xhci_msc_buffer_has_transport_signature(chunk_out)) {
                    kprint("xhci: MSC read lba=%lx count=%u returned transport signature in data buffer\n",
                           lba + done,
                           chunk);
                    xhci_msc_block_mark_offline(dev, "data-transport-signature");
                    ok = 0u;
                    break;
                }
                ok = 1u;
                break;
            }
            failed_phase = dev->last_msc_phase;
            failed_status = dev->last_msc_status;
            if (dev->msc_offline) {
                break;
            }
            if (failed_status == MSC_STATUS_TRANSPORT_ERROR) {
                if (!xhci_msc_recover_transport_now(dev, cmd[0], "rw-retry")) {
                    break;
                }
                continue;
            }
            if (xhci_msc_failure_can_request_sense(failed_phase, failed_status)) {
                if (xhci_msc_request_sense(dev)) {
                    uint8_t sense_action = xhci_msc_classify_sense(dev);

                    if (sense_action == XHCI_MSC_SENSE_OFFLINE) {
                        xhci_msc_block_mark_offline(dev, "medium-not-present");
                        break;
                    }
                    if (sense_action == XHCI_MSC_SENSE_FAIL) {
                        break;
                    }
                }
            }
            xhci_msc_retry_delay(failed_phase, failed_status);
        }
        if (!ok) {
            dev->read_cache_valid = 0u;
            xhci_msc_enter_single_read_mode(dev);
            if (chunk > 1u) {
                max_chunk = 1u;
                kprint("xhci: MSC read lba=%lx count=%u retrying as single-sector reads\n",
                       lba + done,
                       chunk);
                continue;
            }
            xhci_msc_note_block_io_failure(dev,
                                           failed_status,
                                           "repeated-read-io-error");
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
        xhci_msc_note_block_io_success(dev);
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

    if (dev == 0 || dev->msc_offline || buffer == 0 || count == 0u ||
        lba >= dev->sector_count || (uint64_t)count > dev->sector_count - lba) {
        return -1;
    }
    if (!xhci_try_begin_busy()) {
        return -1;
    }
    dev->read_cache_valid = 0u;
    if (!xhci_msc_write_ready_preflight(dev)) {
        xhci_end_busy();
        return -1;
    }
    while (done < count) {
        uint8_t cmd[16];
        uint8_t cmd_len = 10u;
        uint8_t ok = 0u;
        uint8_t failed_phase = 0u;
        uint8_t failed_status = 0u;
        uint32_t chunk = count - done;

        if (chunk > XHCI_MSC_WRITE_CHUNK_SECTORS) {
            chunk = XHCI_MSC_WRITE_CHUNK_SECTORS;
        }

        memset(cmd, 0, sizeof(cmd));
        if (lba + done > 0xffffffffull ||
            (uint64_t)(chunk - 1u) > 0xffffffffull - (lba + done)) {
            cmd[0] = SCSI_WRITE_16;
            usb_write_u64be(cmd + 2, lba + done);
            usb_write_u32be(cmd + 10, chunk);
            cmd_len = 16u;
        } else {
            cmd[0] = SCSI_WRITE_10;
            usb_write_u32be(cmd + 2, (uint32_t)(lba + done));
            cmd[7] = (uint8_t)((chunk >> 8) & 0xffu);
            cmd[8] = (uint8_t)(chunk & 0xffu);
        }
        for (uint32_t attempt = 0u; attempt < XHCI_MSC_WRITE_RETRIES; attempt++) {
            if (xhci_msc_command(dev,
                                 cmd,
                                 cmd_len,
                                 (void *)(in + done * XHCI_SECTOR_SIZE),
                                 chunk * XHCI_SECTOR_SIZE,
                                 0u)) {
                ok = 1u;
                break;
            }
            failed_phase = dev->last_msc_phase;
            failed_status = dev->last_msc_status;
            if (dev->msc_offline) {
                break;
            }
            if (failed_status == MSC_STATUS_TRANSPORT_ERROR) {
                if (!xhci_msc_recover_transport_now(dev, cmd[0], "write-fail-recover")) {
                    break;
                }
                xhci_delay_ms(XHCI_MSC_WRITE_READY_DELAY_MS);
                continue;
            }
            if (xhci_msc_failure_can_request_sense(failed_phase, failed_status)) {
                if (xhci_msc_request_sense(dev)) {
                    uint8_t sense_action = xhci_msc_classify_sense(dev);

                    if (sense_action == XHCI_MSC_SENSE_OFFLINE) {
                        xhci_msc_block_mark_offline(dev, "medium-not-present");
                        break;
                    }
                    if (sense_action == XHCI_MSC_SENSE_FAIL) {
                        break;
                    }
                }
            }
            xhci_msc_retry_delay(failed_phase, failed_status);
        }
        if (!ok) {
            xhci_msc_note_block_io_failure(dev,
                                           failed_status,
                                           "repeated-write-io-error");
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
        xhci_msc_note_block_io_success(dev);
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

    if (dev == 0 || dev->msc_offline || !xhci_try_begin_busy()) {
        return -1;
    }
    if (xhci_msc_sync_cache_soft_allowed(dev)) {
        static uint32_t soft_logged = 0u;

        soft_logged++;
        if (soft_logged <= 4u || (soft_logged & 0x3fu) == 0u) {
            kprint("xhci: MSC sync-cache soft flush slot=%u count=%u\n",
                   (uint32_t)dev->slot_id,
                   soft_logged);
        }
        xhci_end_busy();
        return 0;
    }
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_SYNCHRONIZE_CACHE_10;
    ok = xhci_msc_command_recover(dev, cmd, 10u, 0, 0u, 0u);
    if (!ok && dev->last_msc_status != MSC_STATUS_TRANSPORT_ERROR) {
        if (xhci_msc_request_sense(dev)) {
            if (xhci_msc_classify_sense(dev) == XHCI_MSC_SENSE_OFFLINE) {
                xhci_msc_block_mark_offline(dev, "medium-not-present");
            } else if (dev->last_sense_key == 0x05u) {
                ok = 1;
            }
        }
    }
    if (ok) {
        xhci_msc_note_block_io_success(dev);
    } else {
        xhci_msc_note_block_io_failure(dev,
                                       dev->last_msc_status,
                                       "repeated-flush-io-error");
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
    dev->blockdev.reset = xhci_msc_reset_block_device;
    dev->blockdev.driver_data = dev;
    if (blockdev_register(&dev->blockdev) != 0) {
        kprint("xhci: slot%u MSC block register failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    return 1;
}
