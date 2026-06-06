#include "drivers/usb/xhci_internal.h"


int xhci_parse_msc_config(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_msc = 0;
    uint8_t saw_msc = 0;
    uint8_t saw_uas = 0;

    if (dev == 0 || cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    dev->configuration = cfg[5];
    dev->msc_interface_number = 0u;
    dev->bulk_in_ep = 0u;
    dev->bulk_out_ep = 0u;
    dev->bulk_in_epid = 0u;
    dev->bulk_out_epid = 0u;
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
            if (cfg[offset + 5u] == USB_CLASS_MASS_STORAGE) {
                saw_msc = 1u;
                saw_uas = cfg[offset + 7u] == 0x62u ? 1u : saw_uas;
                if (in_msc) {
                    dev->msc_interface_number = cfg[offset + 2u];
                }
                kprint("xhci: slot%u MSC iface=%u subclass=%u proto=%x eps=%u bot=%u\n",
                       (uint32_t)dev->slot_id,
                       (uint32_t)cfg[offset + 2u],
                       (uint32_t)cfg[offset + 6u],
                       (uint32_t)cfg[offset + 7u],
                       (uint32_t)cfg[offset + 4u],
                       (uint32_t)in_msc);
            }
        } else if (type == 5u && len >= 7u && in_msc) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = (uint16_t)(usb_read_u16le(cfg + offset + 4u) & 0x07ffu);
            uint8_t ep_num = ep & 0x0fu;

            if (attr == 2u && ep_num != 0u && (ep & 0x80u) != 0u) {
                dev->bulk_in_ep = ep;
                dev->bulk_in_epid = (uint8_t)(ep_num * 2u + 1u);
                dev->bulk_in_mps = mps;
            } else if (attr == 2u && ep_num != 0u) {
                dev->bulk_out_ep = ep;
                dev->bulk_out_epid = (uint8_t)(ep_num * 2u);
                dev->bulk_out_mps = mps;
            }
        }
        offset += len;
    }
    if (dev->configuration != 0u &&
        dev->bulk_in_epid != 0u &&
        dev->bulk_out_epid != 0u &&
        dev->bulk_in_mps != 0u &&
        dev->bulk_out_mps != 0u) {
        return 1;
    }
    if (saw_msc) {
        kprint("xhci: slot%u MSC unsupported or incomplete in=%x/%u out=%x/%u uas=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)dev->bulk_in_ep,
               (uint32_t)dev->bulk_in_mps,
               (uint32_t)dev->bulk_out_ep,
               (uint32_t)dev->bulk_out_mps,
               (uint32_t)saw_uas);
    }
    return 0;
}

static void xhci_prepare_bulk_context(struct xhci_enum_device *dev) {
    uint8_t *input_control = xhci_context_ptr(dev->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(dev->input_context, 1u);
    uint8_t *out_slot = xhci_context_ptr(dev->device_context, 0u);
    uint8_t *bulk_out = xhci_context_ptr(dev->input_context, (uint32_t)dev->bulk_out_epid + 1u);
    uint8_t *bulk_in = xhci_context_ptr(dev->input_context, (uint32_t)dev->bulk_in_epid + 1u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;
    uint32_t *out_ctx = (uint32_t *)bulk_out;
    uint32_t *in_ctx = (uint32_t *)bulk_in;
    uint8_t last_epid = dev->bulk_in_epid > dev->bulk_out_epid ? dev->bulk_in_epid : dev->bulk_out_epid;

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(slot, out_slot, g_xhci.context_size);
    ic[1] = XHCI_SLOT_FLAG |
            (1u << dev->bulk_out_epid) |
            (1u << dev->bulk_in_epid);
    slot_ctx[0] = (slot_ctx[0] & ~(0x1fu << 27)) | ((uint32_t)last_epid << 27);

    out_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (2u << 3) | ((uint32_t)dev->bulk_out_mps << 16);
    out_ctx[2] = (uint32_t)dev->bulk_out_ring_phys | 1u;
    out_ctx[3] = (uint32_t)(dev->bulk_out_ring_phys >> 32);
    out_ctx[4] = dev->bulk_out_mps;

    in_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (6u << 3) | ((uint32_t)dev->bulk_in_mps << 16);
    in_ctx[2] = (uint32_t)dev->bulk_in_ring_phys | 1u;
    in_ctx[3] = (uint32_t)(dev->bulk_in_ring_phys >> 32);
    in_ctx[4] = dev->bulk_in_mps;
}

static int xhci_configure_bulk_endpoints(struct xhci_enum_device *dev) {
    xhci_prepare_bulk_context(dev);
    return xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, dev);
}

static int xhci_bulk_endpoint_ring(struct xhci_enum_device *dev,
                                   uint8_t endpoint_id,
                                   struct xhci_trb **ring_out,
                                   uint64_t *ring_phys_out,
                                   uint32_t **enqueue_out,
                                   uint8_t **cycle_out) {
    if (dev == 0 || ring_out == 0 || ring_phys_out == 0 || enqueue_out == 0 || cycle_out == 0) {
        return 0;
    }
    if (endpoint_id == dev->bulk_in_epid) {
        *ring_out = dev->bulk_in_ring;
        *ring_phys_out = dev->bulk_in_ring_phys;
        *enqueue_out = &dev->bulk_in_enqueue;
        *cycle_out = &dev->bulk_in_cycle;
        return 1;
    }
    if (endpoint_id == dev->bulk_out_epid) {
        *ring_out = dev->bulk_out_ring;
        *ring_phys_out = dev->bulk_out_ring_phys;
        *enqueue_out = &dev->bulk_out_enqueue;
        *cycle_out = &dev->bulk_out_cycle;
        return 1;
    }
    return 0;
}

static void xhci_reset_transfer_ring(struct xhci_trb *ring,
                                     uint64_t ring_phys,
                                     uint32_t *enqueue,
                                     uint8_t *cycle) {
    if (ring == 0 || enqueue == 0 || cycle == 0) {
        return;
    }
    memset(ring, 0, XHCI_PAGE_SIZE);
    xhci_ring_trb_set(&ring[XHCI_RING_TRBS - 1u],
                      ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    *enqueue = 0u;
    *cycle = 1u;
}

static uint8_t xhci_endpoint_state(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    uint8_t *endpoint;
    uint32_t *ctx;

    if (dev == 0 || dev->device_context == 0 || endpoint_id == 0u) {
        return XHCI_EP_STATE_DISABLED;
    }
    endpoint = xhci_context_ptr(dev->device_context, endpoint_id);
    ctx = (uint32_t *)endpoint;
    return (uint8_t)(ctx[0] & 0x7u);
}

static int xhci_stop_endpoint_if_running(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    uint8_t state = xhci_endpoint_state(dev, endpoint_id);

    if (state == XHCI_EP_STATE_RUNNING) {
        return xhci_command_endpoint(XHCI_TRB_STOP_ENDPOINT, dev, endpoint_id, 0u);
    }
    return state == XHCI_EP_STATE_STOPPED ||
           state == XHCI_EP_STATE_HALTED ||
           state == XHCI_EP_STATE_ERROR;
}

static int xhci_reset_bulk_endpoint_state(struct xhci_enum_device *dev, uint8_t endpoint_id) {
    struct xhci_trb *ring;
    uint64_t ring_phys;
    uint32_t *enqueue;
    uint8_t *cycle;
    uint8_t state;

    if (!xhci_bulk_endpoint_ring(dev, endpoint_id, &ring, &ring_phys, &enqueue, &cycle)) {
        return 0;
    }
    state = xhci_endpoint_state(dev, endpoint_id);
    if (state == XHCI_EP_STATE_RUNNING) {
        if (!xhci_command_endpoint(XHCI_TRB_STOP_ENDPOINT, dev, endpoint_id, 0u)) {
            return 0;
        }
        state = xhci_endpoint_state(dev, endpoint_id);
    }
    if (state == XHCI_EP_STATE_HALTED) {
        if (!xhci_command_endpoint(XHCI_TRB_RESET_ENDPOINT, dev, endpoint_id, 0u)) {
            return 0;
        }
        state = xhci_endpoint_state(dev, endpoint_id);
    }
    if (state != XHCI_EP_STATE_STOPPED && state != XHCI_EP_STATE_ERROR) {
        if (!xhci_stop_endpoint_if_running(dev, endpoint_id)) {
            return 0;
        }
    }
    xhci_reset_transfer_ring(ring, ring_phys, enqueue, cycle);
    return xhci_command_endpoint(XHCI_TRB_SET_TR_DEQUEUE, dev, endpoint_id, ring_phys | 1u);
}

static int xhci_bulk_transfer(struct xhci_enum_device *dev,
                              uint8_t endpoint_id,
                              uint64_t phys,
                              uint32_t bytes) {
    uint32_t completion = 0u;
    struct xhci_trb *ring;
    uint64_t ring_phys;
    uint64_t submitted_trb_phys;
    uint32_t trb_flags;
    uint32_t *enqueue;
    uint8_t *cycle;

    if (dev == 0 || endpoint_id == 0u || bytes > XHCI_PAGE_SIZE) {
        return 0;
    }
    dev->last_bulk_completion = 0u;
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    if (endpoint_id == dev->bulk_in_epid) {
        ring = dev->bulk_in_ring;
        ring_phys = dev->bulk_in_ring_phys;
        enqueue = &dev->bulk_in_enqueue;
        cycle = &dev->bulk_in_cycle;
    } else if (endpoint_id == dev->bulk_out_epid) {
        ring = dev->bulk_out_ring;
        ring_phys = dev->bulk_out_ring_phys;
        enqueue = &dev->bulk_out_enqueue;
        cycle = &dev->bulk_out_cycle;
    } else {
        return 0;
    }
    trb_flags = (XHCI_TRB_NORMAL << 10) | (1u << 5);
    if ((endpoint_id & 1u) != 0u) {
        trb_flags |= 1u << 2;
    }
    submitted_trb_phys = xhci_transfer_ring_trb(ring,
                                                ring_phys,
                                                enqueue,
                                                cycle,
                                                phys,
                                                bytes,
                                                trb_flags);
    __asm__ __volatile__("" ::: "memory");
    xhci_write32(g_xhci.doorbell, (uint32_t)dev->slot_id * 4u, endpoint_id);
    if (!xhci_wait_transfer_event_spins(dev->slot_id,
                                        endpoint_id,
                                        &completion,
                                        submitted_trb_phys,
                                        XHCI_MSC_BULK_WAIT_SPINS)) {
        uint8_t state = xhci_endpoint_state(dev, endpoint_id);
        dev->last_bulk_completion = 0u;
        kprint("xhci: bulk timeout slot=%u epid=%u bytes=%u trb=%lx state=%u enq=%u cyc=%u ev=%u/%u usbsts=%x\n",
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               bytes,
               submitted_trb_phys,
               (uint32_t)state,
               *enqueue,
               (uint32_t)*cycle,
               g_xhci.event_dequeue,
               (uint32_t)g_xhci.event_cycle,
               xhci_read32(g_xhci.op, XHCI_OP_USBSTS));
        xhci_save_active_controller();
        return 0;
    }
    dev->last_bulk_completion = (uint8_t)completion;
    if (completion != XHCI_CC_SUCCESS &&
        !(completion == XHCI_CC_SHORT_PACKET && (endpoint_id & 1u) != 0u)) {
        kprint("xhci: bulk failed slot=%u epid=%u cc=%u bytes=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               completion,
               bytes);
        xhci_save_active_controller();
        return 0;
    }
    xhci_save_active_controller();
    return 1;
}

static int xhci_clear_endpoint_halt(struct xhci_enum_device *dev, uint8_t endpoint_address) {
    if (dev == 0 || endpoint_address == 0u) {
        return 0;
    }
    return xhci_control_transfer(dev,
                                 0x02u,
                                 USB_REQ_CLEAR_FEATURE,
                                 USB_FEATURE_ENDPOINT_HALT,
                                 endpoint_address,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_msc_bulk_only_reset(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    return xhci_control_transfer(dev,
                                 0x21u,
                                 USB_REQ_BULK_ONLY_RESET,
                                 0u,
                                 dev->msc_interface_number,
                                 0,
                                 0u,
                                 0u);
}

static void xhci_msc_recover_transport(struct xhci_enum_device *dev, uint8_t op, const char *phase) {
    if (dev == 0) {
        return;
    }
    kprint("xhci: MSC recovery slot=%u op=%x phase=%s in=%x out=%x\n",
           (uint32_t)dev->slot_id,
           (uint32_t)op,
           phase,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_out_ep);
    (void)xhci_msc_bulk_only_reset(dev);
    xhci_delay_ms(XHCI_MSC_RESET_SETTLE_MS);
    (void)xhci_clear_endpoint_halt(dev, dev->bulk_in_ep);
    xhci_delay_ms(20u);
    (void)xhci_clear_endpoint_halt(dev, dev->bulk_out_ep);
    xhci_delay_ms(20u);
    (void)xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid);
    (void)xhci_reset_bulk_endpoint_state(dev, dev->bulk_out_epid);
    xhci_delay_ms(50u);
}

int xhci_msc_command(struct xhci_enum_device *dev,
                     const uint8_t *cmd,
                     uint8_t cmd_len,
                     void *buffer,
                     uint32_t data_len,
                     uint8_t data_in) {
    uint32_t tag;
    uint32_t signature;
    uint32_t csw_tag;
    uint8_t csw_retry = 0u;

    if (dev == 0 || cmd == 0 || cmd_len == 0u || cmd_len > 16u || data_len > XHCI_PAGE_SIZE) {
        return 0;
    }
    dev->last_msc_phase = 0u;
    dev->last_msc_status = 0u;
    dev->last_msc_residue = 0u;
    memset(dev->cbw, 0, 31u);
    memset(dev->csw, 0, 13u);
    tag = ++dev->tag;
    usb_write_u32le(dev->cbw + 0, MSC_CBW_SIGNATURE);
    usb_write_u32le(dev->cbw + 4, tag);
    usb_write_u32le(dev->cbw + 8, data_len);
    dev->cbw[12] = data_in ? 0x80u : 0u;
    dev->cbw[13] = dev->msc_lun;
    dev->cbw[14] = cmd_len;
    memcpy(dev->cbw + 15, cmd, cmd_len);
    if (buffer != 0 && data_len != 0u && !data_in) {
        memcpy(dev->data, buffer, data_len);
    }
    dev->last_msc_phase = 1u;
    if (!xhci_bulk_transfer(dev, dev->bulk_out_epid, dev->cbw_phys, 31u)) {
        dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
        kprint("xhci: MSC CBW failed slot=%u tag=%x op=%x\n",
               (uint32_t)dev->slot_id,
               tag,
               (uint32_t)cmd[0]);
        xhci_msc_recover_transport(dev, cmd[0], "CBW");
        return 0;
    }
    if (data_len != 0u) {
        if (data_in) {
            memset(dev->data, 0, data_len);
        }
        dev->last_msc_phase = 2u;
        if (!xhci_bulk_transfer(dev,
                                data_in ? dev->bulk_in_epid : dev->bulk_out_epid,
                                dev->data_phys,
                                data_len)) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            kprint("xhci: MSC DATA %s failed slot=%u tag=%x op=%x len=%u\n",
                   data_in ? "in" : "out",
                   (uint32_t)dev->slot_id,
                   tag,
                   (uint32_t)cmd[0],
                   data_len);
            xhci_msc_recover_transport(dev, cmd[0], data_in ? "DATA-IN" : "DATA-OUT");
            return 0;
        }
        if (buffer != 0 && data_in) {
            memcpy(buffer, dev->data, data_len);
        }
    }
    dev->last_msc_phase = 3u;
    for (;;) {
        memset(dev->csw, 0, 13u);
        if (xhci_bulk_transfer(dev, dev->bulk_in_epid, dev->csw_phys, 13u)) {
            break;
        }
        if (csw_retry != 0u) {
            dev->last_msc_status = MSC_STATUS_TRANSPORT_ERROR;
            kprint("xhci: MSC CSW failed slot=%u tag=%x op=%x cc=%u\n",
                   (uint32_t)dev->slot_id,
                   tag,
                   (uint32_t)cmd[0],
                   (uint32_t)dev->last_bulk_completion);
            xhci_msc_recover_transport(dev, cmd[0], "CSW");
            return 0;
        }
        (void)xhci_clear_endpoint_halt(dev, dev->bulk_in_ep);
        (void)xhci_reset_bulk_endpoint_state(dev, dev->bulk_in_epid);
        xhci_delay_ms(5u);
        csw_retry = 1u;
    }
    signature = usb_read_u32le(dev->csw);
    csw_tag = usb_read_u32le(dev->csw + 4);
    dev->last_msc_residue = usb_read_u32le(dev->csw + 8);
    dev->last_msc_status = dev->csw[12];
    if (signature != MSC_CSW_SIGNATURE || csw_tag != tag) {
        kprint("xhci: MSC CSW bad slot=%u op=%x sig=%x tag=%x/%x status=%u residue=%x\n",
               (uint32_t)dev->slot_id,
               (uint32_t)cmd[0],
               signature,
               csw_tag,
               tag,
               (uint32_t)dev->last_msc_status,
               dev->last_msc_residue);
        xhci_msc_recover_transport(dev, cmd[0], "CSW-BAD");
        return 0;
    }
    if (dev->last_msc_status == 2u) {
        kprint("xhci: MSC CSW phase error slot=%u op=%x residue=%x\n",
               (uint32_t)dev->slot_id,
               (uint32_t)cmd[0],
               dev->last_msc_residue);
        xhci_msc_recover_transport(dev, cmd[0], "CSW-PHASE");
        return 0;
    }
    if (dev->last_msc_status != 0u) {
        if (cmd[0] != SCSI_TEST_UNIT_READY &&
            cmd[0] != SCSI_READ_CAPACITY_10 &&
            cmd[0] != SCSI_REQUEST_SENSE) {
            kprint("xhci: MSC command status slot=%u op=%x status=%u residue=%x\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)cmd[0],
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
        }
        dev->last_msc_phase = 0u;
        return 0;
    }
    dev->last_msc_phase = 0u;
    return 1;
}

static int xhci_msc_read_capacity(struct xhci_enum_device *dev) {
    uint8_t cmd[10];
    uint8_t cap[8];
    uint32_t last_lba;
    uint32_t block_len;

    memset(cmd, 0, sizeof(cmd));
    memset(cap, 0, sizeof(cap));
    cmd[0] = SCSI_READ_CAPACITY_10;
    if (!xhci_msc_command(dev, cmd, 10u, cap, 8u, 1u)) {
        return 0;
    }
    last_lba = usb_read_u32be(cap);
    block_len = usb_read_u32be(cap + 4);
    if (block_len != XHCI_SECTOR_SIZE) {
        kprint("xhci: MSC unsupported block size=%x last_lba=%x\n", block_len, last_lba);
        return 0;
    }
    dev->sector_count = (uint64_t)last_lba + 1u;
    return 1;
}

static int xhci_msc_test_unit_ready(struct xhci_enum_device *dev) {
    uint8_t cmd[6];

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;
    return xhci_msc_command(dev, cmd, 6u, 0, 0u, 0u);
}

static void xhci_msc_clear_sense(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return;
    }
    dev->last_sense_key = 0u;
    dev->last_sense_asc = 0u;
    dev->last_sense_ascq = 0u;
}

static int xhci_msc_sense_response_valid(const uint8_t *sense, uint32_t length) {
    uint8_t response;

    if (sense == 0 || length < 14u) {
        return 0;
    }
    response = sense[0] & 0x7fu;
    return response == 0x70u ||
           response == 0x71u ||
           (sense[2] & 0x0fu) != 0u ||
           sense[12] != 0u ||
           sense[13] != 0u;
}

static void xhci_msc_record_sense(struct xhci_enum_device *dev, const uint8_t *sense, uint32_t length) {
    if (dev == 0 || !xhci_msc_sense_response_valid(sense, length)) {
        return;
    }
    dev->last_sense_key = sense[2] & 0x0fu;
    dev->last_sense_asc = sense[12];
    dev->last_sense_ascq = sense[13];
}

int xhci_msc_medium_not_present(const struct xhci_enum_device *dev) {
    return dev != 0 && dev->last_sense_key == 0x02u && dev->last_sense_asc == 0x3au;
}

void xhci_msc_retry_delay(uint8_t failed_phase, uint8_t failed_status) {
    if (failed_status == MSC_STATUS_TRANSPORT_ERROR ||
        failed_phase == 1u ||
        failed_phase == 2u ||
        failed_phase == 3u) {
        xhci_delay_ms(120u);
        return;
    }
    xhci_delay_ms(20u);
}

int xhci_msc_request_sense(struct xhci_enum_device *dev) {
    uint8_t cmd[6];
    uint8_t sense[18];
    int ok;

    memset(cmd, 0, sizeof(cmd));
    memset(sense, 0, sizeof(sense));
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = sizeof(sense);
    ok = xhci_msc_command(dev, cmd, 6u, sense, sizeof(sense), 1u);
    if (ok || (dev != 0 && dev->last_msc_phase == 3u && xhci_msc_sense_response_valid(sense, sizeof(sense)))) {
        xhci_msc_record_sense(dev, sense, sizeof(sense));
        return 1;
    }
    return 0;
}

static uint8_t xhci_msc_get_max_lun(struct xhci_enum_device *dev) {
    uint8_t lun = 0u;

    if (dev == 0) {
        return 0u;
    }
    if (!xhci_control_transfer(dev,
                               0xa1u,
                               USB_REQ_GET_MAX_LUN,
                               0u,
                               dev->msc_interface_number,
                               &lun,
                               1u,
                               1u)) {
        return 0u;
    }
    if (lun > USB_MSC_MAX_LUN_LIMIT) {
        return 0u;
    }
    return lun;
}

static int xhci_msc_wait_ready(struct xhci_enum_device *dev) {
    for (uint32_t i = 0u; i < 20u; i++) {
        if (xhci_msc_test_unit_ready(dev)) {
            return 1;
        }
        if (xhci_msc_request_sense(dev) && xhci_msc_medium_not_present(dev)) {
            return 0;
        }
        xhci_delay_ms(100u);
    }
    return 0;
}

static int xhci_msc_probe_device(struct xhci_enum_device *dev) {
    uint8_t cmd[16];
    uint8_t inquiry[36];

    dev->max_lun = xhci_msc_get_max_lun(dev);
    kprint("xhci: MSC probe slot=%u if=%u maxlun=%u in=%x/%u out=%x/%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev->msc_interface_number,
           (uint32_t)dev->max_lun,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_in_mps,
           (uint32_t)dev->bulk_out_ep,
           (uint32_t)dev->bulk_out_mps);

    for (uint8_t lun = 0u; lun <= dev->max_lun; lun++) {
        uint8_t inquiry_ok = 0u;

        dev->msc_lun = lun;
        xhci_msc_clear_sense(dev);
        memset(cmd, 0, sizeof(cmd));
        memset(inquiry, 0, sizeof(inquiry));
        cmd[0] = SCSI_INQUIRY;
        cmd[4] = sizeof(inquiry);
        for (uint32_t i = 0u; i < 5u; i++) {
            if (xhci_msc_command(dev, cmd, 6u, inquiry, sizeof(inquiry), 1u)) {
                inquiry_ok = 1u;
                break;
            }
            xhci_delay_ms(100u);
        }
        if (!inquiry_ok) {
            kprint("xhci: MSC lun%u inquiry failed phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        kprint("xhci: MSC lun%u inquiry type=%x removable=%u\n",
               (uint32_t)lun,
               (uint32_t)(inquiry[0] & 0x1fu),
               (uint32_t)((inquiry[1] & 0x80u) != 0u));
        if (!xhci_msc_wait_ready(dev)) {
            kprint("xhci: MSC lun%u not ready sense=%x/%x/%x phase=%u status=%u residue=%x\n",
                   (uint32_t)lun,
                   (uint32_t)dev->last_sense_key,
                   (uint32_t)dev->last_sense_asc,
                   (uint32_t)dev->last_sense_ascq,
                   (uint32_t)dev->last_msc_phase,
                   (uint32_t)dev->last_msc_status,
                   dev->last_msc_residue);
            continue;
        }
        for (uint32_t i = 0u; i < 10u; i++) {
            if (xhci_msc_read_capacity(dev)) {
                return 1;
            }
            if (xhci_msc_request_sense(dev) && xhci_msc_medium_not_present(dev)) {
                break;
            }
            xhci_delay_ms(100u);
        }
        kprint("xhci: MSC lun%u read capacity failed sense=%x/%x/%x phase=%u status=%u residue=%x\n",
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


void xhci_probe_msc(struct xhci_enum_device *dev) {
    if (g_xhci_msc_count >= XHCI_MAX_ENUM_DEVICES) {
        return;
    }
    if (!xhci_alloc_msc_resources(dev)) {
        kprint("xhci: slot%u MSC resource allocation failed\n", (uint32_t)dev->slot_id);
        return;
    }
    if (!xhci_control_set_configuration(dev, dev->configuration)) {
        kprint("xhci: slot%u MSC set config failed\n", (uint32_t)dev->slot_id);
        return;
    }
    xhci_delay_ms(XHCI_MSC_CONFIG_SETTLE_MS);
    if (!xhci_configure_bulk_endpoints(dev)) {
        kprint("xhci: slot%u MSC endpoint config failed in=%u out=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)dev->bulk_in_epid,
               (uint32_t)dev->bulk_out_epid);
        return;
    }
    xhci_delay_ms(50u);
    dev->tag = 0x56780000u;
    if (!xhci_msc_probe_device(dev)) {
        kprint("xhci: slot%u MSC probe failed\n", (uint32_t)dev->slot_id);
        return;
    }
    if (!xhci_msc_register_blockdev(dev)) {
        return;
    }
    kprint("xhci: slot%u %s sectors=%lx in=%x out=%x\n",
           (uint32_t)dev->slot_id,
           dev->name,
           dev->sector_count,
           (uint32_t)dev->bulk_in_ep,
           (uint32_t)dev->bulk_out_ep);
    g_xhci_msc_count++;
}
