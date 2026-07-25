#include "kernel/internal/core/tty_internal.h"
#include "hal/hal.h"

static struct tty g_virtual_ttys[TTY_VIRTUAL_COUNT];
static uint32_t g_active_tty_index;
static uint8_t g_virtual_ttys_ready;

void tty_virtual_init_all(uint16_t top_row, uint16_t bottom_row, uint8_t color) {
    for (uint32_t i = 0; i < TTY_VIRTUAL_COUNT; i++) {
        tty_init(&g_virtual_ttys[i], top_row, bottom_row, color);
        console_set_visible(&g_virtual_ttys[i].console, i == 0u);
    }
    g_active_tty_index = 0u;
    g_virtual_ttys_ready = 1u;
    console_set_visible(&g_virtual_ttys[0].console, 1);
}

struct tty *tty_virtual(uint32_t index) {
    if (index >= TTY_VIRTUAL_COUNT) {
        return NULL;
    }
    return &g_virtual_ttys[index];
}

struct tty *tty_active(void) {
    return tty_virtual(g_active_tty_index);
}

uint32_t tty_active_index(void) {
    return g_active_tty_index;
}

int tty_switch_active(uint32_t index) {
    struct tty *old_tty;
    struct tty *new_tty;

    if (!g_virtual_ttys_ready || index >= TTY_VIRTUAL_COUNT) {
        return 0;
    }
    old_tty = tty_virtual(g_active_tty_index);
    new_tty = tty_virtual(index);
    if (old_tty == NULL || new_tty == NULL) {
        return 0;
    }
    if (index != g_active_tty_index) {
        console_set_visible(&old_tty->console, 0);
        g_active_tty_index = index;
    }
    console_set_visible(&new_tty->console, 1);
    hal_display_present();
    return 1;
}
