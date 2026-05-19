#pragma once

#include <stdint.h>
#include "kernel/public/input/keyboard_types.h"

void xhci_init(void);
uint32_t xhci_port_count(void);
uint32_t xhci_connected_port_count(void);
uint32_t xhci_hid_keyboard_count(void);
int xhci_poll_keyboard_event(struct keyboard_event *out);
void xhci_poll_mouse_events(uint32_t tick);
