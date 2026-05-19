#include "kernel/internal/core/device_poll_internal.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"
#include "drivers/net/rtl8139.h"
#include "drivers/serial/uart.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/xhci.h"
#include "hal/hal.h"
#include "kernel/public/core/console.h"
#include "lib/string.h"

static uint8_t g_device_poll_mouse_cursor_enabled;
static uint32_t g_device_poll_mouse_cursor_seq;
static uint8_t g_device_poll_mouse_buttons;
static uint8_t g_device_poll_mouse_cell_valid;
static uint16_t g_device_poll_mouse_row;
static uint16_t g_device_poll_mouse_col;
static struct console *g_device_poll_mouse_selection_console;

static void device_poll_mouse_cursor_event(const struct mouse_event_record *event) {
    uint16_t row;
    uint16_t col;
    uint8_t old_buttons;
    uint8_t left_down;
    uint8_t left_was_down;

    if (!g_device_poll_mouse_cursor_enabled || event == 0) {
        return;
    }
    g_device_poll_mouse_cursor_seq = event->seq;
    if (event->dx != 0 || event->dy != 0) {
        hal_display_move_mouse_cursor(event->dx, event->dy);
    }
    old_buttons = g_device_poll_mouse_buttons;
    g_device_poll_mouse_buttons = event->buttons;
    if (hal_display_mouse_cursor_cell(&row, &col)) {
        g_device_poll_mouse_row = row;
        g_device_poll_mouse_col = col;
        g_device_poll_mouse_cell_valid = 1u;
    } else if (g_device_poll_mouse_cell_valid) {
        row = g_device_poll_mouse_row;
        col = g_device_poll_mouse_col;
    } else {
        return;
    }
    if (g_device_poll_mouse_selection_console == 0) {
        return;
    }
    if ((event->buttons & 0x02u) != 0u && (old_buttons & 0x02u) == 0u) {
        console_mouse_select_clear(g_device_poll_mouse_selection_console);
        return;
    }
    left_down = (event->buttons & 0x01u) != 0u;
    left_was_down = (old_buttons & 0x01u) != 0u;
    if (left_down && !left_was_down) {
        console_mouse_select_begin(g_device_poll_mouse_selection_console, row, col);
    } else if (left_down) {
        console_mouse_select_update(g_device_poll_mouse_selection_console, row, col);
    } else if (!left_down && left_was_down) {
        console_mouse_select_end(g_device_poll_mouse_selection_console, row, col);
    }
}

void device_poll_init_input(void) {
    mouse_init();
    uart_enable_input();
}

void device_poll_set_mouse_cursor_enabled(uint8_t enabled) {
    g_device_poll_mouse_cursor_enabled = enabled != 0u ? 1u : 0u;
    g_device_poll_mouse_cursor_seq = mouse_event_latest_seq();
    mouse_set_event_callback(g_device_poll_mouse_cursor_enabled ? device_poll_mouse_cursor_event : 0);
    hal_display_set_mouse_cursor_enabled(g_device_poll_mouse_cursor_enabled);
}

void device_poll_set_mouse_selection_console(struct console *console) {
    g_device_poll_mouse_selection_console = console;
}

void device_poll_serial_write(const char *text) {
    uart_write(text);
}

void device_poll_push_keyboard_event(const struct keyboard_event *event, volatile uint32_t *ticks) {
    uint32_t tick = 0;

    if (ticks != 0) {
        tick = *ticks;
    }
    keyboard_event_queue_push(event, tick);
}

int device_poll_poll_usb_keyboard_event(struct keyboard_event *out) {
    if (ehci_poll_keyboard_event(out)) {
        return 1;
    }
    return xhci_poll_keyboard_event(out);
}

static struct keyboard_event device_poll_uart_char_to_key_event(char ch) {
    struct keyboard_event event;

    memset(&event, 0, sizeof(event));
    event.keycode = KEYBOARD_KEY_SPACE;
    event.ascii = ch;
    event.pressed = 1u;
    if (ch == '\r' || ch == '\n') {
        event.keycode = KEYBOARD_KEY_ENTER;
        event.ascii = '\n';
    } else if (ch == '\b' || ch == 0x7f) {
        event.keycode = KEYBOARD_KEY_BACKSPACE;
        event.ascii = 0;
    } else if (ch == '\t') {
        event.keycode = KEYBOARD_KEY_TAB;
        event.ascii = '\t';
    } else if (ch == 0x03) {
        event.keycode = KEYBOARD_KEY_C;
        event.ctrl = 1u;
        event.ascii = 0;
    } else if (ch == 0x1a) {
        event.keycode = KEYBOARD_KEY_Z;
        event.ctrl = 1u;
        event.ascii = 0;
    }
    return event;
}

int device_poll_poll_uart_keyboard_event(struct keyboard_event *out) {
    char ch;

    if (out == 0 || !uart_pop_console_char(&ch)) {
        return 0;
    }
    *out = device_poll_uart_char_to_key_event(ch);
    return 1;
}

void device_poll_poll_usb_mouse_events(volatile uint32_t *ticks) {
    uint32_t tick = 0;

    if (ticks != 0) {
        tick = *ticks;
    }
    mouse_poll_ps2(tick);
    ehci_hotplug_poll();
    ehci_poll_mouse_events(tick);
    xhci_poll_mouse_events(tick);
    device_poll_update_mouse_cursor();
}

void device_poll_update_mouse_cursor(void) {
    struct mouse_event_record event;

    if (!g_device_poll_mouse_cursor_enabled) {
        return;
    }
    while (mouse_event_get_after(&g_device_poll_mouse_cursor_seq, &event)) {
        hal_display_move_mouse_cursor(event.dx, event.dy);
    }
}

struct keyboard_event device_poll_read_ps2_keyboard_event(void) {
    return keyboard_handle_scancode(hal_keyboard_read_scancode());
}

void device_poll_handle_mouse_irq(volatile uint32_t *ticks) {
    uint32_t tick = 0;

    if (ticks != 0) {
        tick = *ticks;
    }
    mouse_handle_data(hal_io_in8(0x60), tick);
    device_poll_update_mouse_cursor();
}

void device_poll_handle_uart_irq(void) {
    uart_poll_input();
}

void device_poll_handle_network_irq(uint8_t irq_line) {
    (void)rtl8139_handle_irq(irq_line);
}
