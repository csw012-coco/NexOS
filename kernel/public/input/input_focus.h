#pragma once

#include <stdint.h>

int input_focus_grab(uint32_t pid);
int input_focus_release(uint32_t pid);
void input_focus_clear(void);
uint32_t input_focus_owner_pid(void);
