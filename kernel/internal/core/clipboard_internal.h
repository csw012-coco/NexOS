#pragma once

#include <stdint.h>

struct console;

enum {
    KERNEL_CLIPBOARD_TEXT_MAX = 4096u
};

uint32_t kernel_clipboard_set_text(const char *text, uint32_t len);
uint32_t kernel_clipboard_copy_console_selection(const struct console *console);
const char *kernel_clipboard_text(void);
uint32_t kernel_clipboard_size(void);
