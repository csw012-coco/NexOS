#pragma once

#include <stdint.h>

#include "kernel/public/proc/process.h"

struct tty;

struct boot_user_init_ops {
    int (*run_test_pair)(struct process_snapshot *process0,
                         struct process_snapshot *process1);
    int (*run_command)(const char *command, struct process_snapshot *process);
    void (*boot_log)(const char *text);
    void (*early_log)(const char *text);
};

struct boot_user_init_config {
    struct tty *tty;
    const struct boot_user_init_ops *ops;
    const char *test_name;
    const char *test_log_prefix;
    const char *test_pass_log;
    const char *test_fail_log;
    const char *test_boot_pass_log;
    const char *shell_command;
    const char *shell_log_prefix;
    const char *shell_start_log;
    const char *shell_fail_log;
    const char *shell_exit_log;
    int verbose_selftest;
};

int boot_user_init_run_selftest(const struct boot_user_init_config *config);
int boot_user_init_autostart_shell(const struct boot_user_init_config *config);
int boot_user_init_run_command(const struct boot_user_init_config *config,
                               const char *command);
