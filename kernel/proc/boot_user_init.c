#include "kernel/public/proc/boot_user_init.h"

#include "fs/vfs.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/tty.h"

static void boot_user_init_log(const struct boot_user_init_config *config,
                               const char *text) {
    if (config != 0 && config->ops != 0 && config->ops->early_log != 0 && text != 0) {
        config->ops->early_log(text);
    }
}

static void boot_user_init_log_path(const char *prefix, const char *path) {
    if (prefix != 0 && path != 0) {
        kprint("%s path=%s%s\n", prefix, path[0] == '/' ? "" : "/", path);
    }
}

static int boot_user_init_probe_path(const struct boot_user_init_config *config,
                                     const char *path) {
    struct vfs_node node;
    uint8_t buffer[4];
    uint32_t offset = 0;
    uint32_t bytes = 0;
    int64_t read_rc;

    if (config == 0 || config->shell_vfs == 0 || path == 0) {
        return 0;
    }
    if (vfs_open(config->shell_vfs, path, 0, &node) != 0 || node.kind != VFS_NODE_FILE) {
        kprint("kernel: init probe open fail\n");
        return 0;
    }
    read_rc = vfs_read(config->shell_vfs,
                       &node,
                       &offset,
                       buffer,
                       (uint32_t)sizeof(buffer),
                       VFS_READ_BLOCKING);
    if (read_rc > 0) {
        bytes = (uint32_t)read_rc;
    }
    kprint("kernel: init probe ok\n");
    kprint("kernel: init probe kind %lx\n", (uint64_t)node.mount_kind);
    kprint("kernel: init probe fsize %lx\n", (uint64_t)vfs_node_file_size(&node));
    kprint("kernel: init probe read %lx\n", (uint64_t)bytes);
    return 1;
}

int boot_user_init_run_selftest(const struct boot_user_init_config *config) {
    struct process_snapshot process0;
    struct process_snapshot process1;
    struct tty *tty;
    const char *prefix;

    if (config == 0 || config->ops == 0 ||
        config->ops->run_test_pair == 0) {
        return 0;
    }
    tty = config->tty;
    prefix = config->test_log_prefix != 0 ? config->test_log_prefix : "selftest";
    if (config->verbose_selftest && tty != 0) {
        tty_write_str(tty, prefix, 0x0eu);
        tty_write_str(tty, ": loading ", 0x0eu);
        tty_write_str(tty,
                      config->test_name != 0
                          ? config->test_name
                          : "user selftest",
                      0x0fu);
        tty_write_str(tty, " twice\n", 0x0eu);
    }
    if (!config->ops->run_test_pair(&process0, &process1)) {
        if (tty != 0) {
            tty_write_str(tty, prefix, 0x0cu);
            tty_write_str(tty, ": FAILED\n", 0x0cu);
        }
        boot_user_init_log(config,
                           config->test_fail_log != 0
                               ? config->test_fail_log
                               : "selftest: FAILED\n");
        return 0;
    }
    if (config->verbose_selftest && tty != 0) {
        tty_write_str(tty, prefix, 0x0au);
        tty_write_str(tty, ": PASS, pids=", 0x0au);
        tty_write_dec(tty, process0.pid, 0x0fu);
        tty_write_str(tty, ",", 0x0fu);
        tty_write_dec(tty, process1.pid, 0x0fu);
        tty_write_str(tty, "\n", 0x0fu);
    } else if (config->ops->boot_log != 0) {
        config->ops->boot_log(config->test_boot_pass_log != 0
                                  ? config->test_boot_pass_log
                                  : "selftest: PASS");
    }
    if (config->verbose_selftest) {
        boot_user_init_log(config,
                           config->test_pass_log != 0
                               ? config->test_pass_log
                               : "selftest: PASS\n");
    }
    return 1;
}

int boot_user_init_autostart_shell(const struct boot_user_init_config *config) {
    struct process_snapshot process;
    struct tty *tty;
    const char *command;
    const char *prefix;

    if (config == 0 || config->ops == 0 ||
        config->ops->run_command == 0 ||
        config->shell_command == 0) {
        return 0;
    }
    tty = config->tty;
    command = config->shell_command;
    prefix = config->shell_log_prefix != 0 ? config->shell_log_prefix : "shell";

    if (config->shell_init_path != 0) {
        boot_user_init_log_path("kernel: init", config->shell_init_path);
        if (config->shell_probe_init_path) {
            (void)boot_user_init_probe_path(config, config->shell_init_path);
        }
    }
    boot_user_init_log(config, config->shell_pre_start_log);
    boot_user_init_log(config,
                       config->shell_start_log != 0
                           ? config->shell_start_log
                           : "shell: starting\n");
    if (config->verbose_selftest && tty != 0) {
        tty_write_str(tty, prefix, 0x0au);
        tty_write_str(tty, ": starting ", 0x0au);
        tty_write_str(tty, command, 0x0fu);
        tty_write_str(tty, "\n", 0x0fu);
    }
    if (!config->ops->run_command(command, &process)) {
        if (config->verbose_selftest && tty != 0) {
            tty_write_str(tty, prefix, 0x0cu);
            tty_write_str(tty, ": ", 0x0cu);
            tty_write_str(tty, command, 0x0fu);
            tty_write_str(tty, " failed\n", 0x0cu);
        }
        boot_user_init_log(config,
                           config->shell_fail_log != 0
                               ? config->shell_fail_log
                               : "shell: failed\n");
        return 0;
    }
    if (config->verbose_selftest && tty != 0) {
        tty_write_str(tty, prefix, 0x0eu);
        tty_write_str(tty, ": exited, status=", 0x0eu);
        tty_write_dec(tty, (uint32_t)process.exit_code, 0x0fu);
        tty_write_str(tty, "\n", 0x0fu);
    }
    boot_user_init_log(config,
                       config->shell_exit_log != 0
                           ? config->shell_exit_log
                           : "shell: exited\n");
    return 1;
}

int boot_user_init_run_command(const struct boot_user_init_config *config,
                               const char *command) {
    struct process_snapshot process;
    struct tty *tty;

    if (config == 0 || config->ops == 0 ||
        config->ops->run_command == 0 ||
        command == 0 || command[0] == '\0') {
        return 0;
    }
    tty = config->tty;
    if (tty != 0) {
        tty_write_str(tty, "run: ", 0x0eu);
        tty_write_str(tty, command, 0x0fu);
        tty_write_str(tty, "\n", 0x0fu);
    }
    if (!config->ops->run_command(command, &process)) {
        if (tty != 0) {
            tty_write_str(tty, "run: failed\n", 0x0cu);
        }
        return 0;
    }
    if (tty != 0) {
        tty_write_str(tty, "run: exit=", 0x0au);
        tty_write_dec(tty, (uint32_t)process.exit_code, 0x0fu);
        tty_write_str(tty, "\n", 0x0fu);
    }
    return 1;
}
