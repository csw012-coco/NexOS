#include "drivers/usb/ehci_internal.h"

int ehci_parse_msc_config(struct ehci_msc_device *dev, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_msc = 0;
    uint8_t tmp_ep;
    uint16_t tmp_mps;

    if (length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    dev->configuration = cfg[5];
    dev->msc_interface_number = 0u;
    dev->bulk_in_ep = 0u;
    dev->bulk_out_ep = 0u;
    dev->bulk_in_mps = 0u;
    dev->bulk_out_mps = 0u;
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            in_msc = cfg[offset + 5u] == USB_CLASS_MASS_STORAGE &&
                     cfg[offset + 6u] == USB_SUBCLASS_SCSI &&
                     cfg[offset + 7u] == USB_PROTO_BULK_ONLY;
            if (in_msc) {
                dev->msc_interface_number = cfg[offset + 2u];
            }
        } else if (type == 5u && len >= 7u && in_msc) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = usb_read_u16le(cfg + offset + 4u);

            if (attr == 2u && (ep & 0x80u) != 0u) {
                dev->bulk_in_ep = ep;
                dev->bulk_in_mps = mps;
            } else if (attr == 2u) {
                dev->bulk_out_ep = ep;
                dev->bulk_out_mps = mps;
            }
        }
        offset += len;
    }
    if (dev->configuration == 0u ||
        dev->bulk_in_ep == 0u ||
        dev->bulk_out_ep == 0u ||
        dev->bulk_in_mps == 0u ||
        dev->bulk_out_mps == 0u) {
        kprint("ehci: msc parsed incomplete in_ep=%x in_mps=%u out_ep=%x out_mps=%u cfg=%u\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)dev->configuration);
        return 0;
    }
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: msc parsed raw in_ep=%x in_mps=%u out_ep=%x out_mps=%u cfg=%u\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)dev->configuration);
    }
    if ((dev->bulk_in_ep & 0x80u) == 0u && (dev->bulk_out_ep & 0x80u) != 0u) {
        tmp_ep = dev->bulk_in_ep;
        tmp_mps = dev->bulk_in_mps;
        dev->bulk_in_ep = dev->bulk_out_ep;
        dev->bulk_in_mps = dev->bulk_out_mps;
        dev->bulk_out_ep = tmp_ep;
        dev->bulk_out_mps = tmp_mps;
        kprint("ehci: msc EP direction swapped in=%x out=%x\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_out_ep);
    }
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: msc parsed in_ep=%x in_mps=%u out_ep=%x out_mps=%u cfg=%u\n",
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)dev->configuration);
    }
    return 1;
}


int ehci_msc_command(struct ehci_msc_device *dev,
                            const uint8_t *cmd,
                            uint8_t cmd_len,
                            void *data,
                            uint32_t data_len,
                            uint8_t data_in) {
    uint32_t tag;
    uint32_t signature;
    uint32_t csw_tag;
    uint32_t cbw_token = 0u;
    uint32_t data_token = 0u;
    uint32_t csw_token;
    uint8_t csw_retry = 0u;

    if (dev == 0 || cmd == 0 || cmd_len == 0u || cmd_len > 16u || data_len > EHCI_PAGE_SIZE) {
        return 0;
    }
    dev->last_msc_phase = 0u;
    dev->last_msc_status = 0u;
    dev->last_msc_residue = 0u;
    tag = ++dev->tag;
    memset(dev->cbw, 0, 31u);
    usb_write_u32le(dev->cbw + 0, MSC_CBW_SIGNATURE);
    usb_write_u32le(dev->cbw + 4, tag);
    usb_write_u32le(dev->cbw + 8, data_len);
    dev->cbw[12] = data_in ? 0x80u : 0x00u;
    dev->cbw[13] = dev->msc_lun;
    dev->cbw[14] = cmd_len;
    memcpy(dev->cbw + 15, cmd, cmd_len);
    dev->last_msc_phase = 1u;
    if (!ehci_bulk_transfer(dev, dev->bulk_out_ep, dev->bulk_out_mps,
                            &dev->bulk_out_toggle, dev->cbw_phys, 31u, 0u, &cbw_token, 0u)) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        kprint("ehci: CBW send failed tag=%x op=%x out_toggle=%u cbw_token=%x\n",
               tag,
               (uint32_t)cmd[0],
               (uint32_t)dev->bulk_out_toggle,
               cbw_token);
        return 0;
    }
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: CBW ok tag=%x op=%x out_toggle=%u in_toggle=%u\n",
               tag,
               (uint32_t)cmd[0],
               (uint32_t)dev->bulk_out_toggle,
               (uint32_t)dev->bulk_in_toggle);
    }
    ehci_delay_ms(EHCI_MSC_CBW_STAGE_SETTLE_MS);
    if (data_len != 0u) {
        if (!data_in && data != 0) {
            memcpy(dev->data, data, data_len);
        } else if (data_in) {
            memset(dev->data, 0, data_len);
        }
        dev->last_msc_phase = 2u;
        if (!ehci_bulk_transfer(dev,
                                data_in ? dev->bulk_in_ep : dev->bulk_out_ep,
                                data_in ? dev->bulk_in_mps : dev->bulk_out_mps,
                                data_in ? &dev->bulk_in_toggle : &dev->bulk_out_toggle,
                                dev->data_phys,
                                data_len,
                                data_in,
                                &data_token,
                                EHCI_ASYNC_BULK_DATA_SPINS)) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            kprint("ehci: DATA %s failed tag=%x op=%x len=%u toggle=%u data_token=%x in_mps=%u out_mps=%u\n",
                   data_in ? "in" : "out",
                   tag,
                   (uint32_t)cmd[0],
                   data_len,
                   (uint32_t)(data_in ? dev->bulk_in_toggle : dev->bulk_out_toggle),
                   data_token,
                   (uint32_t)dev->bulk_in_mps,
                   (uint32_t)dev->bulk_out_mps);
            return 0;
        }
        if (data_in && data != 0) {
            memcpy(data, dev->data, data_len);
        }
        ehci_delay_ms(EHCI_MSC_DATA_CSW_SETTLE_MS);
    }
    dev->last_msc_phase = 3u;
    for (;;) {
        memset(dev->csw, 0, 13u);
        csw_token = 0u;
        if (ehci_bulk_transfer(dev, dev->bulk_in_ep, dev->bulk_in_mps,
                               &dev->bulk_in_toggle, dev->csw_phys, 13u, 1u, &csw_token,
                               EHCI_ASYNC_BULK_CSW_SPINS)) {
            signature = usb_read_u32le(dev->csw);
            csw_tag = usb_read_u32le(dev->csw + 4);
            dev->last_msc_residue = usb_read_u32le(dev->csw + 8);
            dev->last_msc_status = dev->csw[12];
            if (signature == MSC_CSW_SIGNATURE && csw_tag == tag) {
                if (dev->last_msc_status == 0u) {
                    break;
                }
                if (dev->last_msc_status == 1u) {
                    dev->last_msc_phase = 0u;
                    return 0;
                }
                if (dev->last_msc_status == 2u) {
                    dev->last_msc_phase = 4u;
                    return 0;
                }
                dev->last_msc_phase = 4u;
                return 0;
            }
            if (csw_retry != 0u) {
                dev->last_msc_phase = 4u;
                return 0;
            }
        } else {
            if (csw_retry != 0u) {
                dev->last_msc_phase = 3u;
                dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
                kprint("ehci: CSW recv failed x2 tag=%x op=%x in_toggle=%u csw_token=%x\n",
                       tag,
                       (uint32_t)cmd[0],
                       (uint32_t)dev->bulk_in_toggle,
                       csw_token);
                return 0;
            }
            if (EHCI_MSC_DEBUG) {
                kprint("ehci: CSW recv fail#1 tag=%x op=%x in_toggle=%u csw_token=%x\n",
                       tag,
                       (uint32_t)cmd[0],
                       (uint32_t)dev->bulk_in_toggle,
                       csw_token);
            }
            (void)ehci_clear_endpoint_halt(dev, dev->bulk_in_ep);
            dev->bulk_in_toggle = 0u;
            ehci_delay_ms(5u);
        }
        csw_retry = 1u;
    }
    dev->last_msc_phase = 0u;
    return 1;
}

int ehci_clear_endpoint_halt(struct ehci_msc_device *dev, uint8_t ep) {
    struct usb_ctrl_request req;
    uint16_t mps = dev->control_mps != 0u ? dev->control_mps : 64u;

    req.type = 0x02u;
    req.request = USB_REQ_CLEAR_FEATURE;
    req.value = USB_FEATURE_ENDPOINT_HALT;
    req.index = ep;
    req.length = 0u;
    return ehci_control_transfer(dev, dev->address, mps, &req, 0, 0u, 0u);
}

int ehci_msc_reset_recovery(struct ehci_msc_device *dev) {
    struct usb_ctrl_request req;
    uint16_t mps;
    int ok;

    if (dev == 0) {
        return 0;
    }
    mps = dev->control_mps != 0u ? dev->control_mps : 64u;
    req.type = 0x21u;
    req.request = USB_REQ_BULK_ONLY_RESET;
    req.value = 0u;
    req.index = dev->msc_interface_number;
    req.length = 0u;
    ok = ehci_control_transfer(dev, dev->address, mps, &req, 0, 0u, 0u);
    ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
    ok = ehci_clear_endpoint_halt(dev, dev->bulk_in_ep) && ok;
    ehci_delay_ms(20u);
    ok = ehci_clear_endpoint_halt(dev, dev->bulk_out_ep) && ok;
    ehci_delay_ms(20u);
    dev->bulk_in_toggle = 0u;
    dev->bulk_out_toggle = 0u;
    dev->read_cache_valid = 0u;
    ehci_delay_ms(50u);
    return ok;
}

struct ehci_msc_device *ehci_find_hub_by_addr(uint8_t address) {
    for (uint32_t i = 0; i < g_ehci_hub_count; i++) {
        if (g_ehci_hubs[i].present && g_ehci_hubs[i].address == address) {
            return &g_ehci_hubs[i];
        }
    }
    return 0;
}

int ehci_reset_device_port(struct ehci_msc_device *dev) {
    struct ehci_msc_device *hub;
    uint16_t status = 0u;
    uint16_t change = 0u;
    uint32_t root_status = 0u;

    if (dev == 0) {
        return 0;
    }
    if (dev->hub_addr == 0u) {
        if (!ehci_reset_port(dev->root_port, &root_status)) {
            return 0;
        }
        (void)root_status;
        dev->speed = EHCI_DEV_SPEED_HIGH;
        return 1;
    }
    hub = ehci_find_hub_by_addr(dev->hub_addr);
    if (hub == 0 || !ehci_hub_set_port_feature(hub, dev->hub_port, USB_HUB_FEATURE_PORT_RESET)) {
        return 0;
    }
    ehci_delay_ms(100u);
    for (uint32_t i = 0; i < 300u; i++) {
        if (!ehci_hub_get_port_status(hub, dev->hub_port, &status, &change)) {
            return 0;
        }
        if ((status & USB_HUB_PORT_RESET) == 0u) {
            break;
        }
        ehci_delay_ms(1u);
    }
    ehci_hub_clear_changes(hub, dev->hub_port, change);
    if (!ehci_hub_get_port_status(hub, dev->hub_port, &status, &change) ||
        (status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        return 0;
    }
    dev->speed = ehci_hub_child_speed(status);
    return 1;
}

int ehci_msc_hard_reset_recovery(struct ehci_msc_device *dev) {
    uint16_t mps;

    if (dev == 0 || dev->address == 0u || dev->configuration == 0u) {
        return 0;
    }
    mps = dev->control_mps != 0u ? dev->control_mps : 64u;
    if (!ehci_reset_device_port(dev)) {
        return 0;
    }
    ehci_delay_ms(80u);
    if (!ehci_set_address(dev, dev->address, mps)) {
        return 0;
    }
    ehci_delay_ms(20u);
    if (!ehci_set_configuration(dev, dev->configuration, mps)) {
        return 0;
    }
    dev->bulk_in_toggle = 0u;
    dev->bulk_out_toggle = 0u;
    dev->read_cache_valid = 0u;
    ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
    return 1;
}

int ehci_msc_command_recover(struct ehci_msc_device *dev,
                                    const uint8_t *cmd,
                                    uint8_t cmd_len,
                                    void *data,
                                    uint32_t data_len,
                                    uint8_t data_in) {
    uint8_t failed_phase;
    uint8_t read10_csw_data_ok = 0u;

    if (ehci_msc_command(dev, cmd, cmd_len, data, data_len, data_in)) {
        return 1;
    }
    failed_phase = dev != 0 ? dev->last_msc_phase : 0u;
    if (dev != 0 && cmd != 0 && data != 0 &&
        failed_phase == 3u &&
        data_in && data_len != 0u && data_len <= EHCI_PAGE_SIZE &&
        cmd_len != 0u && cmd[0] == SCSI_READ_10 &&
        !ehci_msc_buffer_has_transport_signature((const uint8_t *)data)) {
        read10_csw_data_ok = 1u;
        if (data == dev->data && dev->read_cache != 0) {
            memcpy(dev->read_cache, data, data_len);
        }
    }
    if (dev != 0 && (failed_phase == 1u ||
                     failed_phase == 2u ||
                     failed_phase == 3u ||
                     failed_phase == 4u)) {
        int recovered = ehci_msc_reset_recovery(dev);

        if (!recovered) {
            recovered = ehci_msc_hard_reset_recovery(dev);
        }
        if (read10_csw_data_ok && recovered) {
            if (data == dev->data && dev->read_cache != 0) {
                memcpy(data, dev->read_cache, data_len);
            }
            dev->last_msc_phase = 0u;
            dev->last_msc_status = 0u;
            dev->last_msc_residue = 0u;
            return 1;
        }
    }
    return 0;
}

uint8_t ehci_msc_get_max_lun(struct ehci_msc_device *dev) {
    struct usb_ctrl_request req;
    uint8_t lun = 0u;
    uint16_t mps;

    if (dev == 0) {
        return 0u;
    }
    mps = dev->control_mps != 0u ? dev->control_mps : 64u;
    req.type = 0xa1u;
    req.request = USB_REQ_GET_MAX_LUN;
    req.value = 0u;
    req.index = dev->msc_interface_number;
    req.length = 1u;
    if (!ehci_control_transfer(dev, dev->address, mps, &req, &lun, 1u, 1u)) {
        return 0u;
    }
    if (lun > USB_MSC_MAX_LUN_LIMIT) {
        return 0u;
    }
    if (lun != 0u) {
        kprint("ehci: MSC GetMaxLUN reported %u\n", (uint32_t)lun);
    }
    return lun;
}

void ehci_msc_record_sense(struct ehci_msc_device *dev, const uint8_t *sense, uint32_t length) {
    if (dev == 0 || sense == 0 || length < 14u) {
        return;
    }
    dev->last_sense_key = sense[2] & 0x0fu;
    dev->last_sense_asc = sense[12];
    dev->last_sense_ascq = sense[13];
}

void ehci_msc_reduce_readahead(struct ehci_msc_device *dev) {
    if (dev == 0 || dev->read_ahead_sectors <= 1u) {
        return;
    }
    dev->read_ahead_sectors >>= 1;
    if (dev->read_ahead_sectors == 0u) {
        dev->read_ahead_sectors = 1u;
    }
    kprint("ehci: MSC readahead reduced to %u sectors\n",
           (uint32_t)dev->read_ahead_sectors);
}

int ehci_msc_request_sense(struct ehci_msc_device *dev, uint8_t *sense, uint32_t length) {
    uint8_t cmd[6];
    int ok;

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = (uint8_t)length;
    ok = ehci_msc_command_recover(dev, cmd, 6u, sense, length, 1u);
    if (ok) {
        ehci_msc_record_sense(dev, sense, length);
    }
    return ok;
}

int ehci_bytes_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < size; i++) {
        if (lhs[i] != rhs[i]) {
            return 0;
        }
    }
    return 1;
}

int ehci_msc_buffer_has_transport_signature(const uint8_t *data) {
    uint32_t signature;

    if (data == 0) {
        return 0;
    }
    signature = usb_read_u32le(data);
    return signature == MSC_CBW_SIGNATURE || signature == MSC_CSW_SIGNATURE;
}

int ehci_msc_sync_cache(struct ehci_msc_device *dev, uint64_t lba, uint32_t count) {
    uint8_t cmd[10];

    if (dev == 0 || !dev->sync_cache_supported) {
        return 1;
    }
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_SYNCHRONIZE_CACHE_10;
    usb_write_u32be(cmd + 2, (uint32_t)lba);
    cmd[7] = (uint8_t)((count >> 8) & 0xffu);
    cmd[8] = (uint8_t)(count & 0xffu);
    if (ehci_msc_command_recover(dev, cmd, 10u, 0, 0u, 0u)) {
        return 1;
    }
    (void)ehci_msc_request_sense(dev, dev->data, 18u);
    if (dev->last_sense_key == 0x05u) {
        dev->sync_cache_supported = 0u;
        kprint("ehci: MSC synchronize cache unsupported, continuing without it\n");
        return 1;
    }
    return 0;
}

void ehci_msc_retry_delay(struct ehci_msc_device *dev) {
    if (dev == 0) {
        return;
    }
    if (dev->last_msc_phase == 0u && dev->last_msc_status != 0u) {
        (void)ehci_msc_request_sense(dev, dev->data, 18u);
        ehci_delay_ms(20u);
        return;
    }
    if (dev->last_msc_phase == 1u || dev->last_msc_phase == 2u ||
        dev->last_msc_phase == 3u || dev->last_msc_phase == 4u) {
        ehci_delay_ms(120u);
        return;
    }
    ehci_delay_ms(20u);
}

int ehci_msc_test_unit_ready(struct ehci_msc_device *dev) {
    uint8_t cmd[6];

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    return ehci_msc_command_recover(dev, cmd, 6u, 0, 0u, 0u);
}

int ehci_msc_wait_ready(struct ehci_msc_device *dev) {
    uint8_t sense[18];
    uint8_t failed_phase;
    uint8_t failed_status;

    for (uint32_t i = 0; i < 20u; i++) {
        memset(sense, 0, sizeof(sense));
        if (ehci_msc_test_unit_ready(dev)) {
            return 1;
        }
        failed_phase = dev != 0 ? dev->last_msc_phase : 0u;
        failed_status = dev != 0 ? dev->last_msc_status : 0u;
        if (failed_phase == 1u ||
            failed_phase == 2u) {
            ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
            continue;
        }
        if (failed_phase == 3u) {
            if (failed_status == MSC_STATUS_TRANSPORT_ERROR) {
                ehci_delay_ms(50u);
                continue;
            }
            (void)ehci_msc_request_sense(dev, sense, sizeof(sense));
            if (dev->last_sense_key == 0x02u && dev->last_sense_asc == 0x3au) {
                return 0;
            }
            ehci_delay_ms(100u);
            continue;
        }
        if (failed_phase == 4u) {
            ehci_delay_ms(EHCI_MSC_RESET_SETTLE_MS);
            continue;
        }
        (void)ehci_msc_request_sense(dev, sense, sizeof(sense));
        if (dev->last_sense_key == 0x02u && dev->last_sense_asc == 0x3au) {
            return 0;
        }
        ehci_delay_ms(100u);
    }
    return 0;
}

int ehci_msc_read_capacity(struct ehci_msc_device *dev) {
    uint8_t cmd[10];
    uint8_t cap[8];
    uint32_t last_lba;
    uint32_t block_len;

    memset(cmd, 0, sizeof(cmd));
    memset(cap, 0, sizeof(cap));
    cmd[0] = SCSI_READ_CAPACITY_10;
    if (!ehci_msc_command_recover(dev, cmd, 10u, cap, 8u, 1u)) {
        return 0;
    }
    last_lba = usb_read_u32be(cap);
    block_len = usb_read_u32be(cap + 4);
    if (block_len != EHCI_SECTOR_SIZE) {
        kprint("ehci: MSC read capacity block_len=%x last_lba=%x\n", block_len, last_lba);
        return 0;
    }
    dev->sector_count = (uint64_t)last_lba + 1u;
    return dev->sector_count != 0u;
}

int ehci_msc_probe(struct ehci_msc_device *dev) {
    uint8_t cmd[6];
    uint8_t inquiry[36];

    if (dev == 0) {
        return 0;
    }
    dev->max_lun = ehci_msc_get_max_lun(dev);
    kprint("ehci: MSC if=%u maxlun=%u in=%x/%u out=%x/%u ctrl_mps=%u\n",
           (uint32_t)dev->msc_interface_number,
           (uint32_t)dev->max_lun,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_in_mps,
           (uint32_t)dev->bulk_out_ep,
           (uint32_t)dev->bulk_out_mps,
           (uint32_t)dev->control_mps);
    if (EHCI_MSC_DEBUG) {
        kprint("ehci: phys qh=%lx qtd=%lx data=%lx cbw=%lx csw=%lx\n",
               dev->qh_phys,
               dev->qtd_phys,
               dev->data_phys,
               dev->cbw_phys,
               dev->csw_phys);
    }
    for (uint8_t lun = 0u; lun <= dev->max_lun; lun++) {
        uint8_t inquiry_ok = 0u;

        dev->msc_lun = lun;
        dev->last_sense_key = 0u;
        dev->last_sense_asc = 0u;
        dev->last_sense_ascq = 0u;
        memset(cmd, 0, sizeof(cmd));
        memset(inquiry, 0, sizeof(inquiry));
        cmd[0] = SCSI_INQUIRY;
        cmd[4] = sizeof(inquiry);
        for (uint32_t i = 0; i < 5u; i++) {
            if (ehci_msc_command_recover(dev, cmd, 6u, inquiry, sizeof(inquiry), 1u)) {
                inquiry_ok = 1u;
                break;
            }
            ehci_delay_ms(100u);
        }
        if (!inquiry_ok) {
            kprint("ehci: MSC lun%u inquiry failed phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        if (!ehci_msc_wait_ready(dev)) {
            kprint("ehci: MSC lun%u not ready sense=%x/%x/%x phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        for (uint32_t i = 0; i < 10u; i++) {
            if (ehci_msc_read_capacity(dev)) {
                return 1;
            }
            (void)ehci_msc_request_sense(dev, inquiry, 18u);
            ehci_delay_ms(100u);
        }
        kprint("ehci: MSC lun%u read capacity failed sense=%x/%x/%x phase=%u status=%u residue=%x\n",
               (uint32_t)lun,
               (uint32_t)dev->last_sense_key,
               (uint32_t)dev->last_sense_asc,
               (uint32_t)dev->last_sense_ascq,
               (uint32_t)dev->last_msc_phase,
               (uint32_t)dev->last_msc_status,
               dev->last_msc_residue);
    }
    return 0;
}
