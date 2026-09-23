#include "arch/x86/i386/services/shared_services.h"
#include "kernel/internal/core/kernel_config_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/boot_user_init.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/sys/system_limits.h"
#include "lib/string.h"

static char g_boot_user_init_path[NOS_PATH_BUFFER_SIZE];
static char g_boot_user_shell_command[NOS_PATH_BUFFER_SIZE + 40u];
static char g_boot_user_shell_start_log[NOS_PATH_BUFFER_SIZE + 40u];
static char g_boot_user_shell_fail_log[NOS_PATH_BUFFER_SIZE + 40u];
static char g_boot_user_shell_exit_log[NOS_PATH_BUFFER_SIZE + 40u];

static void boot_user_copy_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void boot_user_append_text(char *dst, uint32_t dst_size, const char *src) {
    uint32_t len;
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    len = str_len(dst);
    while (src != 0 && src[i] != '\0' && len + 1u < dst_size) {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
}

static void boot_user_copy_path(char *dst, uint32_t dst_size, const char *path) {
    if (dst == 0 || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (path == 0 || path[0] == '\0') {
        boot_user_copy_text(dst, dst_size, "/system/init");
        return;
    }
    if (path[0] != '/') {
        boot_user_copy_text(dst, dst_size, "/");
    }
    boot_user_append_text(dst, dst_size, path);
}

static void boot_user_build_command(const char *path) {
    boot_user_copy_text(g_boot_user_shell_command,
                        sizeof(g_boot_user_shell_command),
                        "/cmd/ush --tty /dev/tty --init ");
    boot_user_append_text(g_boot_user_shell_command,
                          sizeof(g_boot_user_shell_command),
                          path);
}

static void boot_user_build_path_log(char *dst,
                                     uint32_t dst_size,
                                     const char *prefix,
                                     const char *path) {
    boot_user_copy_text(dst, dst_size, prefix);
    boot_user_append_text(dst, dst_size, path);
    boot_user_append_text(dst, dst_size, "\n");
}

static void boot_user_set_init_path(struct boot_user_init_config *config,
                                    const char *path) {
    if (config == 0) {
        return;
    }
    boot_user_copy_path(g_boot_user_init_path,
                        sizeof(g_boot_user_init_path),
                        path);
    boot_user_build_command(g_boot_user_init_path);
    boot_user_build_path_log(g_boot_user_shell_start_log,
                             sizeof(g_boot_user_shell_start_log),
                             "kernel: init starting path=",
                             g_boot_user_init_path);
    boot_user_build_path_log(g_boot_user_shell_fail_log,
                             sizeof(g_boot_user_shell_fail_log),
                             "kernel: init failed path=",
                             g_boot_user_init_path);
    boot_user_build_path_log(g_boot_user_shell_exit_log,
                             sizeof(g_boot_user_shell_exit_log),
                             "kernel: init complete path=",
                             g_boot_user_init_path);
    config->shell_command = g_boot_user_shell_command;
    config->shell_start_log = g_boot_user_shell_start_log;
    config->shell_fail_log = g_boot_user_shell_fail_log;
    config->shell_exit_log = g_boot_user_shell_exit_log;
    config->shell_init_path = g_boot_user_init_path;
}

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
    config->shell_log_prefix = "init";
    config->shell_vfs = shared_services_active_vfs();
    config->shell_probe_init_path = 1;
    config->shell_pre_start_log = "kernel: virtual tty shells managed by service\n";
    config->verbose_selftest = boot_flags_dev_selftest_enabled();
    boot_user_set_init_path(config, "/system/init");
}

void boot_user_services_apply_kernel_config(struct boot_user_init_config *config) {
    struct kernel_config kernel_config;
    struct vfs *vfs;

    if (config == 0) {
        return;
    }
    vfs = shared_services_active_vfs();
    kernel_config_load(vfs, "/system/config/nex.scf", &kernel_config);
    if (!kernel_config.loaded) {
        kernel_config_load(vfs, "SYSTEM/CONFIG/NOS.CFG", &kernel_config);
    }
    if (!kernel_config.loaded) {
        kernel_config_load(vfs, "NOS.CFG", &kernel_config);
    }
    if (!kernel_config.loaded) {
        kernel_config_load(vfs, "NEXOS.CFG", &kernel_config);
    }
    if (!kernel_config.loaded) {
        return;
    }

    boot_services_log("kernel: config loaded");
    if (kernel_config.init_path_set) {
        boot_user_set_init_path(config, kernel_config.init_path);
    }
    syscall_common_request_core_set_root_token(kernel_config.security_root_token);
    boot_services_log(kernel_config.ring3_smoke
                          ? "kernel: ring3 smoke unsupported"
                          : "kernel: ring3 smoke skip");
}
