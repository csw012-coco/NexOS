#pragma once

#include <stdint.h>
#include "kernel/public/input/keyboard_types.h"

void ehci_init(void);
uint32_t ehci_msc_device_count(void);
uint32_t ehci_hid_keyboard_count(void);
int ehci_poll_keyboard_event(struct keyboard_event *out);
void ehci_poll_mouse_events(uint32_t tick);
void ehci_hotplug_poll(void);
