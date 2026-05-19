#include "drivers/usb/ehci_internal.h"

int ehci_config_has_hub_interface(const uint8_t *cfg, uint32_t length) {
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

struct ehci_msc_device *ehci_select_probe_xfer(void) {
    if (g_ehci_msc_count < EHCI_MAX_MSC) {
        return &g_ehci_msc[g_ehci_msc_count];
    }
    if (g_ehci_hid_keyboard_count < EHCI_MAX_HID_KEYBOARDS) {
        return &g_ehci_hid_keyboards[g_ehci_hid_keyboard_count].xfer;
    }
    return 0;
}

void ehci_log_device_summary(struct ehci_msc_device *dev,
                                    uint32_t root_port,
                                    const uint8_t *dev_desc,
                                    uint16_t mps0) {
    if (dev->hub_addr != 0u) {
        kprint("ehci: hub%u port%u dev addr=%u speed=%u cls=%x sub=%x proto=%x vid=%x pid=%x mps0=%u\n",
               (uint32_t)dev->hub_addr,
               (uint32_t)dev->hub_port,
               (uint32_t)dev->address,
               (uint32_t)dev->speed,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6],
               (uint32_t)usb_read_u16le(dev_desc + 8),
               (uint32_t)usb_read_u16le(dev_desc + 10),
               (uint32_t)mps0);
    } else {
        kprint("ehci: port%u dev addr=%u speed=%u cls=%x sub=%x proto=%x vid=%x pid=%x mps0=%u\n",
               root_port,
               (uint32_t)dev->address,
               (uint32_t)dev->speed,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6],
               (uint32_t)usb_read_u16le(dev_desc + 8),
               (uint32_t)usb_read_u16le(dev_desc + 10),
               (uint32_t)mps0);
    }
}

void ehci_log_unsupported(struct ehci_msc_device *dev,
                                 uint32_t root_port,
                                 uint16_t total_len,
                                 const uint8_t *dev_desc) {
    if (dev->hub_addr != 0u) {
        kprint("ehci: hub%u port%u unsupported config total=%u devcls=%x devsub=%x devproto=%x\n",
               (uint32_t)dev->hub_addr,
               (uint32_t)dev->hub_port,
               (uint32_t)total_len,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6]);
    } else {
        kprint("ehci: port%u unsupported config total=%u devcls=%x devsub=%x devproto=%x\n",
               root_port,
               (uint32_t)total_len,
               (uint32_t)dev_desc[4],
               (uint32_t)dev_desc[5],
               (uint32_t)dev_desc[6]);
    }
}

int ehci_hub_get_descriptor(struct ehci_msc_device *hub, uint8_t *buffer, uint16_t length) {
    struct usb_ctrl_request req;

    req.type = 0xa0u;
    req.request = USB_REQ_GET_DESCRIPTOR;
    req.value = (uint16_t)(USB_DESC_HUB << 8);
    req.index = 0u;
    req.length = length;
    return ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, buffer, length, 1u);
}

int ehci_hub_set_port_feature(struct ehci_msc_device *hub, uint8_t port, uint16_t feature) {
    struct usb_ctrl_request req;

    req.type = 0x23u;
    req.request = USB_REQ_SET_FEATURE;
    req.value = feature;
    req.index = port;
    req.length = 0u;
    return ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, 0, 0u, 0u);
}

int ehci_hub_clear_port_feature(struct ehci_msc_device *hub, uint8_t port, uint16_t feature) {
    struct usb_ctrl_request req;

    req.type = 0x23u;
    req.request = USB_REQ_CLEAR_FEATURE;
    req.value = feature;
    req.index = port;
    req.length = 0u;
    return ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, 0, 0u, 0u);
}

int ehci_hub_get_port_status(struct ehci_msc_device *hub,
                                    uint8_t port,
                                    uint16_t *status_out,
                                    uint16_t *change_out) {
    struct usb_ctrl_request req;
    uint8_t data[4];

    req.type = 0xa3u;
    req.request = USB_REQ_GET_STATUS;
    req.value = 0u;
    req.index = port;
    req.length = sizeof(data);
    memset(data, 0, sizeof(data));
    if (!ehci_control_transfer(hub, hub->address, hub->bulk_in_mps, &req, data, sizeof(data), 1u)) {
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

uint8_t ehci_hub_child_speed(uint16_t status) {
    if ((status & USB_HUB_PORT_LOW_SPEED) != 0u) {
        return EHCI_DEV_SPEED_LOW;
    }
    if ((status & USB_HUB_PORT_HIGH_SPEED) != 0u) {
        return EHCI_DEV_SPEED_HIGH;
    }
    return EHCI_DEV_SPEED_FULL;
}

void ehci_hub_clear_changes(struct ehci_msc_device *hub, uint8_t port, uint16_t change) {
    if ((change & USB_HUB_PORT_CONNECTION) != 0u) {
        (void)ehci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_CONNECTION);
    }
    if ((change & USB_HUB_PORT_ENABLE) != 0u) {
        (void)ehci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_ENABLE);
    }
    if ((change & USB_HUB_PORT_RESET) != 0u) {
        (void)ehci_hub_clear_port_feature(hub, port, USB_HUB_FEATURE_C_PORT_RESET);
    }
}

int ehci_enumerate_hub_port(struct ehci_msc_device *hub, uint32_t root_port, uint8_t port) {
    struct ehci_msc_device *dev;
    uint8_t dev_desc[18];
    uint16_t status = 0u;
    uint16_t change = 0u;
    uint16_t mps0;

    if (!ehci_hub_get_port_status(hub, port, &status, &change)) {
        kprint("ehci: hub%u port%u status failed\n", (uint32_t)hub->address, (uint32_t)port);
        return 0;
    }
    ehci_hub_clear_changes(hub, port, change);
    if ((status & USB_HUB_PORT_CONNECTION) == 0u) {
        return 0;
    }
    kprint("ehci: hub%u port%u connected status=%x change=%x\n",
           (uint32_t)hub->address, (uint32_t)port, (uint32_t)status, (uint32_t)change);
    if (!ehci_hub_set_port_feature(hub, port, USB_HUB_FEATURE_PORT_RESET)) {
        kprint("ehci: hub%u port%u reset failed\n", (uint32_t)hub->address, (uint32_t)port);
        return 0;
    }
    ehci_delay_ms(80u);
    for (uint32_t i = 0; i < 200u; i++) {
        if (!ehci_hub_get_port_status(hub, port, &status, &change)) {
            return 0;
        }
        if ((status & USB_HUB_PORT_RESET) == 0u) {
            break;
        }
        ehci_delay_ms(1u);
    }
    ehci_hub_clear_changes(hub, port, change);
    if (!ehci_hub_get_port_status(hub, port, &status, &change) ||
        (status & USB_HUB_PORT_CONNECTION) == 0u ||
        (status & USB_HUB_PORT_ENABLE) == 0u) {
        kprint("ehci: hub%u port%u enable failed status=%x change=%x\n",
               (uint32_t)hub->address, (uint32_t)port, (uint32_t)status, (uint32_t)change);
        return 0;
    }

    dev = ehci_select_probe_xfer();
    if (dev == 0) {
        return 0;
    }
    memset(dev, 0, sizeof(*dev));
    dev->controller = hub->controller;
    ehci_write_name(dev->name, g_ehci_msc_count);
    dev->speed = ehci_hub_child_speed(status);
    dev->root_port = (uint8_t)root_port;
    dev->hub_addr = hub->address;
    dev->hub_port = port;
    if (!ehci_alloc_msc_memory(dev)) {
        return 0;
    }
    mps0 = dev->speed == EHCI_DEV_SPEED_HIGH ? 64u : 8u;
    memset(dev_desc, 0, sizeof(dev_desc));
    for (uint32_t attempt = 0; attempt < 3u; attempt++) {
        ehci_delay_ms(20u);
        if (ehci_get_descriptor(dev, 0u, mps0, USB_DESC_DEVICE, 0u, dev_desc, 8u)) {
            mps0 = dev_desc[7] != 0u ? dev_desc[7] : mps0;
            return ehci_finish_device_enumeration(dev, root_port, dev_desc, mps0);
        }
    }
    kprint("ehci: hub%u port%u get device descriptor failed speed=%u status=%x\n",
           (uint32_t)hub->address, (uint32_t)port, (uint32_t)dev->speed, (uint32_t)status);
    return 0;
}

int ehci_probe_hub(struct ehci_msc_device *dev,
                          uint32_t root_port,
                          uint16_t mps0,
                          const uint8_t *cfg,
                          uint32_t cfg_len) {
    struct ehci_msc_device *hub;
    uint8_t hub_desc[32];
    uint8_t port_count;
    uint32_t pwr_ms;
    uint32_t hub_index;

    (void)cfg_len;
    if (g_ehci_hub_count >= EHCI_MAX_HUBS) {
        kprint("ehci: hub limit reached\n");
        return 0;
    }
    dev->configuration = cfg[5];
    dev->control_mps = mps0;
    dev->bulk_in_mps = mps0;
    if (!ehci_set_configuration(dev, dev->configuration, mps0)) {
        kprint("ehci: port%u hub set config failed\n", root_port);
        return 0;
    }
    hub_index = g_ehci_hub_count++;
    hub = &g_ehci_hubs[hub_index];
    memset(hub, 0, sizeof(*hub));
    *hub = *dev;
    hub->present = 1u;
    memset(hub_desc, 0, sizeof(hub_desc));
    if (!ehci_hub_get_descriptor(hub, hub_desc, 8u)) {
        kprint("ehci: hub%u descriptor failed\n", (uint32_t)hub->address);
        return 0;
    }
    port_count = hub_desc[2];
    if (port_count > 8u) {
        port_count = 8u;
    }
    pwr_ms = (uint32_t)hub_desc[5] * 2u;
    if (pwr_ms < 100u) {
        pwr_ms = 100u;
    }
    kprint("ehci: hub%u rootport=%u ports=%u pwr=%u\n",
           (uint32_t)hub->address,
           root_port,
           (uint32_t)port_count,
           pwr_ms);
    for (uint8_t port = 1u; port <= port_count; port++) {
        (void)ehci_hub_set_port_feature(hub, port, USB_HUB_FEATURE_PORT_POWER);
    }
    ehci_delay_ms(pwr_ms);
    for (uint8_t port = 1u; port <= port_count; port++) {
        (void)ehci_enumerate_hub_port(hub, root_port, port);
    }
    return 1;
}

int ehci_finish_device_enumeration(struct ehci_msc_device *dev,
                                          uint32_t root_port,
                                          uint8_t *dev_desc,
                                          uint16_t mps0) {
    struct ehci_hid_keyboard *kbd;
    struct ehci_hid_mouse *mouse;
    struct ehci_msc_device saved;
    uint8_t cfg[256];
    uint16_t total_len;
    uint8_t hid_parsed;

    dev->control_mps = mps0;
    dev->bulk_in_mps = mps0;
    dev->root_port = (uint8_t)root_port;
    dev->address = g_ehci_next_address++;
    if (!ehci_set_address(dev, dev->address, mps0)) {
        return 0;
    }
    ehci_delay_ms(10u);
    if (!ehci_get_descriptor(dev, dev->address, mps0, USB_DESC_DEVICE, 0u, dev_desc, 18u)) {
        return 0;
    }
    ehci_log_device_summary(dev, root_port, dev_desc, mps0);
    memset(cfg, 0, sizeof(cfg));
    if (!ehci_get_descriptor(dev, dev->address, mps0, USB_DESC_CONFIGURATION, 0u, cfg, 9u)) {
        return 0;
    }
    total_len = usb_read_u16le(cfg + 2);
    if (total_len > sizeof(cfg)) {
        total_len = sizeof(cfg);
    }
    if (!ehci_get_descriptor(dev, dev->address, mps0, USB_DESC_CONFIGURATION, 0u, cfg, total_len)) {
        return 0;
    }
    if (dev_desc[4] == USB_CLASS_HUB || ehci_config_has_hub_interface(cfg, total_len)) {
        return ehci_probe_hub(dev, root_port, mps0, cfg, total_len);
    }
    if (g_ehci_msc_count < EHCI_MAX_MSC && ehci_parse_msc_config(dev, cfg, total_len)) {
        if (EHCI_MSC_DEBUG) {
            ehci_log_config_descriptor(cfg, total_len);
        }
        if (!ehci_set_configuration(dev, dev->configuration, mps0)) {
            return 0;
        }
        ehci_delay_ms(EHCI_SET_CONFIGURATION_SETTLE_MS);
        dev->bulk_in_toggle = 0u;
        dev->bulk_out_toggle = 0u;
        dev->tag = 0x12340000u;
        if (!ehci_msc_probe(dev)) {
            kprint("ehci: port%u MSC probe failed\n", root_port);
            return 0;
        }
        dev->blockdev.name = dev->name;
        dev->blockdev.block_size = EHCI_SECTOR_SIZE;
        dev->blockdev.block_count = dev->sector_count;
        dev->blockdev.read = ehci_msc_read_impl;
        dev->blockdev.write = ehci_msc_write_impl;
        dev->blockdev.driver_data = dev;
        dev->present = 1u;
        if (blockdev_register(&dev->blockdev) != 0) {
            dev->present = 0u;
            return 0;
        }
        kprint("ehci: port%u %s lun=%u sectors=%lx in=%x out=%x\n",
               root_port, dev->name, (uint32_t)dev->msc_lun, dev->sector_count,
               (uint32_t)dev->bulk_in_ep, (uint32_t)dev->bulk_out_ep);
        g_ehci_msc_count++;
        return 1;
    }
    if (g_ehci_hid_mouse_count < EHCI_MAX_HID_MICE) {
        mouse = &g_ehci_hid_mice[g_ehci_hid_mouse_count];
        saved = *dev;
        memset(mouse, 0, sizeof(*mouse));
        mouse->xfer = saved;
        mouse->address = saved.address;
        mouse->xfer.bulk_in_mps = mps0;
        if (ehci_parse_hid_mouse_config(mouse, cfg, total_len)) {
            if (!ehci_set_configuration(&mouse->xfer, mouse->configuration, mps0)) {
                kprint("ehci: port%u hid mouse set config failed\n", root_port);
                return 0;
            }
            if (!ehci_hid_mouse_set_protocol(mouse, 0u)) {
                kprint("ehci: port%u hid mouse set protocol failed\n", root_port);
            }
            if (!ehci_hid_mouse_set_idle(mouse)) {
                kprint("ehci: port%u hid mouse set idle failed\n", root_port);
            }
            memset(mouse->last_report, 0, sizeof(mouse->last_report));
            mouse->present = 1u;
            if (mouse->xfer.hub_addr != 0u) {
                kprint("ehci: hub%u port%u hidmouse%u addr=%u iface=%u in=%x mps=%u\n",
                       (uint32_t)mouse->xfer.hub_addr,
                       (uint32_t)mouse->xfer.hub_port,
                       g_ehci_hid_mouse_count,
                       (uint32_t)mouse->address,
                       (uint32_t)mouse->interface_number,
                       (uint32_t)mouse->interrupt_in_ep,
                       (uint32_t)mouse->interrupt_in_mps);
            } else {
                kprint("ehci: port%u hidmouse%u addr=%u iface=%u in=%x mps=%u\n",
                       root_port,
                       g_ehci_hid_mouse_count,
                       (uint32_t)mouse->address,
                       (uint32_t)mouse->interface_number,
                       (uint32_t)mouse->interrupt_in_ep,
                       (uint32_t)mouse->interrupt_in_mps);
            }
            g_ehci_hid_mouse_count++;
            return 1;
        }
    }
    if (g_ehci_hid_keyboard_count >= EHCI_MAX_HID_KEYBOARDS) {
        return 0;
    }
    kbd = &g_ehci_hid_keyboards[g_ehci_hid_keyboard_count];
    saved = *dev;
    memset(kbd, 0, sizeof(*kbd));
    kbd->xfer = saved;
    kbd->address = saved.address;
    kbd->xfer.bulk_in_mps = mps0;
    hid_parsed = (uint8_t)ehci_parse_hid_keyboard_config(kbd, cfg, total_len);
    if (!hid_parsed) {
        ehci_log_unsupported(dev, root_port, total_len, dev_desc);
        return 0;
    }
    if (!ehci_set_configuration(&kbd->xfer, kbd->configuration, mps0)) {
        kprint("ehci: port%u hid set config failed\n", root_port);
        return 0;
    }
    if (!ehci_hid_set_protocol(kbd, 0u)) {
        kprint("ehci: port%u hid set protocol failed\n", root_port);
    }
    if (!ehci_hid_set_idle(kbd)) {
        kprint("ehci: port%u hid set idle failed\n", root_port);
    }
    memset(kbd->last_report, 0, sizeof(kbd->last_report));
    kbd->report_fail_logged = 0u;
    kbd->present = 1u;
    if (kbd->xfer.hub_addr != 0u) {
        kprint("ehci: hub%u port%u hidkbd%u addr=%u iface=%u in=%x mps=%u\n",
               (uint32_t)kbd->xfer.hub_addr,
               (uint32_t)kbd->xfer.hub_port,
               g_ehci_hid_keyboard_count,
               (uint32_t)kbd->address,
               (uint32_t)kbd->interface_number,
               (uint32_t)kbd->interrupt_in_ep,
               (uint32_t)kbd->interrupt_in_mps);
    } else {
        kprint("ehci: port%u hidkbd%u addr=%u iface=%u in=%x mps=%u\n",
               root_port,
               g_ehci_hid_keyboard_count,
               (uint32_t)kbd->address,
               (uint32_t)kbd->interface_number,
               (uint32_t)kbd->interrupt_in_ep,
               (uint32_t)kbd->interrupt_in_mps);
    }
    g_ehci_hid_keyboard_count++;
    return 1;
}

int ehci_enumerate_port(uint32_t port_index) {
    struct ehci_msc_device *dev;
    uint8_t dev_desc[18];
    uint16_t mps0 = 64u;
    uint32_t port_off = 0x44u + port_index * 4u;
    uint32_t port = ehci_read_op(port_off);

    if ((g_ehci_msc_count >= EHCI_MAX_MSC &&
         g_ehci_hid_keyboard_count >= EHCI_MAX_HID_KEYBOARDS &&
         g_ehci_hid_mouse_count >= EHCI_MAX_HID_MICE) ||
        (port & EHCI_PORT_CONNECT) == 0u) {
        return 0;
    }
    if (!ehci_reset_port(port_index, &port)) {
        if ((port & EHCI_PORT_OWNER) != 0u) {
            kprint("ehci: port%u handed to companion portsc=%x\n", port_index, port);
        } else {
            kprint("ehci: port%u not high-speed portsc=%x\n", port_index, port);
        }
        return 0;
    }

    dev = ehci_select_probe_xfer();
    if (dev == 0) {
        return 0;
    }
    memset(dev, 0, sizeof(*dev));
    dev->controller = g_ehci;
    ehci_write_name(dev->name, g_ehci_msc_count);
    dev->speed = EHCI_DEV_SPEED_HIGH;
    if (!ehci_alloc_msc_memory(dev)) {
        return 0;
    }
    if (!ehci_get_initial_device_descriptor(dev, port_index, dev_desc, 8u, mps0, &port)) {
        kprint("ehci: port%u get device descriptor failed\n", port_index);
        return 0;
    }
    mps0 = dev_desc[7] != 0u ? dev_desc[7] : 64u;
    return ehci_finish_device_enumeration(dev, port_index, dev_desc, mps0);
}

static int ehci_root_port_tracked(uint32_t port_index) {
    for (uint32_t i = 0u; i < g_ehci_msc_count && i < EHCI_MAX_MSC; i++) {
        struct ehci_msc_device *dev = &g_ehci_msc[i];

        if (dev->present && dev->hub_addr == 0u && dev->root_port == (uint8_t)port_index) {
            return 1;
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hub_count && i < EHCI_MAX_HUBS; i++) {
        struct ehci_msc_device *hub = &g_ehci_hubs[i];

        if (hub->present && hub->hub_addr == 0u && hub->root_port == (uint8_t)port_index) {
            return 1;
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hid_keyboard_count && i < EHCI_MAX_HID_KEYBOARDS; i++) {
        struct ehci_hid_keyboard *kbd = &g_ehci_hid_keyboards[i];

        if (kbd->present && kbd->xfer.hub_addr == 0u && kbd->xfer.root_port == (uint8_t)port_index) {
            return 1;
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hid_mouse_count && i < EHCI_MAX_HID_MICE; i++) {
        struct ehci_hid_mouse *mouse = &g_ehci_hid_mice[i];

        if (mouse->present && mouse->xfer.hub_addr == 0u && mouse->xfer.root_port == (uint8_t)port_index) {
            return 1;
        }
    }
    return 0;
}

static void ehci_detach_hub_children(uint8_t hub_addr) {
    if (hub_addr == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < g_ehci_msc_count && i < EHCI_MAX_MSC; i++) {
        struct ehci_msc_device *dev = &g_ehci_msc[i];

        if (dev->present && dev->hub_addr == hub_addr) {
            (void)blockdev_unregister(&dev->blockdev);
            dev->present = 0u;
            dev->read_cache_valid = 0u;
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hid_keyboard_count && i < EHCI_MAX_HID_KEYBOARDS; i++) {
        struct ehci_hid_keyboard *kbd = &g_ehci_hid_keyboards[i];

        if (kbd->present && kbd->xfer.hub_addr == hub_addr) {
            kbd->present = 0u;
            memset(kbd->last_report, 0, sizeof(kbd->last_report));
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hid_mouse_count && i < EHCI_MAX_HID_MICE; i++) {
        struct ehci_hid_mouse *mouse = &g_ehci_hid_mice[i];

        if (mouse->present && mouse->xfer.hub_addr == hub_addr) {
            mouse->present = 0u;
            memset(mouse->last_report, 0, sizeof(mouse->last_report));
        }
    }
}

static void ehci_mark_root_port_detached(uint32_t port_index) {
    for (uint32_t i = 0u; i < g_ehci_msc_count && i < EHCI_MAX_MSC; i++) {
        struct ehci_msc_device *dev = &g_ehci_msc[i];

        if (dev->present && dev->hub_addr == 0u && dev->root_port == (uint8_t)port_index) {
            (void)blockdev_unregister(&dev->blockdev);
            dev->present = 0u;
            dev->read_cache_valid = 0u;
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hub_count && i < EHCI_MAX_HUBS; i++) {
        struct ehci_msc_device *hub = &g_ehci_hubs[i];

        if (hub->present && hub->hub_addr == 0u && hub->root_port == (uint8_t)port_index) {
            ehci_detach_hub_children(hub->address);
            hub->present = 0u;
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hid_keyboard_count && i < EHCI_MAX_HID_KEYBOARDS; i++) {
        struct ehci_hid_keyboard *kbd = &g_ehci_hid_keyboards[i];

        if (kbd->present && kbd->xfer.hub_addr == 0u && kbd->xfer.root_port == (uint8_t)port_index) {
            kbd->present = 0u;
            memset(kbd->last_report, 0, sizeof(kbd->last_report));
        }
    }
    for (uint32_t i = 0u; i < g_ehci_hid_mouse_count && i < EHCI_MAX_HID_MICE; i++) {
        struct ehci_hid_mouse *mouse = &g_ehci_hid_mice[i];

        if (mouse->present && mouse->xfer.hub_addr == 0u && mouse->xfer.root_port == (uint8_t)port_index) {
            mouse->present = 0u;
            memset(mouse->last_report, 0, sizeof(mouse->last_report));
        }
    }
}

void ehci_hotplug_poll(void) {
    uint32_t tick;

    if (g_ehci.cap == 0 || g_ehci.op == 0 || g_ehci.port_count == 0u) {
        return;
    }
    tick = hal_timer_current_ticks();
    if ((uint32_t)(tick - g_ehci_last_hotplug_tick) < EHCI_HOTPLUG_SCAN_TICKS) {
        return;
    }
    g_ehci_last_hotplug_tick = tick;
    for (uint32_t port = 0u; port < g_ehci.port_count; port++) {
        uint32_t port_off = 0x44u + port * 4u;
        uint32_t portsc = ehci_read_op(port_off);
        uint32_t changes = portsc & EHCI_PORT_WRITE_CLEAR_BITS;

        if (changes != 0u) {
            ehci_write_op(port_off, ehci_port_write_value(portsc) | changes);
        }
        if ((portsc & EHCI_PORT_CONNECT) == 0u) {
            if (ehci_root_port_tracked(port)) {
                kprint("ehci: hotplug port%u disconnected portsc=%x\n", port, portsc);
                ehci_mark_root_port_detached(port);
            }
            continue;
        }
        if (ehci_root_port_tracked(port)) {
            continue;
        }
        kprint("ehci: hotplug port%u connected portsc=%x\n", port, portsc);
        (void)ehci_enumerate_port(port);
    }
}
