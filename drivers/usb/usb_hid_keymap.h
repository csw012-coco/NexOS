#pragma once

#include <stdint.h>
#include "kernel/public/input/keyboard_types.h"

enum keyboard_keycode usb_hid_usage_to_keycode(uint8_t usage);
int usb_hid_keycode_can_repeat(enum keyboard_keycode keycode);
int usb_hid_usage_can_repeat(uint8_t usage);
uint8_t usb_hid_first_repeat_usage(const uint8_t report[8]);
