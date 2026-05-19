#include "drivers/usb/xhci_internal.h"

int xhci_config_has_hub_interface(const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;

    if (cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u && cfg[offset + 5u] == USB_CLASS_HUB) {
            return 1;
        }
        offset += len;
    }
    return 0;
}

static int xhci_hub_get_descriptor(struct xhci_enum_device *hub,
                                   uint8_t desc_type,
                                   uint8_t *buffer,
                                   uint16_t length) {
    return xhci_control_transfer(hub,
                                 0xa0u,
                                 USB_REQ_GET_DESCRIPTOR,
                                 (uint16_t)(desc_type << 8),
                                 0u,
                                 buffer,
                                 length,
                                 1u);
}

static int xhci_hub_set_port_feature(struct xhci_enum_device *hub, uint8_t port, uint16_t feature) {
    return xhci_control_transfer(hub,
                                 0x23u,
                                 USB_REQ_SET_FEATURE,
                                 feature,
                                 port,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_hub_clear_port_feature(struct xhci_enum_device *hub, uint8_t port, uint16_t feature) {
    return xhci_control_transfer(hub,
                                 0x23u,
                                 USB_REQ_CLEAR_FEATURE,
                                 feature,
                                 port,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_hub_get_port_status(struct xhci_enum_device *hub,
                                    uint8_t port,
                                    uint16_t *status_out,
                                    uint16_t *change_out) {
    uint8_t data[4];

    memset(data, 0, sizeof(data));
    if (!xhci_control_transfer(hub,
                               0xa3u,
                               USB_REQ_GET_STATUS,
                               0u,
                               port,
                               data,
                               sizeof(data),
                               1u)) {
        return 0;
    }
    if (status_out != 0) {
        *status_out = usb_read_u16le(data);
    }
    if (change_out != 0) {
        *change_out = usb_read_u16le(data + 2);
    }
    return 1;
}

static uint8_t xhci_hub_child_speed(struct xhci_enum_device *hub, uint16_t status) {
    if ((status & USB_HUB_PORT_LOW_SPEED) != 0u) {
        return XHCI_SPEED_LOW;
    }
    if ((status & USB_HUB_PORT_HIGH_SPEED) != 0u) {
        return XHCI_SPEED_HIGH;
    }
    if (hub->speed >= XHCI_SPEED_SUPER) {
        return XHCI_SPEED_SUPER;
    }
    return XHCI_SPEED_FULL;
}

static void xhci_hub_clear_changes(struct xhci_enum_device *hub, uint8_t port, uint16_t change) {
    if ((change & USB_HUB_PORT_CONNECTION) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_CONNECTION);
    }
    if ((change & USB_HUB_PORT_ENABLE) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_ENABLE);
    }
    if ((change & USB_HUB_PORT_RESET) != 0u) {
        (void)xhci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_RESET);
    }
}

static void xhci_prepare_hub_context(struct xhci_enum_device *hub, uint8_t port_count) {
    uint8_t *input_control = xhci_context_ptr(hub->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(hub->input_context, 1u);
    uint8_t *out_slot = xhci_context_ptr(hub->device_context, 0u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;

    memset(hub->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(slot, out_slot, g_xhci.context_size);
    ic[1] = XHCI_SLOT_FLAG;
    slot_ctx[0] |= XHCI_SLOT_HUB;
    slot_ctx[1] = (slot_ctx[1] & ~(0xffu << 24)) | ((uint32_t)port_count << 24);
}

static int xhci_update_hub_context(struct xhci_enum_device *hub, uint8_t port_count) {
    xhci_prepare_hub_context(hub, port_count);
    if (xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, hub)) {
        return 1;
    }
    xhci_prepare_hub_context(hub, port_count);
    return xhci_command_context(XHCI_TRB_EVALUATE_CONTEXT, hub);
}

static int xhci_enumerate_hub_port(struct xhci_enum_device *hub, uint8_t port) {
    struct xhci_enum_device *child;
    uint16_t status = 0u;
    uint16_t change = 0u;

    if (hub->route_depth >= 5u || port == 0u || port > 15u) {
        return 0;
    }
    if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
        kprint("xhci: hub slot%u port%u status failed\n", (uint32_t)hub->slot_id, (uint32_t)port);
        return 0;
    }
    xhci_hub_clear_changes(hub, port, change);
    if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
        return 0;
    }
    kprint("xhci: hub slot%u port%u connected status=%x change=%x\n",
           (uint32_t)hub->slot_id, (uint32_t)port, (uint32_t)status, (uint32_t)change);
    if (!xhci_hub_set_port_feature(hub, port, USB_HUB_FEATURE_PORT_RESET)) {
        kprint("xhci: hub slot%u port%u reset failed\n", (uint32_t)hub->slot_id, (uint32_t)port);
        return 0;
    }
    xhci_delay_ms(80u);
    for (uint32_t i = 0; i < 200u; i++) {
        if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
            return 0;
        }
        if ((status & USB_HUB_PORT_RESET) == 0u) {
            break;
        }
        xhci_delay_ms(1u);
    }
    xhci_hub_clear_changes(hub, port, change);
    if (!xhci_hub_get_port_status(hub, port, &status, &change) ||
        (status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        kprint("xhci: hub slot%u port%u enable failed status=%x change=%x\n",
               (uint32_t)hub->slot_id, (uint32_t)port, (uint32_t)status, (uint32_t)change);
        return 0;
    }

    child = xhci_alloc_device_record();
    if (child == 0) {
        kprint("xhci: hub slot%u port%u no device slots\n", (uint32_t)hub->slot_id, (uint32_t)port);
        return 0;
    }
    memset(child, 0, sizeof(*child));
    child->port = hub->port;
    child->controller_index = hub->controller_index;
    child->speed = xhci_hub_child_speed(hub, status);
    child->route_depth = (uint8_t)(hub->route_depth + 1u);
    child->route_string = hub->route_string | ((uint32_t)port << (hub->route_depth * 4u));
    child->parent_slot_id = hub->slot_id;
    child->parent_port = port;
    kprint("xhci: hub slot%u port%u route=%x speed=%u\n",
           (uint32_t)hub->slot_id,
           (uint32_t)port,
           child->route_string,
           (uint32_t)child->speed);
    return xhci_enumerate_device(child);
}

void xhci_probe_hub(struct xhci_enum_device *dev, const uint8_t *cfg, uint16_t cfg_len) {
    uint8_t hub_desc[32];
    uint8_t primary_desc;
    uint8_t fallback_desc;
    uint8_t port_count;
    uint32_t pwr_ms;

    (void)cfg_len;
    if (!xhci_control_set_configuration(dev, cfg[5])) {
        kprint("xhci: hub slot%u set config failed\n", (uint32_t)dev->slot_id);
        return;
    }
    memset(hub_desc, 0, sizeof(hub_desc));
    primary_desc = dev->speed >= XHCI_SPEED_SUPER ? USB_DESC_SS_HUB : USB_DESC_HUB;
    fallback_desc = primary_desc == USB_DESC_HUB ? USB_DESC_SS_HUB : USB_DESC_HUB;
    if (!xhci_hub_get_descriptor(dev, primary_desc, hub_desc, primary_desc == USB_DESC_SS_HUB ? 12u : 8u) &&
        !xhci_hub_get_descriptor(dev, fallback_desc, hub_desc, fallback_desc == USB_DESC_SS_HUB ? 12u : 8u)) {
        kprint("xhci: hub slot%u descriptor failed\n", (uint32_t)dev->slot_id);
        return;
    }
    port_count = hub_desc[2];
    if (port_count > XHCI_MAX_HUB_PORTS) {
        port_count = XHCI_MAX_HUB_PORTS;
    }
    dev->hub_port_count = port_count;
    pwr_ms = (uint32_t)hub_desc[5] * 2u;
    if (pwr_ms < 100u) {
        pwr_ms = 100u;
    }
    kprint("xhci: hub slot%u rootport=%u ports=%u pwr=%u desc=%x\n",
           (uint32_t)dev->slot_id,
           (uint32_t)dev->port,
           (uint32_t)port_count,
           pwr_ms,
           (uint32_t)hub_desc[1]);
    if (!xhci_update_hub_context(dev, port_count)) {
        kprint("xhci: hub slot%u context update failed\n", (uint32_t)dev->slot_id);
        return;
    }
    for (uint8_t port = 1u; port <= port_count; port++) {
        (void)xhci_hub_set_port_feature(dev, port, USB_HUB_FEATURE_PORT_POWER);
    }
    xhci_delay_ms(pwr_ms);
    for (uint8_t port = 1u; port <= port_count; port++) {
        uint16_t status = 0u;
        uint16_t change = 0u;

        if (xhci_hub_get_port_status(dev, port, &status, &change)) {
            kprint("xhci: hub slot%u port%u status=%x change=%x\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)port,
                   (uint32_t)status,
                   (uint32_t)change);
        }
        (void)xhci_enumerate_hub_port(dev, port);
    }
}

static void xhci_reset_port(uint32_t port) {
    uint32_t offset = XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE;
    uint32_t portsc = xhci_read32(g_xhci.op, offset);

    if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) != 0u) {
        return;
    }
    xhci_write32(g_xhci.op, offset, (portsc & XHCI_PORTSC_RW_PRESERVE) | XHCI_PORTSC_PR);
    for (uint32_t i = 0; i < 1000000u; i++) {
        portsc = xhci_read32(g_xhci.op, offset);
        if ((portsc & XHCI_PORTSC_PR) == 0u) {
            break;
        }
    }
}

static uint32_t xhci_root_port_offset(uint32_t port) {
    return XHCI_OP_PORT_BASE + (port - 1u) * XHCI_PORT_REG_STRIDE;
}

static void xhci_clear_root_port_changes(uint32_t port, uint32_t portsc) {
    uint32_t changes;

    if (port == 0u || port >= XHCI_MAX_ROOT_PORTS) {
        return;
    }
    changes = portsc & XHCI_PORTSC_CHANGE_BITS;
    if (changes == 0u) {
        return;
    }
    xhci_write32(g_xhci.op,
                 xhci_root_port_offset(port),
                 (portsc & XHCI_PORTSC_RW_PRESERVE) | changes);
}

static struct xhci_enum_device *xhci_find_root_port_device(uint8_t controller_index, uint8_t port) {
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == controller_index &&
            dev->parent_slot_id == 0u &&
            dev->port == port) {
            return dev;
        }
    }
    return 0;
}

static struct xhci_enum_device *xhci_find_hub_child(struct xhci_enum_device *hub, uint8_t port) {
    if (hub == 0 || hub->slot_id == 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == hub->controller_index &&
            dev->parent_slot_id == hub->slot_id &&
            dev->parent_port == port) {
            return dev;
        }
    }
    return 0;
}

static void xhci_mark_device_detached(struct xhci_enum_device *dev) {
    uint8_t slot_id;

    if (dev == 0 || !dev->used) {
        return;
    }
    slot_id = dev->slot_id;
    if (dev->blockdev.driver_data == dev) {
        (void)blockdev_unregister(&dev->blockdev);
    }
    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        struct xhci_hid_keyboard *kbd = &g_hid_keyboards[i];

        if (kbd->dev == dev) {
            xhci_hid_release_all_keys(kbd);
            kbd->present = 0u;
            kbd->poll_disabled = 1u;
            kbd->interrupt_pending = 0u;
            kbd->report_queue_head = 0u;
            kbd->report_queue_tail = 0u;
            kbd->report_queue_count = 0u;
        }
    }
    for (uint32_t i = 0u; i < g_hid_mouse_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        struct xhci_hid_keyboard *mouse = &g_hid_mice[i];

        if (mouse->dev == dev) {
            mouse->present = 0u;
            mouse->poll_disabled = 1u;
            mouse->interrupt_pending = 0u;
            mouse->report_queue_head = 0u;
            mouse->report_queue_tail = 0u;
            mouse->report_queue_count = 0u;
        }
    }
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        if (g_enum_devices[i].used &&
            g_enum_devices[i].controller_index == dev->controller_index &&
            g_enum_devices[i].parent_slot_id == slot_id) {
            xhci_mark_device_detached(&g_enum_devices[i]);
        }
    }
    if (dev->parent_slot_id == 0u) {
        g_xhci.root_port_slots[dev->port] = 0u;
    }
    if (slot_id != 0u && slot_id <= g_xhci.max_slots) {
        g_xhci.dcbaa[slot_id] = 0u;
    }
    dev->used = 0u;
    dev->slot_id = 0u;
    dev->hub_port_count = 0u;
}

static int xhci_root_port_is_tracked(uint8_t port) {
    if (g_xhci.root_port_slots[port] != 0u) {
        return 1;
    }
    return xhci_find_root_port_device(g_xhci_active_controller, port) != 0;
}

static int xhci_enumerate_root_port(uint32_t port, const char *reason) {
    uint32_t portsc;
    uint32_t speed;
    struct xhci_enum_device *dev;

    if (port == 0u || port > g_xhci.max_ports || port >= XHCI_MAX_ROOT_PORTS) {
        return 0;
    }
    portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
    xhci_clear_root_port_changes(port, portsc);
    if ((portsc & XHCI_PORTSC_CCS) == 0u || xhci_root_port_is_tracked((uint8_t)port)) {
        return 0;
    }
    xhci_delay_ms(20u);
    xhci_reset_port(port);
    xhci_delay_ms(20u);
    portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
    xhci_clear_root_port_changes(port, portsc);
    if ((portsc & XHCI_PORTSC_CCS) == 0u || (portsc & XHCI_PORTSC_PED) == 0u) {
        kprint("xhci: %s root port%u enable failed portsc=%x\n", reason, port, portsc);
        return 0;
    }
    speed = (portsc >> 10) & 0x0fu;
    kprint("xhci: %s root port%u connected speed=%u enabled=%u powered=%u portsc=%x\n",
           reason,
           port,
           speed,
           (uint32_t)((portsc & XHCI_PORTSC_PED) != 0u),
           (uint32_t)((portsc & XHCI_PORTSC_PP) != 0u),
           portsc);
    dev = xhci_alloc_device_record();
    if (dev == 0) {
        kprint("xhci: no enum slots left\n");
        return 0;
    }
    memset(dev, 0, sizeof(*dev));
    dev->port = (uint8_t)port;
    dev->controller_index = g_xhci_active_controller;
    dev->speed = (uint8_t)speed;
    if (!xhci_enumerate_device(dev)) {
        return 0;
    }
    g_xhci.root_port_slots[port] = dev->slot_id;
    return 1;
}

void xhci_enumerate_connected_ports(void) {
    for (uint32_t port = 1; port <= g_xhci.max_ports; port++) {
        (void)xhci_enumerate_root_port(port, "boot");
    }
}

static void xhci_scan_root_hotplug_ports(void) {
    for (uint32_t port = 1u; port <= g_xhci.max_ports && port < XHCI_MAX_ROOT_PORTS; port++) {
        uint32_t portsc = xhci_read32(g_xhci.op, xhci_root_port_offset(port));
        struct xhci_enum_device *dev = xhci_find_root_port_device(g_xhci_active_controller, (uint8_t)port);

        xhci_clear_root_port_changes(port, portsc);
        if ((portsc & XHCI_PORTSC_CCS) == 0u) {
            if (dev != 0 || g_xhci.root_port_slots[port] != 0u) {
                kprint("xhci: hotplug root port%u disconnected portsc=%x\n", port, portsc);
                xhci_mark_device_detached(dev);
                g_xhci.root_port_slots[port] = 0u;
            }
            continue;
        }
        if (dev != 0 || g_xhci.root_port_slots[port] != 0u) {
            continue;
        }
        (void)xhci_enumerate_root_port(port, "hotplug");
    }
}

static void xhci_scan_hub_hotplug(struct xhci_enum_device *hub) {
    if (hub == 0 || !hub->used || hub->hub_port_count == 0u) {
        return;
    }
    for (uint8_t port = 1u; port <= hub->hub_port_count; port++) {
        struct xhci_enum_device *child;
        uint16_t status = 0u;
        uint16_t change = 0u;

        child = xhci_find_hub_child(hub, port);
        if (!xhci_hub_get_port_status(hub, port, &status, &change)) {
            continue;
        }
        xhci_hub_clear_changes(hub, port, change);
        if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
            if (child != 0) {
                kprint("xhci: hotplug hub slot%u port%u disconnected status=%x change=%x\n",
                       (uint32_t)hub->slot_id,
                       (uint32_t)port,
                       (uint32_t)status,
                       (uint32_t)change);
                xhci_mark_device_detached(child);
            }
            continue;
        }
        if (child != 0) {
            continue;
        }
        kprint("xhci: hotplug hub slot%u port%u connected status=%x change=%x\n",
               (uint32_t)hub->slot_id,
               (uint32_t)port,
               (uint32_t)status,
               (uint32_t)change);
        (void)xhci_enumerate_hub_port(hub, port);
    }
}

static void xhci_scan_hub_hotplug_ports(void) {
    for (uint32_t i = 0u; i < XHCI_MAX_ENUM_DEVICES; i++) {
        struct xhci_enum_device *dev = &g_enum_devices[i];

        if (dev->used &&
            dev->controller_index == g_xhci_active_controller &&
            dev->hub_port_count != 0u) {
            xhci_scan_hub_hotplug(dev);
        }
    }
}

void xhci_hotplug_poll(void) {
    uint32_t tick;

    if (g_xhci_controller_count == 0u) {
        return;
    }
    tick = hal_timer_current_ticks();
    if ((uint32_t)(tick - g_xhci_last_hotplug_tick) < XHCI_HOTPLUG_SCAN_TICKS) {
        return;
    }
    g_xhci_last_hotplug_tick = tick;
    for (uint32_t index = 0u; index < g_xhci_controller_count && index < XHCI_MAX_CONTROLLERS; index++) {
        if (!xhci_select_controller((uint8_t)index)) {
            continue;
        }
        xhci_scan_root_hotplug_ports();
        xhci_scan_hub_hotplug_ports();
        xhci_save_active_controller();
    }
}
