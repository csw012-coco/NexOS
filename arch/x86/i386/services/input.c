#include "drivers/input/keyboard.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/xhci.h"
#include "hal/hal.h"
#include "arch/x86/i386/services/shared_services.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/process_scheduler_ops.h"

static uint32_t input_services_usb_poll_tick;

void input_services_prompt(void) {
    struct tty *tty = shared_services_active_tty();

    if (tty == 0) {
        return;
    }
    tty_write_str(tty, "i386> ", 0x0bu);
    tty_show_prompt(tty);
}

int input_services_pop_keyboard_event(struct keyboard_event *event) {
    uint8_t scancode;
    uint32_t tick = process_scheduler_ticks();

    if (event == 0) {
        return 0;
    }
    input_services_usb_poll_tick++;
    if ((input_services_usb_poll_tick & 0x3fu) == 0u) {
        ehci_hotplug_poll();
        xhci_hotplug_poll();
    }
    ehci_poll_mouse_events(tick);
    xhci_poll_mouse_events(tick);
    if (ehci_poll_keyboard_event(event)) {
        keyboard_event_queue_push(event, tick);
        return 1;
    }
    if (xhci_poll_keyboard_event(event)) {
        keyboard_event_queue_push(event, tick);
        return 1;
    }
    scancode = hal_keyboard_read_scancode();
    if (scancode == 0u) {
        return 0;
    }
    *event = keyboard_handle_scancode(scancode);
    keyboard_event_queue_push(event, tick);
    return 1;
}
