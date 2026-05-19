#include "drivers/usb/xhci_internal.h"

static int xhci_parse_hid_keyboard_config(struct xhci_enum_device *dev,
                                          struct xhci_hid_keyboard *kbd,
                                          const uint8_t *cfg,
                                          uint32_t length) {
    uint32_t offset = 0u;
    uint8_t in_hid = 0u;
    uint8_t hid_iface = 0u;
    uint8_t hid_subclass = 0u;
    uint8_t hid_protocol = 0u;
    uint8_t hid_endpoint_count = 0u;
    uint8_t found = 0u;
    uint8_t found_boot = 0u;

    if (dev == 0 || kbd == 0 || cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            hid_iface = cfg[offset + 2u];
            hid_endpoint_count = cfg[offset + 4u];
            hid_subclass = cfg[offset + 6u];
            hid_protocol = cfg[offset + 7u];
            in_hid = cfg[offset + 5u] == USB_CLASS_HID;
            if (in_hid) {
                kprint("xhci: slot%u HID iface=%u subclass=%u proto=%u eps=%u\n",
                       (uint32_t)dev->slot_id,
                       (uint32_t)hid_iface,
                       (uint32_t)hid_subclass,
                       (uint32_t)hid_protocol,
                       (uint32_t)hid_endpoint_count);
            }
        } else if (type == 5u && len >= 7u && in_hid) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = (uint16_t)(usb_read_u16le(cfg + offset + 4u) & 0x07ffu);
            uint8_t interval = cfg[offset + 6u];
            uint8_t ep_num = ep & 0x0fu;
            uint8_t is_boot_keyboard = hid_subclass == USB_SUBCLASS_BOOT &&
                                       hid_protocol == USB_PROTO_KEYBOARD;

            kprint("xhci: slot%u HID iface=%u ep=%x attr=%u mps=%u interval=%u\n",
                   (uint32_t)dev->slot_id,
                   (uint32_t)hid_iface,
                   (uint32_t)ep,
                   (uint32_t)attr,
                   (uint32_t)mps,
                   (uint32_t)interval);

            if (hid_protocol == USB_PROTO_KEYBOARD &&
                attr == 3u && (ep & 0x80u) != 0u && ep_num != 0u && mps != 0u &&
                (!found || (is_boot_keyboard && !found_boot))) {
                kbd->interface_number = hid_iface;
                kbd->interface_class = USB_CLASS_HID;
                kbd->interface_subclass = hid_subclass;
                kbd->interface_protocol = hid_protocol;
                kbd->boot_protocol = is_boot_keyboard;
                kbd->interrupt_in_ep = ep;
                kbd->interrupt_in_epid = (uint8_t)(ep_num * 2u + 1u);
                kbd->interrupt_in_mps = mps;
                kbd->interrupt_in_interval = interval;
                kbd->report_size = (uint8_t)(mps > XHCI_HID_REPORT_BYTES ? XHCI_HID_REPORT_BYTES : mps);
                if (kbd->report_size < 8u) {
                    kbd->report_size = 8u;
                }
                found = 1u;
                found_boot = is_boot_keyboard;
            }
        }
        offset += len;
    }
    return found;
}

static int xhci_parse_hid_mouse_config(struct xhci_enum_device *dev,
                                       struct xhci_hid_keyboard *mouse,
                                       const uint8_t *cfg,
                                       uint32_t length) {
    uint32_t offset = 0u;
    uint8_t in_mouse = 0u;
    uint8_t hid_iface = 0u;
    uint8_t hid_subclass = 0u;
    uint8_t hid_protocol = 0u;

    if (dev == 0 || mouse == 0 || cfg == 0 || length < 9u || cfg[1] != USB_DESC_CONFIGURATION) {
        return 0;
    }
    while (offset + 2u <= length) {
        uint8_t len = cfg[offset];
        uint8_t type = cfg[offset + 1u];

        if (len < 2u || offset + len > length) {
            break;
        }
        if (type == 4u && len >= 9u) {
            hid_iface = cfg[offset + 2u];
            hid_subclass = cfg[offset + 6u];
            hid_protocol = cfg[offset + 7u];
            in_mouse = cfg[offset + 5u] == USB_CLASS_HID &&
                       hid_subclass == USB_SUBCLASS_BOOT &&
                       hid_protocol == USB_PROTO_MOUSE;
        } else if (type == 5u && len >= 7u && in_mouse) {
            uint8_t ep = cfg[offset + 2u];
            uint8_t attr = cfg[offset + 3u] & 0x03u;
            uint16_t mps = (uint16_t)(usb_read_u16le(cfg + offset + 4u) & 0x07ffu);
            uint8_t interval = cfg[offset + 6u];
            uint8_t ep_num = ep & 0x0fu;

            if (attr == 3u && (ep & 0x80u) != 0u && ep_num != 0u && mps != 0u) {
                mouse->interface_number = hid_iface;
                mouse->interface_class = USB_CLASS_HID;
                mouse->interface_subclass = hid_subclass;
                mouse->interface_protocol = hid_protocol;
                mouse->boot_protocol = 1u;
                mouse->interrupt_in_ep = ep;
                mouse->interrupt_in_epid = (uint8_t)(ep_num * 2u + 1u);
                mouse->interrupt_in_mps = mps;
                mouse->interrupt_in_interval = interval;
                mouse->report_size = (uint8_t)(mps > XHCI_HID_REPORT_BYTES ? XHCI_HID_REPORT_BYTES : mps);
                if (mouse->report_size < 4u) {
                    mouse->report_size = 4u;
                }
                return 1;
            }
        }
        offset += len;
    }
    return 0;
}

static int xhci_hid_set_protocol(struct xhci_hid_keyboard *kbd, uint8_t protocol) {
    if (kbd == 0 || kbd->dev == 0) {
        return 0;
    }
    return xhci_control_transfer(kbd->dev,
                                 0x21u,
                                 USB_REQ_SET_PROTOCOL,
                                 protocol,
                                 kbd->interface_number,
                                 0,
                                 0u,
                                 0u);
}

static int xhci_hid_set_idle(struct xhci_hid_keyboard *kbd) {
    if (kbd == 0 || kbd->dev == 0) {
        return 0;
    }
    return xhci_control_transfer(kbd->dev,
                                 0x21u,
                                 USB_REQ_SET_IDLE,
                                 0u,
                                 kbd->interface_number,
                                 0,
                                 0u,
                                 0u);
}

static uint8_t xhci_hid_interval_value(struct xhci_enum_device *dev, uint8_t interval) {
    uint32_t target;
    uint8_t encoded = 0u;

    if (interval == 0u) {
        interval = 10u;
    }
    if (dev != 0 && dev->speed >= XHCI_SPEED_HIGH) {
        return interval > 16u ? 15u : (uint8_t)(interval - 1u);
    }
    target = (uint32_t)interval * 8u;
    if (target < 8u) {
        target = 8u;
    }
    target--;
    while (target != 0u && encoded < 15u) {
        target >>= 1;
        encoded++;
    }
    return encoded;
}

static void xhci_prepare_hid_interrupt_context(struct xhci_hid_keyboard *kbd) {
    struct xhci_enum_device *dev = kbd->dev;
    uint8_t *input_control = xhci_context_ptr(dev->input_context, 0u);
    uint8_t *slot = xhci_context_ptr(dev->input_context, 1u);
    uint8_t *out_slot = xhci_context_ptr(dev->device_context, 0u);
    uint8_t *ep = xhci_context_ptr(dev->input_context, (uint32_t)kbd->interrupt_in_epid + 1u);
    uint32_t *ic = (uint32_t *)input_control;
    uint32_t *slot_ctx = (uint32_t *)slot;
    uint32_t *ep_ctx = (uint32_t *)ep;
    uint32_t interval = xhci_hid_interval_value(dev, kbd->interrupt_in_interval);
    uint32_t mps = kbd->interrupt_in_mps;

    memset(dev->input_context, 0, XHCI_PAGE_SIZE);
    memcpy(slot, out_slot, g_xhci.context_size);
    ic[1] = XHCI_SLOT_FLAG | (1u << kbd->interrupt_in_epid);
    slot_ctx[0] = (slot_ctx[0] & ~(0x1fu << 27)) | ((uint32_t)kbd->interrupt_in_epid << 27);

    ep_ctx[0] = interval << 16;
    ep_ctx[1] = XHCI_EP_CONTEXT_CERR_3 | (7u << 3) | (mps << 16);
    ep_ctx[2] = (uint32_t)kbd->interrupt_in_ring_phys | 1u;
    ep_ctx[3] = (uint32_t)(kbd->interrupt_in_ring_phys >> 32);
    ep_ctx[4] = (uint32_t)kbd->report_size | (mps << 16);
}

static int xhci_configure_hid_interrupt_endpoint(struct xhci_hid_keyboard *kbd) {
    if (kbd == 0 || kbd->dev == 0 || kbd->interrupt_in_epid == 0u) {
        return 0;
    }
    xhci_prepare_hid_interrupt_context(kbd);
    return xhci_command_context(XHCI_TRB_CONFIGURE_ENDPOINT, kbd->dev);
}

static uint32_t xhci_hid_report_transfer_size(const struct xhci_hid_keyboard *kbd) {
    uint32_t report_size;

    if (kbd == 0) {
        return 8u;
    }
    report_size = kbd->report_size;
    if (kbd->interface_protocol == USB_PROTO_MOUSE) {
        if (report_size < 4u || report_size > XHCI_HID_REPORT_BYTES) {
            report_size = 4u;
        }
        return report_size;
    }
    if (report_size < 8u || report_size > XHCI_HID_REPORT_BYTES) {
        report_size = 8u;
    }
    return report_size;
}

static void xhci_hid_copy_report(const struct xhci_hid_keyboard *kbd, uint8_t report[8]) {
    uint32_t report_offset = 0u;
    uint32_t report_size = xhci_hid_report_transfer_size(kbd);

    if (report == 0) {
        return;
    }
    memset(report, 0, 8u);
    if (kbd == 0 || kbd->report == 0) {
        return;
    }
    if (!kbd->boot_protocol && report_size > 8u && kbd->report[0] != 0u) {
        report_offset = 1u;
    }
    if (report_offset + 8u <= report_size) {
        memcpy(report, kbd->report + report_offset, 8u);
    } else {
        memcpy(report, kbd->report, report_size < 8u ? report_size : 8u);
    }
}

static void xhci_hid_queue_report(struct xhci_hid_keyboard *kbd, uint32_t completion) {
    uint32_t index;

    if (kbd == 0) {
        return;
    }
    if (kbd->report_queue_count >= XHCI_HID_REPORT_QUEUE_SIZE) {
        kbd->report_queue_tail = (uint8_t)((kbd->report_queue_tail + 1u) % XHCI_HID_REPORT_QUEUE_SIZE);
        kbd->report_queue_count--;
    }
    index = kbd->report_queue_head;
    kbd->report_queue_completion[index] = (uint8_t)completion;
    if (completion == XHCI_CC_SUCCESS || completion == XHCI_CC_SHORT_PACKET) {
        xhci_hid_copy_report(kbd, kbd->report_queue[index]);
    } else {
        memset(kbd->report_queue[index], 0, 8u);
    }
    kbd->report_queue_head = (uint8_t)((kbd->report_queue_head + 1u) % XHCI_HID_REPORT_QUEUE_SIZE);
    kbd->report_queue_count++;
}

static int xhci_hid_pop_queued_report(struct xhci_hid_keyboard *kbd,
                                      uint8_t report[8],
                                      uint32_t *completion_out) {
    uint32_t index;

    if (kbd == 0 || report == 0 || kbd->report_queue_count == 0u) {
        return 0;
    }
    index = kbd->report_queue_tail;
    memcpy(report, kbd->report_queue[index], 8u);
    if (completion_out != 0) {
        *completion_out = kbd->report_queue_completion[index];
    }
    kbd->report_queue_tail = (uint8_t)((kbd->report_queue_tail + 1u) % XHCI_HID_REPORT_QUEUE_SIZE);
    kbd->report_queue_count--;
    return 1;
}

static int xhci_hid_submit_interrupt_report(struct xhci_hid_keyboard *kbd) {
    uint32_t report_size;

    if (kbd == 0 || kbd->dev == 0 || kbd->interrupt_in_ring == 0 || kbd->report == 0) {
        return 0;
    }
    if (kbd->interrupt_pending) {
        return 1;
    }
    if (!xhci_select_controller(kbd->dev->controller_index)) {
        return 0;
    }
    report_size = xhci_hid_report_transfer_size(kbd);
    memset(kbd->report, 0, report_size);
    (void)xhci_transfer_ring_trb(kbd->interrupt_in_ring,
                                 kbd->interrupt_in_ring_phys,
                                 &kbd->interrupt_in_enqueue,
                                 &kbd->interrupt_in_cycle,
                                 kbd->report_phys,
                                 report_size,
                                 (XHCI_TRB_NORMAL << 10) | (1u << 5) | (1u << 2));
    kbd->interrupt_pending = 1u;
    xhci_write32(g_xhci.doorbell, (uint32_t)kbd->dev->slot_id * 4u, kbd->interrupt_in_epid);
    xhci_save_active_controller();
    return 1;
}

int xhci_hid_poll_interrupt_report(struct xhci_hid_keyboard *kbd, uint8_t report[8]) {
    uint32_t completion = 0u;
    uint8_t from_queue = 0u;

    if (kbd == 0 || kbd->dev == 0 || report == 0 || kbd->interrupt_in_ring == 0 || kbd->report == 0) {
        return 0;
    }
    if (!xhci_select_controller(kbd->dev->controller_index)) {
        return 0;
    }
    if (xhci_hid_pop_queued_report(kbd, report, &completion)) {
        from_queue = 1u;
        goto complete_transfer;
    }
    if (!xhci_hid_submit_interrupt_report(kbd)) {
        xhci_save_active_controller();
        return 0;
    }
    if (!xhci_wait_transfer_event_spins(kbd->dev->slot_id,
                                        kbd->interrupt_in_epid,
                                        &completion,
                                        0u,
                                        XHCI_HID_REPORT_WAIT_SPINS)) {
        xhci_save_active_controller();
        return 0;
    }
    kbd->interrupt_pending = 0u;
complete_transfer:
    if (completion != XHCI_CC_SUCCESS && completion != XHCI_CC_SHORT_PACKET) {
        if (!kbd->report_fail_logged) {
            kprint("xhci: hidkbd interrupt completion cc=%u epid=%u\n",
                   completion,
                   (uint32_t)kbd->interrupt_in_epid);
            kbd->report_fail_logged = 1u;
        }
        xhci_hid_release_all_keys(kbd);
        (void)xhci_hid_submit_interrupt_report(kbd);
        xhci_save_active_controller();
        return 0;
    }
    if (!from_queue) {
        xhci_hid_copy_report(kbd, report);
    }
    (void)xhci_hid_submit_interrupt_report(kbd);
    xhci_save_active_controller();
    return 1;
}

static void xhci_hid_queue_event(struct keyboard_event event);
static int xhci_hid_report_contains_usage(const uint8_t report[8], uint8_t usage);

static int xhci_hid_set_leds(struct xhci_hid_keyboard *kbd, uint8_t led_state) {
    uint8_t output = led_state & 0x07u;

    if (kbd == 0 || kbd->dev == 0) {
        return 0;
    }
    if (xhci_control_transfer(kbd->dev,
                              0x21u,
                              USB_REQ_SET_REPORT,
                              0x0200u,
                              kbd->interface_number,
                              &output,
                              1u,
                              0u)) {
        kbd->led_state = output;
        kbd->led_fail_logged = 0u;
        return 1;
    }
    if (!kbd->led_fail_logged) {
        kprint("xhci: hidkbd LED update failed iface=%u leds=%x\n",
               (uint32_t)kbd->interface_number,
               (uint32_t)output);
        kbd->led_fail_logged = 1u;
    }
    return 0;
}

static int xhci_keycode_is_lock(enum keyboard_keycode keycode) {
    return keycode == KEYBOARD_KEY_CAPS_LOCK ||
           keycode == KEYBOARD_KEY_NUM_LOCK ||
           keycode == KEYBOARD_KEY_SCROLL_LOCK;
}

static void xhci_hid_queue_key_event(struct xhci_hid_keyboard *kbd,
                                     enum keyboard_keycode keycode,
                                     int pressed) {
    uint8_t old_leds = keyboard_led_state();
    struct keyboard_event event = keyboard_handle_keycode(keycode, pressed);
    uint8_t new_leds = keyboard_led_state();

    xhci_hid_queue_event(event);
    if (pressed && old_leds != new_leds && xhci_keycode_is_lock(keycode)) {
        (void)xhci_hid_set_leds(kbd, new_leds);
    }
}

static void xhci_hid_set_repeat_usage(struct xhci_hid_keyboard *kbd, uint8_t usage) {
    if (kbd == 0 || kbd->repeat_usage == usage) {
        return;
    }
    kbd->repeat_usage = usage;
    kbd->repeat_active = usage != 0u ? 1u : 0u;
    kbd->repeat_ticks = 0u;
}

static void xhci_hid_tick_repeat(struct xhci_hid_keyboard *kbd) {
    uint32_t repeat_age;

    if (kbd == 0 || !kbd->present || kbd->poll_disabled || !kbd->repeat_active || kbd->repeat_usage == 0u) {
        return;
    }
    if (!xhci_hid_report_contains_usage(kbd->last_report, kbd->repeat_usage)) {
        xhci_hid_set_repeat_usage(kbd, 0u);
        return;
    }
    kbd->repeat_ticks++;
    if (kbd->repeat_ticks < XHCI_HID_REPEAT_DELAY_TICKS) {
        return;
    }
    repeat_age = kbd->repeat_ticks - XHCI_HID_REPEAT_DELAY_TICKS;
    if ((repeat_age % XHCI_HID_REPEAT_RATE_TICKS) != 0u) {
        return;
    }
    xhci_hid_queue_key_event(kbd, usb_hid_usage_to_keycode(kbd->repeat_usage), 1);
}

void xhci_hid_tick_repeats_once(void) {
    uint32_t tick = hal_timer_current_ticks();

    if (tick == g_hid_last_repeat_tick) {
        return;
    }
    g_hid_last_repeat_tick = tick;
    for (uint32_t i = 0u; i < g_hid_keyboard_count; i++) {
        xhci_hid_tick_repeat(&g_hid_keyboards[i]);
    }
}

int xhci_hid_pop_event(struct keyboard_event *out) {
    if (out == 0 || g_hid_event_count == 0u) {
        return 0;
    }
    *out = g_hid_event_queue[g_hid_event_tail];
    g_hid_event_tail = (g_hid_event_tail + 1u) % XHCI_HID_EVENT_QUEUE_SIZE;
    g_hid_event_count--;
    return 1;
}

static int xhci_hid_report_contains_usage(const uint8_t report[8], uint8_t usage) {
    for (uint32_t i = 2u; i < 8u; i++) {
        if (report[i] == usage) {
            return 1;
        }
    }
    return 0;
}

static void xhci_hid_queue_event(struct keyboard_event event) {
    if (event.keycode == KEYBOARD_KEY_NONE) {
        return;
    }
    if (g_hid_event_count >= XHCI_HID_EVENT_QUEUE_SIZE) {
        g_hid_event_tail = (g_hid_event_tail + 1u) % XHCI_HID_EVENT_QUEUE_SIZE;
        g_hid_event_count--;
    }
    g_hid_event_queue[g_hid_event_head] = event;
    g_hid_event_head = (g_hid_event_head + 1u) % XHCI_HID_EVENT_QUEUE_SIZE;
    g_hid_event_count++;
}

int xhci_hid_defer_transfer_event(uint8_t slot_id, uint8_t endpoint_id, uint32_t completion) {
    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        struct xhci_hid_keyboard *kbd = &g_hid_keyboards[i];

        if (!kbd->present ||
            !kbd->interrupt_pending ||
            kbd->dev == 0 ||
            kbd->dev->controller_index != g_xhci_active_controller ||
            kbd->dev->slot_id != slot_id ||
            kbd->interrupt_in_epid != endpoint_id) {
            continue;
        }
        kbd->interrupt_pending = 0u;
        xhci_hid_queue_report(kbd, completion);
        (void)xhci_hid_submit_interrupt_report(kbd);
        return 1;
    }
    for (uint32_t i = 0u; i < g_hid_mouse_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        struct xhci_hid_keyboard *mouse = &g_hid_mice[i];

        if (!mouse->present ||
            !mouse->interrupt_pending ||
            mouse->dev == 0 ||
            mouse->dev->controller_index != g_xhci_active_controller ||
            mouse->dev->slot_id != slot_id ||
            mouse->interrupt_in_epid != endpoint_id) {
            continue;
        }
        mouse->interrupt_pending = 0u;
        xhci_hid_queue_report(mouse, completion);
        (void)xhci_hid_submit_interrupt_report(mouse);
        return 1;
    }
    return 0;
}

void xhci_hid_release_all_keys(struct xhci_hid_keyboard *kbd) {
    uint8_t empty_report[8];
    uint8_t had_keys = 0u;

    if (kbd == 0) {
        return;
    }
    for (uint32_t i = 0u; i < 8u; i++) {
        if (kbd->last_report[i] != 0u) {
            had_keys = 1u;
            break;
        }
    }
    if (had_keys) {
        memset(empty_report, 0, sizeof(empty_report));
        xhci_hid_process_report(kbd, empty_report);
    }
    kbd->repeat_active = 0u;
    kbd->repeat_usage = 0u;
    kbd->repeat_ticks = 0u;
    memset(kbd->last_report, 0, sizeof(kbd->last_report));
}

void xhci_hid_process_report(struct xhci_hid_keyboard *kbd, const uint8_t report[8]) {
    static const uint8_t modifier_usages[8] = {0xe0u, 0xe1u, 0xe2u, 0xe3u, 0xe4u, 0xe5u, 0xe6u, 0xe7u};
    uint8_t repeat_usage;

    if (kbd == 0 || report == 0) {
        return;
    }
    repeat_usage = kbd->repeat_usage;
    for (uint32_t i = 0u; i < 8u; i++) {
        uint8_t mask = (uint8_t)(1u << i);
        enum keyboard_keycode keycode = usb_hid_usage_to_keycode(modifier_usages[i]);

        if (((kbd->last_report[0] ^ report[0]) & mask) == 0u) {
            continue;
        }
        xhci_hid_queue_key_event(kbd, keycode, (report[0] & mask) != 0u);
    }
    for (uint32_t i = 2u; i < 8u; i++) {
        uint8_t usage = kbd->last_report[i];

        if (usage != 0u && !xhci_hid_report_contains_usage(report, usage)) {
            xhci_hid_queue_key_event(kbd, usb_hid_usage_to_keycode(usage), 0);
        }
    }
    for (uint32_t i = 2u; i < 8u; i++) {
        uint8_t usage = report[i];

        if (usage != 0u && !xhci_hid_report_contains_usage(kbd->last_report, usage)) {
            xhci_hid_queue_key_event(kbd, usb_hid_usage_to_keycode(usage), 1);
            if (usb_hid_usage_can_repeat(usage)) {
                repeat_usage = usage;
            }
        }
    }
    if (repeat_usage == 0u) {
        repeat_usage = usb_hid_first_repeat_usage(report);
    } else if (!xhci_hid_report_contains_usage(report, repeat_usage)) {
        repeat_usage = usb_hid_first_repeat_usage(report);
    }
    xhci_hid_set_repeat_usage(kbd, repeat_usage);
    memcpy(kbd->last_report, report, 8u);
}

int xhci_probe_hid_keyboard(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length) {
    struct xhci_hid_keyboard *kbd;
    uint32_t kbd_index = XHCI_MAX_HID_KEYBOARDS;

    if (dev == 0 || cfg == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < g_hid_keyboard_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        if (!g_hid_keyboards[i].present) {
            kbd_index = i;
            break;
        }
    }
    if (kbd_index == XHCI_MAX_HID_KEYBOARDS && g_hid_keyboard_count < XHCI_MAX_HID_KEYBOARDS) {
        kbd_index = g_hid_keyboard_count;
        g_hid_keyboard_count++;
    }
    if (kbd_index == XHCI_MAX_HID_KEYBOARDS) {
        kprint("xhci: HID keyboard table full max=%u\n", XHCI_MAX_HID_KEYBOARDS);
        return 0;
    }
    kbd = &g_hid_keyboards[kbd_index];
    memset(kbd, 0, sizeof(*kbd));
    if (!xhci_parse_hid_keyboard_config(dev, kbd, cfg, length)) {
        return 0;
    }
    if (!xhci_control_set_configuration(dev, cfg[5])) {
        kprint("xhci: slot%u HID set config failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    kbd->dev = dev;
    if (kbd->boot_protocol && !xhci_hid_set_protocol(kbd, 0u)) {
        kprint("xhci: slot%u HID set protocol failed\n", (uint32_t)dev->slot_id);
    } else if (!kbd->boot_protocol) {
        kprint("xhci: slot%u HID non-boot fallback iface=%u subclass=%u proto=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)kbd->interface_number,
               (uint32_t)kbd->interface_subclass,
               (uint32_t)kbd->interface_protocol);
    }
    if (!xhci_hid_set_idle(kbd)) {
        kprint("xhci: slot%u HID set idle failed\n", (uint32_t)dev->slot_id);
    }
    if (!xhci_alloc_hid_keyboard_resources(kbd)) {
        kprint("xhci: slot%u HID resource allocation failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    if (!xhci_configure_hid_interrupt_endpoint(kbd)) {
        kprint("xhci: slot%u HID endpoint config failed epid=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)kbd->interrupt_in_epid);
        return 0;
    }
    (void)xhci_hid_set_leds(kbd, keyboard_led_state());
    kbd->present = 1u;
    (void)xhci_hid_submit_interrupt_report(kbd);
    kprint("xhci: slot%u hidkbd%u iface=%u in=%x epid=%u mps=%u interval=%u report=%u boot=%u\n",
           (uint32_t)dev->slot_id,
           kbd_index,
           (uint32_t)kbd->interface_number,
           (uint32_t)kbd->interrupt_in_ep,
           (uint32_t)kbd->interrupt_in_epid,
           (uint32_t)kbd->interrupt_in_mps,
           (uint32_t)kbd->interrupt_in_interval,
           (uint32_t)kbd->report_size,
           (uint32_t)kbd->boot_protocol);
    return 1;
}

static void xhci_hid_mouse_process_report(struct xhci_hid_keyboard *mouse,
                                          const uint8_t report[8],
                                          uint32_t tick) {
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

int xhci_probe_hid_mouse(struct xhci_enum_device *dev, const uint8_t *cfg, uint32_t length) {
    struct xhci_hid_keyboard *mouse;
    uint32_t mouse_index = XHCI_MAX_HID_KEYBOARDS;
    uint8_t new_slot = 0u;

    if (dev == 0 || cfg == 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < g_hid_mouse_count && i < XHCI_MAX_HID_KEYBOARDS; i++) {
        if (!g_hid_mice[i].present) {
            mouse_index = i;
            break;
        }
    }
    if (mouse_index == XHCI_MAX_HID_KEYBOARDS && g_hid_mouse_count < XHCI_MAX_HID_KEYBOARDS) {
        mouse_index = g_hid_mouse_count;
        new_slot = 1u;
    }
    if (mouse_index == XHCI_MAX_HID_KEYBOARDS) {
        kprint("xhci: HID mouse table full max=%u\n", XHCI_MAX_HID_KEYBOARDS);
        return 0;
    }
    mouse = &g_hid_mice[mouse_index];
    memset(mouse, 0, sizeof(*mouse));
    if (!xhci_parse_hid_mouse_config(dev, mouse, cfg, length)) {
        return 0;
    }
    if (!xhci_control_set_configuration(dev, cfg[5])) {
        kprint("xhci: slot%u HID mouse set config failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    mouse->dev = dev;
    if (!xhci_hid_set_protocol(mouse, 0u)) {
        kprint("xhci: slot%u HID mouse set protocol failed\n", (uint32_t)dev->slot_id);
    }
    if (!xhci_hid_set_idle(mouse)) {
        kprint("xhci: slot%u HID mouse set idle failed\n", (uint32_t)dev->slot_id);
    }
    if (!xhci_alloc_hid_keyboard_resources(mouse)) {
        kprint("xhci: slot%u HID mouse resource allocation failed\n", (uint32_t)dev->slot_id);
        return 0;
    }
    if (!xhci_configure_hid_interrupt_endpoint(mouse)) {
        kprint("xhci: slot%u HID mouse endpoint config failed epid=%u\n",
               (uint32_t)dev->slot_id,
               (uint32_t)mouse->interrupt_in_epid);
        return 0;
    }
    mouse->present = 1u;
    if (new_slot) {
        g_hid_mouse_count = mouse_index + 1u;
    }
    (void)xhci_hid_submit_interrupt_report(mouse);
    kprint("xhci: slot%u hidmouse%u iface=%u in=%x epid=%u mps=%u interval=%u report=%u\n",
           (uint32_t)dev->slot_id,
           mouse_index,
           (uint32_t)mouse->interface_number,
           (uint32_t)mouse->interrupt_in_ep,
           (uint32_t)mouse->interrupt_in_epid,
           (uint32_t)mouse->interrupt_in_mps,
           (uint32_t)mouse->interrupt_in_interval,
           (uint32_t)mouse->report_size);
    return 1;
}

void xhci_poll_mouse_events(uint32_t tick) {
    uint8_t report[8];

    if (!xhci_try_begin_busy()) {
        return;
    }
    xhci_hotplug_poll();
    for (uint32_t i = 0u; i < g_hid_mouse_count; i++) {
        struct xhci_hid_keyboard *mouse = &g_hid_mice[i];

        if (!mouse->present || mouse->poll_disabled) {
            continue;
        }
        memset(report, 0, sizeof(report));
        if (!xhci_hid_poll_interrupt_report(mouse, report)) {
            continue;
        }
        mouse->report_fail_logged = 0u;
        mouse->poll_fail_count = 0u;
        xhci_hid_mouse_process_report(mouse, report, tick);
    }
    xhci_end_busy();
}
