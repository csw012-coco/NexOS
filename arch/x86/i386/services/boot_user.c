#include "arch/x86/i386/services/shared_services.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/boot_user_init.h"
#include "kernel/public/proc/process.h"

void boot_user_services_log(const char *text) {
    boot_services_log(text);
}

static void boot_user_services_early_log(const char *text) {
    kprint("%s", text);
}

void boot_user_services_init_config(struct boot_user_init_config *config) {
    static const struct boot_user_init_ops ops = {
        .run_test_pair = boot_user_services_run_test_pair,
        .run_command = boot_user_services_run_command_arch,
        .boot_log = boot_user_services_log,
        .early_log = boot_user_services_early_log,
    };

    config->tty = shared_services_active_tty();
    config->ops = &ops;
    config->test_name = "/cmd/test32";
    config->test_log_prefix = "test32";
    config->test_pass_log = "test32: PASS\n";
    config->test_fail_log = "test32: FAILED\n";
    config->test_boot_pass_log = "selftest: test32 PASS";
    config->shell_command = "/cmd/ush --tty /dev/tty --init /system/init";
    config->shell_log_prefix = "init";
    config->shell_start_log = "kernel: init starting /system/init\n";
    config->shell_fail_log = "kernel: init /system/init failed\n";
    config->shell_exit_log = "kernel: init /system/init complete\n";
    config->verbose_selftest = boot_flags_dev_selftest_enabled();
}
