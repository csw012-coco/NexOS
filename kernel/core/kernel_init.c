#include <stdint.h>
#include "hal/hal.h"
#include "bootx.h"
#include "fs/vfs.h"
#include "kernel/internal/core/kernel_boot_internal.h"
#include "kernel/internal/core/kernel_config_internal.h"
#include "kernel/internal/core/kernel_init_internal.h"
#include "drivers/input/mouse.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/public/core/console.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/sched_policy.h"
#include "kernel/public/mem/vmm.h"

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

struct vfs *kernel_init_core_services(struct tty *shell_tty, volatile uint32_t *timer_ticks) {
    if (shell_tty == 0 || timer_ticks == 0) {
        return 0;
    }

    process_init(shell_tty, timer_ticks);
    sched_policy_init();  /* Initialize scheduler policy layer (SOSP-18) */
    return kernel_bootstrap_vfs();
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
    mouse_init();
    hal_cpu_sti();
}

int kernel_try_run_init(struct vfs *vfs,
                        struct tty *shell_tty,
                        uint16_t *boot_trace_row,
                        const struct bootx_boot_info *boot_info) {
    char init_path[NOS_PATH_BUFFER_SIZE];
    struct vfs_node init_probe_node;
    uint32_t init_probe_offset = 0;
    uint32_t init_probe_bytes = 0;
    uint32_t init_probe_open_ok = 0;
    uint32_t init_probe_kind = 0;
    uint32_t init_probe_size = 0;
    uint8_t init_probe_buffer[64];
    struct kernel_config config;
    const char *cmdline;
    int started;
    int ring3_smoke_ok;

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

    kernel_config_load(vfs, "SYSTEM/CONFIG/NOS.CFG", &config);
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
    if (vfs_open(vfs, init_path, 0, &init_probe_node) == 0 && init_probe_node.kind == VFS_NODE_FILE) {
        init_probe_open_ok = 1;
        init_probe_kind = init_probe_node.mount_kind;
        init_probe_size = vfs_node_file_size(&init_probe_node);
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init probe ok");
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init probe kind", init_probe_kind);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init probe fsize", init_probe_size);
        init_probe_bytes = (uint32_t)vfs_read(vfs,
                                              &init_probe_node,
                                              &init_probe_offset,
                                              init_probe_buffer,
                                              4u,
                                              SYS_READ_BLOCKING);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init probe read", init_probe_bytes);
    } else {
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init probe open fail");
    }
    tty_set_cursor(shell_tty,
                   *boot_trace_row < console_rows() ? *boot_trace_row : (uint16_t)(console_rows() - 1u),
                   0);
    started = process_exec(vfs, init_path, 0, PROCESS_EXEC_AUTO);
    if (!started) {
        uint32_t error = process_last_error();
        struct vmm_page_fault_trace trace;
        struct vmm_page_clone_trace clone_trace;
        uint64_t phys = 0;
        uint64_t flags = 0;
        uint64_t pml4e = 0;
        uint64_t pdpte = 0;
        uint64_t pde = 0;
        uint64_t pte = 0;

        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init exec failed");
        kernel_boot_trace(shell_tty, boot_trace_row, "kernel: init err code");
        kernel_boot_trace_error_code(shell_tty, boot_trace_row, error);
        vmm_get_page_fault_trace(&trace);
        vmm_get_page_clone_trace(&clone_trace);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cur cr3", vmm_current_root());
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl src", clone_trace.source_cr3);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl dst", clone_trace.clone_cr3);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl s e0", clone_trace.source_pml4e0);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl s511", clone_trace.source_pml4e511);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl d e0", clone_trace.clone_pml4e0);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl d511", clone_trace.clone_pml4e511);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl fail v", clone_trace.fail_virt);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl fail p", clone_trace.fail_phys);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: cl fail s", clone_trace.fail_stage);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: sw req", trace.requested_cr3);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: sw act", trace.actual_cr3);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: sw rej", trace.reject_flags);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: sw rip", trace.current_rip);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: sw rsp", trace.current_rsp);
        if (vmm_query_mapping_in_context(trace.requested_cr3, trace.current_rip, &phys, &flags)) {
            kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rip phys", phys);
            kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rip flg", flags);
        } else {
            kernel_boot_trace(shell_tty, boot_trace_row, "kernel: req rip unmapped");
        }
        (void)vmm_query_page_walk_in_context(trace.requested_cr3,
                                             trace.current_rip,
                                             &pml4e,
                                             &pdpte,
                                             &pde,
                                             &pte);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rip pml4", pml4e);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rip pdpt", pdpte);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rip pde", pde);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rip pte", pte);
        if (vmm_query_mapping_in_context(trace.requested_cr3, trace.current_rsp, &phys, &flags)) {
            kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rsp phys", phys);
            kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rsp flg", flags);
        } else {
            kernel_boot_trace(shell_tty, boot_trace_row, "kernel: req rsp unmapped");
        }
        pml4e = 0;
        pdpte = 0;
        pde = 0;
        pte = 0;
        (void)vmm_query_page_walk_in_context(trace.requested_cr3,
                                             trace.current_rsp,
                                             &pml4e,
                                             &pdpte,
                                             &pde,
                                             &pte);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rsp pml4", pml4e);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rsp pdpt", pdpte);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rsp pde", pde);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: req rsp pte", pte);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final err", error);
        kernel_boot_trace_hex64(shell_tty, boot_trace_row, "kernel: init final swrej", trace.reject_flags);
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
