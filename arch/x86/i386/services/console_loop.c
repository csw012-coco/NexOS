#include "drivers/input/keyboard.h"
#include "arch/x86/i386/services/shared_services.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/public/core/tty.h"

void shared_services_run(void) {
    char line[TTY_LINE_MAX + 1u];
    struct tty *tty;

    if (command_services_autostart_shell()) {
        for (;;) {
            __asm__ volatile("sti; hlt" : : : "memory");
        }
    }
    input_services_prompt();
    for (;;) {
        struct keyboard_event event;

        __asm__ volatile("sti; hlt" : : : "memory");
        tty = shared_services_active_tty();
        if (tty == 0) {
            continue;
        }
        while (input_services_pop_keyboard_event(&event)) {
            tty = shared_services_active_tty();
            if (tty == 0) {
                break;
            }
            tty_feed_key_event(tty, &event);
        }
        if (tty_has_line(tty)) {
            (void)tty_read(tty, line, sizeof(line), TTY_READ_LINE);
            command_services_execute(line);
            input_services_prompt();
        }
    }
}
