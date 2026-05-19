#pragma once

#include <stdint.h>
#include "kernel/public/input/keyboard_types.h"

struct console;

void device_poll_init_input(void);
void device_poll_set_mouse_cursor_enabled(uint8_t enabled);
void device_poll_set_mouse_selection_console(struct console *console);
void device_poll_serial_write(const char *text);
void device_poll_push_keyboard_event(const struct keyboard_event *event, volatile uint32_t *ticks);
int device_poll_poll_usb_keyboard_event(struct keyboard_event *out);
int device_poll_poll_uart_keyboard_event(struct keyboard_event *out);
void device_poll_poll_usb_mouse_events(volatile uint32_t *ticks);
void device_poll_update_mouse_cursor(void);
struct keyboard_event device_poll_read_ps2_keyboard_event(void);
void device_poll_handle_mouse_irq(volatile uint32_t *ticks);
void device_poll_handle_uart_irq(void);
void device_poll_handle_network_irq(uint8_t irq_line);
