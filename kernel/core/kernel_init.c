#include <stdint.h>
#include "hal/hal.h"
#include "bootx/bootx.h"
#include "drivers/serial/uart.h"
#include "fs/vfs.h"
#include "kernel/internal/core/kernel_boot_internal.h"
#include "kernel/internal/core/kernel_config_internal.h"
#include "kernel/internal/core/device_poll_internal.h"
#include "kernel/internal/core/kernel_init_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/core/console.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/sched_policy.h"

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void syscall_stub(void);
extern void divide_error_stub(void);
extern void double_fault_stub(void);
extern void invalid_opcode_stub(void);
extern void general_protection_stub(void);
extern void page_fault_stub(void);

static void kernel_boot_trace(struct tty *shell_tty, uint16_t *boot_trace_row, const char *text) {
    uint16_t row;
    uint16_t col = 0;
    uint16_t width = console_width();
    uint16_t rows = console_rows();

    if (shell_tty == 0 || boot_trace_row == 0 || text == 0) {
        return;
    }
    row = *boot_trace_row;
    if (kprint_is_ready()) {
        kprint("%s\n", text);
        *boot_trace_row = tty_cursor_row(shell_tty);
        return;
    }
    if (row >= rows) {
        return;
    }
    tty_clear_row(shell_tty, row, 0x0f);
    while (*text != '\0' && col < width) {
        tty_put_at(shell_tty, row, col, *text++, 0x0f);
        col++;
    }
    if (*boot_trace_row + 1u < rows) {
        (*boot_trace_row)++;
    }
}

static void kernel_boot_trace_hex64(struct tty *shell_tty,
                                    uint16_t *boot_trace_row,
                                    const char *label,
                                    uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    uint16_t row;
    uint16_t col = 0;
    uint16_t width = console_width();
    uint16_t rows = console_rows();
    int shift;

    if (shell_tty == 0 || boot_trace_row == 0 || label == 0) {
        return;
    }
    row = *boot_trace_row;
    if (kprint_is_ready()) {
        kprint("%s %lx\n", label, value);
        *boot_trace_row = tty_cursor_row(shell_tty);
        return;
    }
    if (row >= rows) {
        return;
    }
    tty_clear_row(shell_tty, row, 0x0f);
    while (*label != '\0' && col < width) {
        tty_put_at(shell_tty, row, col++, *label++, 0x0f);
    }
    if (col < width) {
        tty_put_at(shell_tty, row, col++, ' ', 0x0f);
    }
    for (shift = 60; shift >= 0 && col < width; shift -= 4) {
        tty_put_at(shell_tty, row, col++, digits[(value >> shift) & 0xf], 0x0f);
    }
    if (*boot_trace_row + 1u < rows) {
        (*boot_trace_row)++;
    }
}

static void kernel_boot_trace_error_code(struct tty *shell_tty,
                                         uint16_t *boot_trace_row,
                                         uint32_t error) {
    uint16_t rows = console_rows();

    if (shell_tty == 0 || boot_trace_row == 0 || *boot_trace_row >= rows) {
        return;
    }
    tty_clear_row(shell_tty, *boot_trace_row, 0x0f);
    tty_put_at(shell_tty, *boot_trace_row, 0, '0' + (char)((error / 10u) % 10u), 0x0f);
    tty_put_at(shell_tty, *boot_trace_row, 1, '0' + (char)(error % 10u), 0x0f);
    if (*boot_trace_row + 1u < rows) {
        (*boot_trace_row)++;
    }
}

struct kernel_boot_trace_ctx {
    struct tty *shell_tty;
    uint16_t *boot_trace_row;
};

static void kernel_boot_trace_op_text(void *ctx, const char *text) {
    struct kernel_boot_trace_ctx *trace_ctx = (struct kernel_boot_trace_ctx *)ctx;

    if (trace_ctx == 0) {
        return;
    }
    kernel_boot_trace(trace_ctx->shell_tty, trace_ctx->boot_trace_row, text);
}

static void kernel_boot_trace_op_hex64(void *ctx, const char *label, uint64_t value) {
    struct kernel_boot_trace_ctx *trace_ctx = (struct kernel_boot_trace_ctx *)ctx;

    if (trace_ctx == 0) {
        return;
    }
    kernel_boot_trace_hex64(trace_ctx->shell_tty, trace_ctx->boot_trace_row, label, value);
}

static void kernel_copy_path_local(char *dst, const char *src, uint32_t dst_size) {
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

static int kernel_probe_init_path(struct vfs *vfs,
                                  const char *path,
                                  struct vfs_node *node_out,
                                  uint32_t *kind_out,
                                  uint32_t *size_out,
                                  uint32_t *bytes_out,
                                  uint8_t *buffer,
                                  uint32_t buffer_size) {
    struct vfs_node node;
    uint32_t offset = 0;
    uint32_t probe_size = 4u;
    int64_t read_rc;

    if (vfs == 0 || path == 0 || buffer == 0 || buffer_size == 0) {
        return 0;
    }
    if (vfs_open(vfs, path, 0, &node) != 0 || node.kind != VFS_NODE_FILE) {
        return 0;
    }
    if (node_out != 0) {
        *node_out = node;
    }
    if (kind_out != 0) {
        *kind_out = node.mount_kind;
    }
    if (size_out != 0) {
        *size_out = vfs_node_file_size(&node);
    }
    if (probe_size > buffer_size) {
        probe_size = buffer_size;
    }
    read_rc = vfs_read(vfs, &node, &offset, buffer, probe_size, VFS_READ_BLOCKING);
    if (bytes_out != 0) {
        *bytes_out = read_rc < 0 ? 0u : (uint32_t)read_rc;
    }
    return 1;
}

struct vfs *kernel_init_core_services(struct tty *shell_tty,
                                      volatile uint32_t *timer_ticks,
                                      const struct bootx_boot_info *boot_info) {
    if (shell_tty == 0 || timer_ticks == 0) {
        return 0;
    }

    process_init(shell_tty, timer_ticks);
    sched_policy_init();  /* Initialize scheduler policy layer (SOSP-18) */
    return kernel_bootstrap_vfs(boot_info);
}

void kernel_init_interrupts(void) {
    const struct hal_interrupt_handlers handlers = {
        .divide_error = divide_error_stub,
        .double_fault = double_fault_stub,
        .invalid_opcode = invalid_opcode_stub,
        .general_protection_fault = general_protection_stub,
        .page_fault = page_fault_stub,
        .irq0 = irq0_stub,
        .irq1 = irq1_stub,
        .syscall = syscall_stub,
    };

    hal_cpu_cli();
    hal_platform_init(&handlers);
    hal_timer_init(100);
    device_poll_init_input();
    hal_cpu_sti();
}

int kernel_try_run_init(struct vfs *vfs,
                        struct tty *shell_tty,
                        uint16_t *boot_trace_row,
                        const struct bootx_boot_info *boot_info) {
    char init_path[NOS_PATH_BUFFER_SIZE];
    struct vfs_node init_probe_node;
    uint32_t init_probe_bytes = 0;
    uint32_t init_probe_open_ok = 0;
    uint32_t init_probe_kind = 0;
    uint32_t init_probe_size = 0;
    uint8_t init_probe_buffer[64];
    static const char *fallback_init_paths[] = {
        "/init",
        "init",
        "/INIT",
        "INIT",
        "/system/init",
        "system/init"
    };
    struct kernel_config config;
    const char *cmdline;
    int started;
    int ring3_smoke_ok;
    uint32_t fallback_index;

    if (vfs == 0 || shell_tty == 0 || boot_trace_row == 0) {
        return 0;
    }
    if (boot_info == 0 || boot_info->cmdline == 0) {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init no boot cmdline");
        return 0;
    }
    if (!vfs_root_ready(vfs)) {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init root not ready");
        return 0;
    }

    cmdline = (const char *)(uintptr_t)boot_info->cmdline;
    if (!kernel_extract_init_path(cmdline, init_path, sizeof(init_path))) {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init path missing");
        return 0;
    }

    kernel_config_load(vfs, "/system/config/nex.scf", &config);
    if (!config.loaded) {
        kernel_config_load(vfs, "SYSTEM/CONFIG/NOS.CFG", &config);
    }
    if (!config.loaded) {
        kernel_config_load(vfs, "NOS.CFG", &config);
    }
    if (!config.loaded) {
        kernel_config_load(vfs, "NEXOS.CFG", &config);
    }
    if (config.loaded) {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: config loaded");
        if (config.init_path_set) {
            kernel_copy_path_local(init_path, config.init_path, sizeof(init_path));
        }
    }
    syscall_common_request_core_set_root_token(config.security_root_token);
    device_poll_set_mouse_cursor_enabled(config.mouse_cursor);

    if (config.ring3_smoke) {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: ring3 smoke");
        ring3_smoke_ok = process_run_ring3_smoke_test();
        kernel_boot_trace(shell_tty,
                          boot_trace_row,
                          ring3_smoke_ok ? "kernel: ring3 smoke ok" : "kernel: ring3 smoke fail");
        if (!ring3_smoke_ok) {
            kernel_boot_trace(shell_tty, boot_trace_row, "kernel: ring3 smoke err");
            kernel_boot_trace_error_code(shell_tty, boot_trace_row, process_last_error());
        }
    } else {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: ring3 smoke skip");
    }
    kernel_boot_trace(shell_tty, boot_trace_row, init_path);
    if (kernel_probe_init_path(vfs,
                               init_path,
                               &init_probe_node,
                               &init_probe_kind,
                               &init_probe_size,
                               &init_probe_bytes,
                               init_probe_buffer,
                               sizeof(init_probe_buffer))) {
        init_probe_open_ok = 1;
    } else {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init probe open fail");
        for (fallback_index = 0;
             fallback_index < sizeof(fallback_init_paths) / sizeof(fallback_init_paths[0]);
             fallback_index++) {
            if (kernel_probe_init_path(vfs,
                                       fallback_init_paths[fallback_index],
                                       &init_probe_node,
                                       &init_probe_kind,
                                       &init_probe_size,
                                       &init_probe_bytes,
                                       init_probe_buffer,
                                       sizeof(init_probe_buffer))) {
                kernel_copy_path_local(init_path,
                                       fallback_init_paths[fallback_index],
                                       sizeof(init_path));
                init_probe_open_ok = 1;
                kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init fallback");
                kernel_boot_trace(shell_tty, boot_trace_row, init_path);
                break;
            }
        }
    }
    if (init_probe_open_ok) {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init probe ok");
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init probe kind", init_probe_kind);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init probe fsize", init_probe_size);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init probe read", init_probe_bytes);
    }
    tty_set_cursor(shell_tty,
                   *boot_trace_row < console_rows() ? *boot_trace_row : (uint16_t)(console_rows() - 1u),
                   0);
    if (config.virtual_tty_shells) {
        kernel_boot_trace(shell_tty,
                          boot_trace_row,
                          "kernel: virtual tty shells managed by service");
    } else {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: virtual tty shells skip");
    }
    if (config.serial_shell) {
        kernel_boot_trace(shell_tty,
                          boot_trace_row,
                          "kernel: serial shell managed by service");
    }
    kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init starting");
    kernel_boot_trace(shell_tty, boot_trace_row, init_path);
    started = process_exec(vfs, init_path, 0, PROCESS_EXEC_AUTO);
    kernel_boot_trace(shell_tty, boot_trace_row, started
                          ? "kernel: init complete"
                          : "kernel: init returned fail");
    if (!started) {
        uint32_t error = process_last_error();
        struct kernel_boot_trace_ctx trace_ctx = {
            .shell_tty = shell_tty,
            .boot_trace_row = boot_trace_row
        };
        const struct hal_boot_trace_ops trace_ops = {
            .text = kernel_boot_trace_op_text,
            .hex64 = kernel_boot_trace_op_hex64
        };

        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init exec failed");
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init err code");
        kernel_boot_trace_error_code(shell_tty, boot_trace_row, error);
        hal_paging_log_init_exec_failure(&trace_ops, &trace_ctx);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final err", error);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final estage", g_process_exec_last_stage);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final probe", init_probe_open_ok);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final pkind", init_probe_kind);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final psize", init_probe_size);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final pread", init_probe_bytes);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final ropen", g_process_exec_read_open_rc);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final rkind", g_process_exec_read_node_kind);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final rmknd", g_process_exec_read_mount_kind);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final rsize", g_process_exec_read_file_size);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final rread", g_process_exec_read_bytes);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final rres", g_process_exec_read_result);
    }
    return started;
}
