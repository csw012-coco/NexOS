#include "kernel/internal/core/clipboard_internal.h"
#include "kernel/public/core/console.h"

static char g_kernel_clipboard[KERNEL_CLIPBOARD_TEXT_MAX + 1u];
static uint32_t g_kernel_clipboard_size;

uint32_t kernel_clipboard_set_text(const char *text, uint32_t len) {
    uint32_t copied = 0u;

    if (text == 0 || len == 0u) {
        g_kernel_clipboard[0] = '\0';
        g_kernel_clipboard_size = 0u;
        return 0u;
    }

    while (copied < len && copied < KERNEL_CLIPBOARD_TEXT_MAX && text[copied] != '\0') {
        g_kernel_clipboard[copied] = text[copied];
        copied++;
    }
    g_kernel_clipboard[copied] = '\0';
    g_kernel_clipboard_size = copied;
    return copied;
}

uint32_t kernel_clipboard_copy_console_selection(const struct console *console) {
    uint32_t needed;

    if (console == 0) {
        return 0u;
    }
    needed = console_get_selection_text(console, g_kernel_clipboard, sizeof(g_kernel_clipboard));
    if (needed >= sizeof(g_kernel_clipboard)) {
        g_kernel_clipboard_size = KERNEL_CLIPBOARD_TEXT_MAX;
    } else {
        g_kernel_clipboard_size = needed;
    }
    return g_kernel_clipboard_size;
}

const char *kernel_clipboard_text(void) {
    return g_kernel_clipboard;
}

uint32_t kernel_clipboard_size(void) {
    return g_kernel_clipboard_size;
}
