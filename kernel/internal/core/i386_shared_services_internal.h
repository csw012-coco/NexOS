#pragma once

#include "kernel/public/proc/boot_user_init.h"

struct keyboard_event;
struct tty;

void i386_boot_log(const char *text);
void i386_boot_user_init_config(struct boot_user_init_config *config);
struct tty *i386_active_tty(void);
void i386_tty_selftest_prompt(void);
int i386_tty_selftest_pop_keyboard_event(struct keyboard_event *event);

int i386_tty_input_self_test(void);
int i386_tty_utf8_edit_self_test(void);

int i386_run_test32_selftest(void);
int i386_run_nexbox32_full_smoke(void);
int i386_run_backend_smoke(int ahci,
                           int usb,
                           int usb_hid,
                           int rtl8139,
                           int hda,
                           int ac97,
                           int gfx_editor);
