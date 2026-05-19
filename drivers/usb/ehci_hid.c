#include "drivers/usb/ehci_internal.h"

int ehci_parse_hid_keyboard_config(struct ehci_hid_keyboard *kbd, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_keyboard = 0;

    if (kbd == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    kbd->configuration = cfg[5];
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            in_keyboard = cfg[offset + 5u] == USB_CLASS_HID &&
                          cfg[offset + 6u] == USB_SUBCLASS_BOOT &&
                          cfg[offset + 7u] == USB_PROTO_KEYBOARD;
            if (in_keyboard) {
                kbd->interface_number = cfg[offset + 2u];
            }
        } else if (type == 5u && len >= 7u && in_keyboard) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = usb_read_u16le(cfg + offset + 4u);

            if (attr == 3u && (ep & 0x80u) != 0u) {
                kbd->interrupt_in_ep = ep;
                kbd->interrupt_in_mps = mps;
            }
        }
        offset += len;
    }
    return kbd->configuration != 0u && kbd->interrupt_in_ep != 0u && kbd->interrupt_in_mps != 0u;
}

int ehci_parse_hid_mouse_config(struct ehci_hid_mouse *mouse, const uint8_t *cfg, uint32_t length) {
    uint32_t offset = 0;
    uint8_t in_mouse = 0;

    if (mouse == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    mouse->configuration = cfg[5];
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            in_mouse = cfg[offset + 5u] == USB_CLASS_HID &&
                       cfg[offset + 6u] == USB_SUBCLASS_BOOT &&
                       cfg[offset + 7u] == USB_PROTO_MOUSE;
            if (in_mouse) {
                mouse->interface_number = cfg[offset + 2u];
            }
        } else if (type == 5u && len >= 7u && in_mouse) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = usb_read_u16le(cfg + offset + 4u);

            if (attr == 3u && (ep & 0x80u) != 0u) {
                mouse->interrupt_in_ep = ep;
                mouse->interrupt_in_mps = mps;
            }
        }
        offset += len;
    }
    return mouse->configuration != 0u && mouse->interrupt_in_ep != 0u && mouse->interrupt_in_mps != 0u;
}

int ehci_hid_set_protocol(struct ehci_hid_keyboard *kbd, uint8_t protocol) {
    struct usb_ctrl_request req;

    req.type = 0x21u;
    req.request = USB_REQ_SET_PROTOCOL;
    req.value = protocol;
    req.index = kbd->interface_number;
    req.length = 0u;
    return ehci_control_transfer(&kbd->xfer, kbd->address, kbd->xfer.bulk_in_mps, &req, 0, 0u, 0u);
}

int ehci_hid_mouse_set_protocol(struct ehci_hid_mouse *mouse, uint8_t protocol) {
    struct usb_ctrl_request req;

    req.type = 0x21u;
    req.request = USB_REQ_SET_PROTOCOL;
    req.value = protocol;
    req.index = mouse->interface_number;
    req.length = 0u;
    return ehci_control_transfer(&mouse->xfer, mouse->address, mouse->xfer.bulk_in_mps, &req, 0, 0u, 0u);
}

int ehci_hid_set_idle(struct ehci_hid_keyboard *kbd) {
    struct usb_ctrl_request req;

    req.type = 0x21u;
    req.request = USB_REQ_SET_IDLE;
    req.value = 0u;
    req.index = kbd->interface_number;
    req.length = 0u;
    return ehci_control_transfer(&kbd->xfer, kbd->address, kbd->xfer.bulk_in_mps, &req, 0, 0u, 0u);
}

int ehci_hid_mouse_set_idle(struct ehci_hid_mouse *mouse) {
    struct usb_ctrl_request req;

    req.type = 0x21u;
    req.request = USB_REQ_SET_IDLE;
    req.value = 0u;
    req.index = mouse->interface_number;
    req.length = 0u;
    return ehci_control_transfer(&mouse->xfer, mouse->address, mouse->xfer.bulk_in_mps, &req, 0, 0u, 0u);
}

int ehci_hid_get_report(struct ehci_hid_keyboard *kbd, uint8_t report[8]) {
    struct usb_ctrl_request req;

    req.type = 0xa1u;
    req.request = USB_REQ_GET_REPORT;
    req.value = 0x0100u;
    req.index = kbd->interface_number;
    req.length = 8u;
    return ehci_control_transfer(&kbd->xfer, kbd->address, kbd->xfer.bulk_in_mps, &req, report, 8u, 1u);
}

int ehci_hid_mouse_get_report(struct ehci_hid_mouse *mouse, uint8_t report[4]) {
    struct usb_ctrl_request req;

    req.type = 0xa1u;
    req.request = USB_REQ_GET_REPORT;
    req.value = 0x0100u;
    req.index = mouse->interface_number;
    req.length = 4u;
    return ehci_control_transfer(&mouse->xfer, mouse->address, mouse->xfer.bulk_in_mps, &req, report, 4u, 1u);
}

int ehci_hid_mouse_poll_interrupt_report(struct ehci_hid_mouse *mouse, uint8_t report[4]) {
    uint32_t token = 0u;

    if (mouse == 0 || report == 0 || mouse->interrupt_in_ep == 0u ||
        mouse->interrupt_in_mps == 0u || mouse->xfer.data == 0) {
        return 0;
    }
    memset(mouse->xfer.data, 0, 4u);
    if (!ehci_bulk_transfer(&mouse->xfer,
                            mouse->interrupt_in_ep,
                            mouse->interrupt_in_mps,
                            &mouse->xfer.bulk_in_toggle,
                            mouse->xfer.data_phys,
                            4u,
                            1u,
                            &token,
                            EHCI_HID_INTERRUPT_POLL_SPINS)) {
        return 0;
    }
    memcpy(report, mouse->xfer.data, 4u);
    return 1;
}

void ehci_hid_mouse_process_report(struct ehci_hid_mouse *mouse, const uint8_t report[4], uint32_t tick) {
    uint8_t buttons;
    int32_t dx;
    int32_t dy;

    if (mouse == 0 || report == 0) {
        return;
    }
    buttons = (uint8_t)(report[0] & 0x07u);
    dx = (int32_t)(int8_t)report[1];
    dy = -(int32_t)(int8_t)report[2];
    if (dx == 0 && dy == 0 && buttons == (uint8_t)(mouse->last_report[0] & 0x07u)) {
        return;
    }
    mouse_push_event(dx, dy, buttons, tick);
    memcpy(mouse->last_report, report, 4u);
}

void ehci_hid_set_repeat_usage(struct ehci_hid_keyboard *kbd, uint8_t usage) {
    if (kbd == 0 || kbd->repeat_usage == usage) {
        return;
    }
    kbd->repeat_usage = usage;
    kbd->repeat_active = usage != 0u ? 1u : 0u;
    kbd->repeat_ticks = 0u;
}

void ehci_hid_tick_repeat(struct ehci_hid_keyboard *kbd) {
    uint32_t repeat_age;

    if (kbd == 0 || !kbd->present || !kbd->repeat_active || kbd->repeat_usage == 0u) {
        return;
    }
    if (!ehci_hid_report_contains_usage(kbd->last_report, kbd->repeat_usage)) {
        ehci_hid_set_repeat_usage(kbd, 0u);
        return;
    }
    kbd->repeat_ticks++;
    if (kbd->repeat_ticks < EHCI_HID_REPEAT_DELAY_TICKS) {
        return;
    }
    repeat_age = kbd->repeat_ticks - EHCI_HID_REPEAT_DELAY_TICKS;
    if ((repeat_age % EHCI_HID_REPEAT_RATE_TICKS) != 0u) {
        return;
    }
    ehci_hid_queue_event(keyboard_handle_keycode(usb_hid_usage_to_keycode(kbd->repeat_usage), 1));
}

void ehci_hid_tick_repeats_once(void) {
    uint32_t tick = hal_timer_current_ticks();

    if (tick == g_ehci_hid_last_repeat_tick) {
        return;
    }
    g_ehci_hid_last_repeat_tick = tick;
    for (uint32_t i = 0u; i < g_ehci_hid_keyboard_count; i++) {
        ehci_hid_tick_repeat(&g_ehci_hid_keyboards[i]);
    }
}

int ehci_hid_report_contains_usage(const uint8_t report[8], uint8_t usage) {
    for (uint32_t i = 2; i < 8u; i++) {
        if (report[i] == usage) {
            return 1;
        }
    }
    return 0;
}

void ehci_hid_queue_event(struct keyboard_event event) {
    if (event.keycode == KEYBOARD_KEY_NONE) {
        return;
    }
    if (g_ehci_hid_event_count >= EHCI_HID_EVENT_QUEUE_SIZE) {
        g_ehci_hid_event_tail = (g_ehci_hid_event_tail + 1u) % EHCI_HID_EVENT_QUEUE_SIZE;
        g_ehci_hid_event_count--;
    }
    g_ehci_hid_event_queue[g_ehci_hid_event_head] = event;
    g_ehci_hid_event_head = (g_ehci_hid_event_head + 1u) % EHCI_HID_EVENT_QUEUE_SIZE;
    g_ehci_hid_event_count++;
}

int ehci_hid_pop_event(struct keyboard_event *out) {
    if (out == 0 || g_ehci_hid_event_count == 0u) {
        return 0;
    }
    *out = g_ehci_hid_event_queue[g_ehci_hid_event_tail];
    g_ehci_hid_event_tail = (g_ehci_hid_event_tail + 1u) % EHCI_HID_EVENT_QUEUE_SIZE;
    g_ehci_hid_event_count--;
    return 1;
}

void ehci_hid_process_report(struct ehci_hid_keyboard *kbd, const uint8_t report[8]) {
    static const uint8_t modifier_usages[8] = {0xe0u, 0xe1u, 0xe2u, 0xe3u, 0xe4u, 0xe5u, 0xe6u, 0xe7u};
    uint8_t repeat_usage;

    if (kbd == 0 || report == 0) {
        return;
    }
    repeat_usage = kbd->repeat_usage;
    for (uint32_t i = 0; i < 8u; i++) {
        uint8_t mask = (uint8_t)(1u << i);
        enum keyboard_keycode keycode = usb_hid_usage_to_keycode(modifier_usages[i]);

        if (((kbd->last_report[0] ^ report[0]) & mask) == 0u) {
            continue;
        }
        ehci_hid_queue_event(keyboard_handle_keycode(keycode, (report[0] & mask) != 0u));
    }
    for (uint32_t i = 2; i < 8u; i++) {
        uint8_t usage = kbd->last_report[i];

        if (usage != 0u && !ehci_hid_report_contains_usage(report, usage)) {
            ehci_hid_queue_event(keyboard_handle_keycode(usb_hid_usage_to_keycode(usage), 0));
        }
    }
    for (uint32_t i = 2; i < 8u; i++) {
        uint8_t usage = report[i];

        if (usage != 0u && !ehci_hid_report_contains_usage(kbd->last_report, usage)) {
            ehci_hid_queue_event(keyboard_handle_keycode(usb_hid_usage_to_keycode(usage), 1));
            if (usb_hid_usage_can_repeat(usage)) {
                repeat_usage = usage;
            }
        }
    }
    if (repeat_usage == 0u) {
        repeat_usage = usb_hid_first_repeat_usage(report);
    } else if (!ehci_hid_report_contains_usage(report, repeat_usage)) {
        repeat_usage = usb_hid_first_repeat_usage(report);
    }
    ehci_hid_set_repeat_usage(kbd, repeat_usage);
    memcpy(kbd->last_report, report, 8u);
}
