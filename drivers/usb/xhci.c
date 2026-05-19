#include "drivers/usb/xhci_internal.h"


static void xhci_ring_command(uint64_t parameter, uint32_t status, uint32_t control) {
    struct xhci_trb *trb = &g_xhci.command_ring[g_xhci.command_enqueue];

    xhci_ring_trb_set(trb, parameter, status, control | g_xhci.command_cycle);
    g_xhci.command_enqueue++;
    if (g_xhci.command_enqueue >= XHCI_RING_TRBS - 1u) {
        g_xhci.command_ring[XHCI_RING_TRBS - 1u].control =
            (XHCI_TRB_LINK << 10) | 2u | (g_xhci.command_cycle != 0u ? 1u : 0u);
        g_xhci.command_cycle ^= 1u;
        g_xhci.command_enqueue = 0u;
    }
    xhci_write32(g_xhci.doorbell, 0u, 0u);
}

static int xhci_pop_event(struct xhci_trb *out) {
    struct xhci_trb *event = &g_xhci.event_ring[g_xhci.event_dequeue];
    uint32_t control = event->control;

    if (out == 0 || (control & 1u) != g_xhci.event_cycle) {
        return 0;
    }
    *out = *event;
    g_xhci.event_dequeue++;
    if (g_xhci.event_dequeue >= XHCI_RING_TRBS) {
        g_xhci.event_dequeue = 0u;
        g_xhci.event_cycle ^= 1u;
    }
    xhci_write64(g_xhci.runtime + 0x20u, XHCI_INTR_ERDP,
                 g_xhci.event_ring_phys + g_xhci.event_dequeue * XHCI_TRB_SIZE + XHCI_ERDP_EHB);
    return 1;
}

static int xhci_wait_command_completion(uint8_t *slot_id_out, uint32_t *completion_out) {
    for (uint32_t spin = 0; spin < XHCI_WAIT_SPINS_DEFAULT; spin++) {
        struct xhci_trb event;
        uint32_t control;

        if (!xhci_pop_event(&event)) {
            continue;
        }
        control = event.control;
        if (xhci_trb_type(control) == XHCI_TRB_TRANSFER_EVENT) {
            uint8_t event_slot_id = (uint8_t)((control >> 24) & 0xffu);
            uint8_t event_endpoint_id = (uint8_t)((control >> 16) & 0x1fu);
            uint32_t completion = (event.status >> 24) & 0xffu;

            (void)xhci_hid_defer_transfer_event(event_slot_id, event_endpoint_id, completion);
            continue;
        }
        if (xhci_trb_type(control) != XHCI_TRB_COMMAND_COMPLETION) {
            continue;
        }
        if (slot_id_out != 0) {
            *slot_id_out = (uint8_t)((control >> 24) & 0xffu);
        }
        if (completion_out != 0) {
            *completion_out = (event.status >> 24) & 0xffu;
        }
        return 1;
    }
    return 0;
}

int xhci_wait_transfer_event_spins(uint8_t slot_id,
                                   uint8_t endpoint_id,
                                   uint32_t *completion_out,
                                   uint64_t expected_trb_phys,
                                   uint32_t max_spins) {
    for (uint32_t spin = 0; spin < max_spins; spin++) {
        struct xhci_trb event;
        uint32_t control;

        if (!xhci_pop_event(&event)) {
            continue;
        }
        control = event.control;
        if (xhci_trb_type(control) != XHCI_TRB_TRANSFER_EVENT) {
            continue;
        }
        {
            uint8_t event_slot_id = (uint8_t)((control >> 24) & 0xffu);
            uint8_t event_endpoint_id = (uint8_t)((control >> 16) & 0x1fu);
            uint32_t completion = (event.status >> 24) & 0xffu;
            uint64_t event_trb_phys = ((uint64_t)event.parameter_hi << 32) | event.parameter_lo;

            if (event_slot_id == slot_id && event_endpoint_id == endpoint_id) {
                if (expected_trb_phys != 0u && event_trb_phys != expected_trb_phys) {
                    continue;
                }
                if (completion_out != 0) {
                    *completion_out = completion;
                }
                return 1;
            }
            (void)xhci_hid_defer_transfer_event(event_slot_id, event_endpoint_id, completion);
            continue;
        }
    }
    return 0;
}

static int xhci_command_enable_slot(uint8_t *slot_id_out) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    xhci_ring_command(0u, 0u, XHCI_TRB_ENABLE_SLOT << 10);
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS ||
        slot_id == 0u) {
        kprint("xhci: enable slot failed cc=%u slot=%u\n", completion, (uint32_t)slot_id);
        return 0;
    }
    if (slot_id_out != 0) {
        *slot_id_out = slot_id;
    }
    return 1;
}

static int xhci_command_address_device(struct xhci_enum_device *dev) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    xhci_ring_command(dev->input_context_phys,
                      0u,
                      (XHCI_TRB_ADDRESS_DEVICE << 10) | ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: address device failed port=%u slot=%u cc=%u event_slot=%u\n",
               (uint32_t)dev->port,
               (uint32_t)dev->slot_id,
               completion,
               (uint32_t)slot_id);
        return 0;
    }
    return 1;
}

int xhci_command_context(uint8_t command_type, struct xhci_enum_device *dev) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    xhci_ring_command(dev->input_context_phys,
                      0u,
                      (command_type << 10) | ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: context command %u failed slot=%u cc=%u event_slot=%u\n",
               command_type,
               (uint32_t)dev->slot_id,
               completion,
               (uint32_t)slot_id);
        return 0;
    }
    return 1;
}

static uint32_t xhci_ep0_max_packet_for_speed(uint8_t speed) {
    if (speed == XHCI_SPEED_LOW || speed == XHCI_SPEED_FULL) {
        return 8u;
    }
    if (speed >= XHCI_SPEED_SUPER) {
        return 512u;
    }
    return 64u;
}

static uint32_t xhci_ep0_max_packet_from_descriptor(uint8_t speed, uint8_t mps0) {
    if (speed >= XHCI_SPEED_SUPER) {
        return mps0 < 16u ? (1u << mps0) : 512u;
    }
    if (mps0 == 8u || mps0 == 16u || mps0 == 32u || mps0 == 64u) {
        return mps0;
    }
    return xhci_ep0_max_packet_for_speed(speed);
}

static int xhci_update_ep0_max_packet(struct xhci_enum_device *dev, uint32_t max_packet) {
    uint8_t *input_control;
    uint8_t *ep0;
    uint8_t *out_ep0;
    uint32_t *ic;
    uint32_t *ep0_ctx;

    if (dev == 0 || max_packet == 0u || max_packet > 512u) {
        return 0;
    }
    input_control = xhci_context_ptr(dev->input_context, 0u);
    ep0 = xhci_context_ptr(dev->input_context, 2u);
    out_ep0 = xhci_context_ptr(dev->device_context, 1u);
    ic = (uint32_t *)input_control;
    ep0_ctx = (uint32_t *)ep0;

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(ep0, out_ep0, g_xhci.context_size);
    ic[1] = XHCI_EP0_FLAG;
    ep0_ctx[1] = (ep0_ctx[1] & ~(0xffffu << 16)) | (max_packet << 16);
    if (!xhci_command_context(XHCI_TRB_EVALUATE_CONTEXT, dev)) {
        kprint("xhci: slot%u EP0 mps update failed mps=%u\n",
               (uint32_t)dev->slot_id,
               max_packet);
        return 0;
    }
    return 1;
}

static void xhci_prepare_address_context(struct xhci_enum_device *dev) {
    uint8_t *input_control = xhci_context_ptr(dev->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(dev->input_context, 1u);
    uint8_t *ep0 = xhci_context_ptr(dev->input_context, 2u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;
    uint32_t *ep0_ctx = (uint32_t *)ep0;
    uint32_t max_packet = xhci_ep0_max_packet_for_speed(dev->speed);

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memset(dev->device_context, 0, XHCI_PAGE_SIZE);
    memset(dev->ep0_ring, 0, XHCI_PAGE_SIZE);
    xhci_ring_trb_set(&dev->ep0_ring[XHCI_RING_TRBS - 1u],
                      dev->ep0_ring_phys,
                      0u,
                      (XHCI_TRB_LINK << 10) | 2u | 1u);
    dev->ep0_enqueue = 0u;
    dev->ep0_cycle = 1u;
    ic[1] = XHCI_SLOT_FLAG | XHCI_EP0_FLAG;
    slot_ctx[0] = (dev->route_string & 0xfffffu) |
                  ((uint32_t)dev->speed << 20) |
                  XHCI_SLOT_LAST_CTX_1;
    slot_ctx[1] = (uint32_t)dev->port << 16;
    if (dev->parent_slot_id != 0u &&
        (dev->speed == XHCI_SPEED_FULL || dev->speed == XHCI_SPEED_LOW)) {
        slot_ctx[2] = ((uint32_t)dev->parent_slot_id) |
                      ((uint32_t)dev->parent_port << 8);
    }
    ep0_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (4u << 3) | (max_packet << 16);
    ep0_ctx[2] = (uint32_t)dev->ep0_ring_phys | 1u;
    ep0_ctx[3] = (uint32_t)(dev->ep0_ring_phys >> 32);
    ep0_ctx[4] = 8u;
}

static void xhci_ep0_ring_trb(struct xhci_enum_device *dev,
                              uint64_t parameter,
                              uint32_t status,
                              uint32_t control) {
    struct xhci_trb *trb = &dev->ep0_ring[dev->ep0_enqueue];

    xhci_ring_trb_set(trb, parameter, status, control | dev->ep0_cycle);
    dev->ep0_enqueue++;
    if (dev->ep0_enqueue >= XHCI_RING_TRBS - 1u) {
        dev->ep0_ring[XHCI_RING_TRBS - 1u].control =
            (XHCI_TRB_LINK << 10) | 2u | (dev->ep0_cycle != 0u ? 1u : 0u);
        dev->ep0_cycle ^= 1u;
        dev->ep0_enqueue = 0u;
    }
}

uint64_t xhci_transfer_ring_trb(struct xhci_trb *ring,
                                uint64_t ring_phys,
                                uint32_t *enqueue,
                                uint8_t *cycle,
                                uint64_t parameter,
                                uint32_t status,
                                uint32_t control) {
    struct xhci_trb *trb = &ring[*enqueue];
    uint64_t trb_phys = ring_phys + (uint64_t)(*enqueue) * XHCI_TRB_SIZE;

    xhci_ring_trb_set(trb, parameter, status, control | *cycle);
    (*enqueue)++;
    if (*enqueue >= XHCI_RING_TRBS - 1u) {
        ring[XHCI_RING_TRBS - 1u].parameter_lo = (uint32_t)ring_phys;
        ring[XHCI_RING_TRBS - 1u].parameter_hi = (uint32_t)(ring_phys >> 32);
        ring[XHCI_RING_TRBS - 1u].status = 0u;
        ring[XHCI_RING_TRBS - 1u].control = (XHCI_TRB_LINK << 10) | 2u | (*cycle != 0u ? 1u : 0u);
        *cycle ^= 1u;
        *enqueue = 0u;
    }
    return trb_phys;
}

static uint64_t xhci_setup_packet(uint8_t request_type,
                                  uint8_t request,
                                  uint16_t value,
                                  uint16_t index,
                                  uint16_t length) {
    return (uint64_t)request_type |
           ((uint64_t)request << 8) |
           ((uint64_t)value << 16) |
           ((uint64_t)index << 32) |
           ((uint64_t)length << 48);
}

int xhci_control_transfer(struct xhci_enum_device *dev,
                          uint8_t request_type,
                          uint8_t request,
                          uint16_t value,
                          uint16_t index,
                          void *buffer,
                          uint16_t length,
                          uint8_t data_in) {
    uint64_t setup;
    uint32_t completion = 0u;
    uint32_t setup_type;
    uint32_t status_control;
    uint32_t wait_spins;
    uint8_t quiet_failure;

    if (dev == 0 || length > XHCI_PAGE_SIZE || (length != 0u && buffer == 0)) {
        return 0;
    }
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    memset(dev->descriptor, 0, XHCI_PAGE_SIZE);
    if (buffer != 0 && length != 0u && !data_in) {
        memcpy(dev->descriptor, buffer, length);
    }
    setup_type = length == 0u ? 0u : (data_in ? 3u : 2u);
    setup = xhci_setup_packet(request_type,
                              request,
                              value,
                              index,
                              length);
    xhci_ep0_ring_trb(dev,
                      setup,
                      8u,
                      (XHCI_TRB_SETUP_STAGE << 10) | (1u << 6) | (setup_type << 16));
    if (length != 0u) {
        xhci_ep0_ring_trb(dev,
                          dev->descriptor_phys,
                          length,
                          (XHCI_TRB_DATA_STAGE << 10) | (data_in ? (1u << 16) : 0u));
    }
    status_control = (XHCI_TRB_STATUS_STAGE << 10) | (1u << 5);
    if (!data_in) {
        status_control |= 1u << 16;
    }
    xhci_ep0_ring_trb(dev,
                      0u,
                      0u,
                      status_control);
    xhci_write32(g_xhci.doorbell, (uint32_t)dev->slot_id * 4u, 1u);
    wait_spins = request_type == 0xa1u && request == USB_REQ_GET_REPORT
                     ? XHCI_HID_REPORT_WAIT_SPINS
                     : XHCI_WAIT_SPINS_DEFAULT;
    if (request_type == 0xa3u && request == USB_REQ_GET_STATUS) {
        wait_spins = XHCI_HUB_STATUS_WAIT_SPINS;
    }
    quiet_failure = (request_type == 0xa1u && request == USB_REQ_GET_REPORT) ||
                    (request_type == 0xa3u && request == USB_REQ_GET_STATUS);
    if (!xhci_wait_transfer_event_spins(dev->slot_id, 1u, &completion, 0u, wait_spins) ||
        completion != XHCI_CC_SUCCESS) {
        if (!quiet_failure) {
            kprint("xhci: control failed port=%u slot=%u req=%u type=%x cc=%u\n",
                   (uint32_t)dev->port,
                   (uint32_t)dev->slot_id,
                   (uint32_t)request,
                   (uint32_t)request_type,
                   completion);
        }
        xhci_save_active_controller();
        return 0;
    }
    if (buffer != 0 && length != 0u && data_in) {
        memcpy(buffer, dev->descriptor, length);
    }
    xhci_save_active_controller();
    return 1;
}

static int xhci_control_get_descriptor(struct xhci_enum_device *dev,
                                       uint8_t desc_type,
                                       uint8_t desc_index,
                                       void *buffer,
                                       uint16_t length) {
    for (uint32_t attempt = 0u; attempt < XHCI_DESCRIPTOR_RETRIES; attempt++) {
        if (xhci_control_transfer(dev,
                                  0x80u,
                                  USB_REQ_GET_DESCRIPTOR,
                                  (uint16_t)(((uint16_t)desc_type << 8) | desc_index),
                                  0u,
                                  buffer,
                                  length,
                                  1u)) {
            return 1;
        }
        xhci_delay_ms(10u);
    }
    return 0;
}

int xhci_control_set_configuration(struct xhci_enum_device *dev, uint8_t configuration) {
    return xhci_control_transfer(dev,
                                 0x00u,
                                 USB_REQ_SET_CONFIGURATION,
                                 configuration,
                                 0u,
                                 0,
                                 0u,
                                 0u);
}


int xhci_command_endpoint(uint8_t command_type,
                                 struct xhci_enum_device *dev,
                                 uint8_t endpoint_id,
                                 uint64_t parameter) {
    uint32_t completion = 0u;
    uint8_t slot_id = 0u;

    if (dev == 0 || endpoint_id == 0u) {
        return 0;
    }
    if (!xhci_select_controller(dev->controller_index)) {
        return 0;
    }
    xhci_ring_command(parameter,
                      0u,
                      ((uint32_t)command_type << 10) |
                      ((uint32_t)endpoint_id << 16) |
                      ((uint32_t)dev->slot_id << 24));
    if (!xhci_wait_command_completion(&slot_id, &completion) ||
        completion != XHCI_CC_SUCCESS) {
        kprint("xhci: endpoint command %u failed slot=%u epid=%u cc=%u event_slot=%u\n",
               (uint32_t)command_type,
               (uint32_t)dev->slot_id,
               (uint32_t)endpoint_id,
               completion,
               (uint32_t)slot_id);
        xhci_save_active_controller();
        return 0;
    }
    xhci_save_active_controller();
    return 1;
}


static void xhci_probe_descriptors(struct xhci_enum_device *dev) {
    uint8_t dev_desc[18];
    uint8_t cfg_head[9];
    uint8_t cfg[256];
    uint16_t total_len;

    memset(dev_desc, 0, sizeof(dev_desc));
    if (!xhci_control_get_descriptor(dev, 1u, 0u, dev_desc, 8u)) {
        kprint("xhci: slot%u device desc8 failed\n", (uint32_t)dev->slot_id);
        return;
    }
    if (!xhci_update_ep0_max_packet(dev, xhci_ep0_max_packet_from_descriptor(dev->speed, dev_desc[7]))) {
        return;
    }
    if (!xhci_control_get_descriptor(dev, 1u, 0u, dev_desc, sizeof(dev_desc))) {
        kprint("xhci: slot%u device desc18 failed\n", (uint32_t)dev->slot_id);
        return;
    }
    kprint("xhci: slot%u device desc class=%u subclass=%u proto=%u mps0=%u vendor=%x product=%x configs=%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev_desc[4],
           (uint32_t)dev_desc[5],
           (uint32_t)dev_desc[6],
           (uint32_t)dev_desc[7],
           (uint32_t)usb_read_u16le(dev_desc + 8),
           (uint32_t)usb_read_u16le(dev_desc + 10),
           (uint32_t)dev_desc[17]);

    memset(cfg_head, 0, sizeof(cfg_head));
    if (!xhci_control_get_descriptor(dev, 2u, 0u, cfg_head, sizeof(cfg_head))) {
        kprint("xhci: slot%u config head failed\n", (uint32_t)dev->slot_id);
        return;
    }
    total_len = usb_read_u16le(cfg_head + 2);
    if (total_len > sizeof(cfg)) {
        total_len = sizeof(cfg);
    }
    memset(cfg, 0, sizeof(cfg));
    if (!xhci_control_get_descriptor(dev, 2u, 0u, cfg, total_len)) {
        return;
    }
    kprint("xhci: slot%u config desc total=%u interfaces=%u config=%u attrs=%x max_power=%u\n",
           (uint32_t)dev->slot_id,
           (uint32_t)total_len,
           (uint32_t)cfg[4],
           (uint32_t)cfg[5],
           (uint32_t)cfg[7],
           (uint32_t)cfg[8]);
    if (xhci_parse_msc_config(dev, cfg, total_len)) {
        xhci_probe_msc(dev);
        return;
    }
    if (xhci_probe_hid_mouse(dev, cfg, total_len)) {
        return;
    }
    if (xhci_probe_hid_keyboard(dev, cfg, total_len)) {
        return;
    }
    if (dev_desc[4] == USB_CLASS_HUB || xhci_config_has_hub_interface(cfg, total_len)) {
        xhci_probe_hub(dev, cfg, total_len);
    }
}


int xhci_enumerate_device(struct xhci_enum_device *dev) {
    if (dev == 0) {
        return 0;
    }
    if (!xhci_alloc_enum_device(dev) || !xhci_command_enable_slot(&dev->slot_id)) {
        return 0;
    }
    g_xhci.dcbaa[dev->slot_id] = dev->device_context_phys;
    xhci_prepare_address_context(dev);
    if (!xhci_command_address_device(dev)) {
        return 0;
    }
    xhci_delay_ms(XHCI_ADDRESS_SETTLE_MS);
    dev->used = 1u;
    if (dev->parent_slot_id != 0u) {
        kprint("xhci: enumerated hubslot=%u hubport=%u slot=%u rootport=%u route=%x speed=%u\n",
               (uint32_t)dev->parent_slot_id,
               (uint32_t)dev->parent_port,
               (uint32_t)dev->slot_id,
               (uint32_t)dev->port,
               dev->route_string,
               (uint32_t)dev->speed);
    } else {
        kprint("xhci: enumerated port%u slot=%u speed=%u devctx=%x input=%x\n",
               (uint32_t)dev->port,
               (uint32_t)dev->slot_id,
               (uint32_t)dev->speed,
               (uint32_t)dev->device_context_phys,
               (uint32_t)dev->input_context_phys);
    }
    xhci_probe_descriptors(dev);
    return 1;
}



uint32_t xhci_hid_keyboard_count(void) {
    uint32_t count = 0u;

    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        if (g_hid_keyboards[i].present) {
            count++;
        }
    }
    return count;
}

int xhci_poll_keyboard_event(struct keyboard_event *out) {
    uint8_t report[8];
    int result = 0;

    if (out == 0) {
        return 0;
    }
    if (!xhci_try_begin_busy()) {
        return 0;
    }
    if (xhci_hid_pop_event(out)) {
        result = 1;
        goto done;
    }
    xhci_hotplug_poll();
    if (xhci_hid_pop_event(out)) {
        result = 1;
        goto done;
    }
    if (g_hid_keyboard_count == 0u) {
        goto done;
    }
    for (uint32_t i = 0u; i < g_hid_keyboard_count; i++) {
        struct xhci_hid_keyboard *kbd = &g_hid_keyboards[i];

        if (!kbd->present || kbd->poll_disabled) {
            continue;
        }
        memset(report, 0, sizeof(report));
        if (!xhci_hid_poll_interrupt_report(kbd, report)) {
            continue;
        }
        kbd->report_fail_logged = 0u;
        kbd->poll_fail_count = 0u;
        xhci_hid_process_report(kbd, report);
        if (xhci_hid_pop_event(out)) {
            result = 1;
            goto done;
        }
    }
    xhci_hid_tick_repeats_once();
    if (xhci_hid_pop_event(out)) {
        result = 1;
        goto done;
    }
done:
    xhci_end_busy();
    return result;
}
