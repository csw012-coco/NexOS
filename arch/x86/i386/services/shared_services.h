#pragma once

#include <stdint.h>

#include "kernel/public/proc/boot_user_init.h"

struct janus_boot_info;
struct keyboard_event;
struct syscall_compat32_context;
struct syscall_boot_info;
struct syscall_framebuffer_info;
struct tty;
struct vfs;

void boot_services_log(const char *text);
void boot_user_services_log(const char *text);
void boot_user_services_init_config(struct boot_user_init_config *config);
void boot_user_services_apply_kernel_config(struct boot_user_init_config *config);
struct vfs *shared_services_active_vfs(void);
struct tty *shared_services_active_tty(void);
int shared_services_handle_tty_switch(const struct keyboard_event *event);
void boot_flags_init(const char *cmdline);
int boot_flags_dev_selftest_enabled(void);
int boot_flags_full_smoke_enabled(void);
int boot_flags_strict_mm_smoke_enabled(void);
int boot_flags_ahci_smoke_enabled(void);
int boot_flags_usb_smoke_enabled(void);
int boot_flags_usb_hid_smoke_enabled(void);
int boot_flags_rtl8139_smoke_enabled(void);
int boot_flags_hda_smoke_enabled(void);
int boot_flags_ac97_smoke_enabled(void);
int boot_flags_gfx_editor_smoke_enabled(void);
int boot_flags_driver_smoke_enabled(void);
void driver_services_init_builtins(int verbose);
void driver_services_init(struct vfs *vfs, int verbose);
int command_services_autostart_shell(void);
void command_services_execute(const char *line);
void input_services_prompt(void);
int input_services_pop_keyboard_event(struct keyboard_event *event);
void shared_services_query_init(const struct syscall_boot_info *info,
                                const struct syscall_framebuffer_info *fb_info,
                                const struct janus_boot_info *raw_boot_info,
                                uint32_t cmdline,
                                uint32_t memmap,
                                uint32_t memmap_count);
int shared_services_init(void);
void shared_services_run(void);
int shared_services_selftest_verbose(void);
void shared_services_syscall_context(struct syscall_compat32_context *ctx);
void shared_services_syscall_cleanup_pid(uint32_t pid);
int shared_services_syscall_page_is_shared(uint32_t pid, uint32_t user_page);
int boot_user_services_run_test_pair(struct process_snapshot *process0,
                                     struct process_snapshot *process1);
int boot_user_services_run_command_arch(const char *command,
                                        struct process_snapshot *process);
int32_t boot_user_services_spawn_command(const char *command,
                                         uint32_t mode,
                                         uint32_t flags);

int tty_selftest_input(void);
int tty_selftest_utf8_edit(void);

int smoke_services_run_test32_selftest(void);
int smoke_services_run_test32_strict_mm(void);
int smoke_services_run_nexbox32_full(void);
int smoke_services_run_backend(int ahci,
                           int usb,
                           int usb_hid,
                           int rtl8139,
                           int hda,
                           int ac97,
                           int gfx_editor);
