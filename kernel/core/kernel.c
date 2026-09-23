#include <stdint.h>
#include "janus/janus.h"
#include "drivers/bus/ioapic.h"
#include "hal/hal.h"
#include "kernel/internal/core/device_poll_internal.h"
#include "kernel/internal/core/boot_log_internal.h"
#include "kernel/internal/core/boot_state_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/kernel_boot_internal.h"
#include "kernel/internal/core/kernel_init_internal.h"
#include "kernel/internal/core/kernel_panic_internal.h"
#include "kernel/internal/fs/file_device_backend.h"
#include "kernel/internal/fs/fs_service_fd_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/internal/proc/process_elf_internal.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/public/core/console.h"
#include "kernel/public/driver/driver.h"
#include "kernel/public/input/input_focus.h"
#include "kernel/public/mem/pmm.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/public/proc/job_control.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/proc/scheduler.h"
#include "kernel/public/proc/sched_policy.h"
#include "kernel/public/sys/syscall.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/mem/vmm.h"
#include "lib/string.h"

static volatile uint32_t timer_ticks;
static uint16_t g_kernel_boot_trace_row;
extern uint64_t g_current_user_raw_entry;

#define shell_tty (*tty_virtual(0))

enum {
    IRQ_DISPATCH_CONTINUE = 0,
    IRQ_DISPATCH_RESUME_FAULT = 1,
    IRQ_DISPATCH_RESUME_KERNEL = 0xfffffffffffffff0ull,
    IRQ_VECTOR_BASE = 32u,
    IRQ_VECTOR_TIMER = 32u,
    IRQ_VECTOR_KEYBOARD = 33u,
    IRQ_VECTOR_SERIAL = 36u,
    IRQ_VECTOR_MOUSE = 44u,
    IRQ_VECTOR_LAST = 47u,
    KERNEL_RUN_MEMORY_BENCHMARK = 0u
};

static void kernel_boot_trace(const char *text);
static void kernel_boot_settle_ticks(uint32_t ticks);

static int kernel_cmdline_has_token(const struct janus_boot_info *boot_info, const char *token) {
    const char *cmdline;
    uint32_t token_len = 0u;

    if (boot_info == 0 || boot_info->cmdline == 0u || token == 0 || token[0] == '\0') {
        return 0;
    }
    cmdline = (const char *)(uintptr_t)boot_info->cmdline;
    while (token[token_len] != '\0') {
        token_len++;
    }
    while (*cmdline != '\0') {
        while (*cmdline == ' ') {
            cmdline++;
        }
        if (*cmdline == '\0') {
            break;
        }
        if (starts_with(cmdline, token) &&
            (cmdline[token_len] == '\0' || cmdline[token_len] == ' ')) {
            return 1;
        }
        while (*cmdline != '\0' && *cmdline != ' ') {
            cmdline++;
        }
    }
    return 0;
}

static void kernel_fs_service_ensure_terminal_owner(
    const struct process *proc) {
    job_ensure_process_terminal_owner(proc);
}

static void kernel_fs_service_tick_excluding_pid(uint32_t pid) {
    sched_tick_excluding_pid(pid);
}

static int kernel_file_device_serial_foreground_allowed(void) {
    return job_serial_current_process_foreground_allowed();
}

static void kernel_register_fs_service_runtime_ops(void) {
    const struct fs_service_fd_runtime_ops ops = {
        .ensure_terminal_owner = kernel_fs_service_ensure_terminal_owner,
        .tick_excluding_pid = kernel_fs_service_tick_excluding_pid
    };

    fs_service_fd_runtime_ops_register(&ops);
}

static void kernel_register_file_device_runtime_ops(void) {
    const struct file_device_backend_runtime_ops ops = {
        .serial_foreground_allowed =
            kernel_file_device_serial_foreground_allowed
    };

    file_device_backend_runtime_ops_register(&ops);
}

static void kernel_halt_forever(void) {
    for (;;) {
        hal_display_service_pending();
        hal_cpu_halt();
    }
}

static void kernel_panic_init_exit(void) {
    tty_putc(&shell_tty, '\n', 0x0f);
    tty_write_str(&shell_tty, "KERNEL PANIC: init exited\n", 0x0f);
    device_poll_serial_write("\nKERNEL PANIC: init exited\n");
    hal_cpu_cli();
    kernel_halt_forever();
}

static void kernel_panic_after_init_return(void) {
    kernel_boot_trace("kernel: init exited");
    kernel_panic_init_exit();
}

static uint64_t kernel_handle_user_exception(uint32_t vector, const struct exception_frame *frame) {
    struct process_session *session;
    struct user_page_mapping *mappings;
    const struct process *proc;
    uint64_t fault_addr = 0;
    uint64_t fault_error = 0;
    uint64_t fault_ip = 0;
    int32_t exit_code = -11;

    if (!hal_exception_frame_is_user(frame)) {
        return IRQ_DISPATCH_CONTINUE;
    }
    if (vector != 0u && vector != 6u && vector != 8u && vector != 13u && vector != 14u) {
        return IRQ_DISPATCH_CONTINUE;
    }

    session = process_current_session();
    mappings = process_current_mappings();
    if (current_cpu_user_state()->nested_kernel_stack_depth != 0) {
        uint32_t index = current_cpu_user_state()->nested_kernel_stack_depth - 1u;

        if (current_cpu_user_state()->active_sessions[index] != 0) {
            session = current_cpu_user_state()->active_sessions[index];
            mappings = current_cpu_user_state()->active_mappings[index];
            process_bind_session(session, mappings);
            if (session->address_space.user_root != 0) {
                (void)vmm_switch_root_or_fail(session->address_space.user_root);
            }
        }
    }
    proc = process_current();
    if (session == 0 || proc == 0 || proc->image_kind == PROCESS_IMAGE_NONE) {
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == 0u) {
        exit_code = -8;
    } else if (vector == 6u) {
        exit_code = -4;
    } else if (vector == 14u) {
        exit_code = -14;
    }

    fault_error = hal_exception_frame_error_code(frame);
    fault_ip = hal_exception_frame_ip(frame);
    if (vector == 14u) {
        fault_addr = hal_page_fault_address();
        if (session->address_space.user_root != 0 &&
            vmm_resolve_cow_fault(session->address_space.user_root,
                                  fault_addr,
                                  fault_error)) {
            return IRQ_DISPATCH_RESUME_FAULT;
        }
        if (process_handle_demand_page_fault(session,
                                             mappings,
                                             fault_addr,
                                             fault_error)) {
            return IRQ_DISPATCH_RESUME_FAULT;
        }
        kprint("proc: fatal user exception pid=%u vec=%u ip=%lx err=%lx fault=%lx name=%s\n",
               proc->pid,
               vector,
               fault_ip,
               fault_error,
               fault_addr,
               proc->name != 0 ? proc->name : "(unnamed)");
    } else {
        kprint("proc: fatal user exception pid=%u vec=%u ip=%lx err=%lx name=%s\n",
               proc->pid,
               vector,
               fault_ip,
               fault_error,
               proc->name != 0 ? proc->name : "(unnamed)");
    }
    process_exit_current(session, exit_code);
    return IRQ_DISPATCH_RESUME_KERNEL;
}

static void kernel_boot_trace(const char *text) {
    uint16_t row = g_kernel_boot_trace_row;
    uint16_t col = 0;
    uint16_t width = console_width();
    uint16_t rows = console_rows();

    if (text == 0) {
        return;
    }
    if (kprint_is_ready()) {
        kprint("%s\n", text);
        g_kernel_boot_trace_row = tty_cursor_row(&shell_tty);
        return;
    }
    if (row >= rows) {
        return;
    }
    tty_clear_row(&shell_tty, row, 0x0f);
    while (*text != '\0' && col < width) {
        tty_put_at(&shell_tty, row, col, *text++, 0x0f);
        col++;
    }
    if (g_kernel_boot_trace_row + 1u < rows) {
        g_kernel_boot_trace_row++;
    }
}

static void kernel_boot_settle_ticks(uint32_t ticks) {
    uint32_t start;
    uint32_t stagnant = 0u;

    if (ticks == 0u) {
        return;
    }
    start = hal_timer_current_ticks();
    while ((uint32_t)(hal_timer_current_ticks() - start) < ticks) {
        uint32_t before = hal_timer_current_ticks();

        hal_display_service_pending();
        hal_cpu_wait_for_interrupt();
        if (hal_timer_current_ticks() != before) {
            stagnant = 0u;
        } else {
            stagnant++;
            if (stagnant > 200000u) {
                break;
            }
            hal_cpu_relax();
        }
    }
}

static int kernel_feed_keyboard_event(const struct keyboard_event *event, const struct syscall_frame *frame) {
    struct tty *target_tty;
    int ctrl_c;
    int ctrl_z;
    int sigint;
    int sigtstp;
    uint32_t focus_pid;

    if (event == 0 || event->keycode == KEYBOARD_KEY_NONE) {
        return 0;
    }
    device_poll_push_keyboard_event(event, &timer_ticks);
    if (event->pressed && event->alt) {
        uint32_t tty_index = TTY_VIRTUAL_COUNT;

        if (event->keycode == KEYBOARD_KEY_F1) {
            tty_index = 0u;
        } else if (event->keycode == KEYBOARD_KEY_F2) {
            tty_index = 1u;
        } else if (event->keycode == KEYBOARD_KEY_F3) {
            tty_index = 2u;
        } else if (event->shift && event->keycode == KEYBOARD_KEY_1) {
            tty_index = 0u;
        } else if (event->shift && event->keycode == KEYBOARD_KEY_2) {
            tty_index = 1u;
        } else if (event->shift && event->keycode == KEYBOARD_KEY_3) {
            tty_index = 2u;
        }
        if (tty_index < TTY_VIRTUAL_COUNT && tty_switch_active(tty_index)) {
            input_focus_clear();
            target_tty = tty_active();
            if (target_tty != 0) {
                device_poll_set_mouse_selection_console(&target_tty->console);
                kprint_set_tty(target_tty);
            }
            return 0;
        }
    }
    target_tty = tty_active();
    if (target_tty == 0) {
        target_tty = &shell_tty;
    }
    ctrl_c = event->pressed && event->ctrl && event->keycode == KEYBOARD_KEY_C;
    ctrl_z = event->pressed && event->ctrl && event->keycode == KEYBOARD_KEY_Z;
    focus_pid = input_focus_owner_pid();
    sigint = ctrl_c &&
             ((focus_pid != 0u && job_deliver_sigint_to_pid(focus_pid) > 0) ||
              job_tty_deliver_sigint(target_tty) > 0);
    sigtstp = ctrl_z && job_tty_deliver_sigtstp(target_tty, frame) > 0;
    if (sigint) {
        input_focus_clear();
        tty_write_str(target_tty, "^C\n", 0x0f);
    } else if (sigtstp) {
        input_focus_clear();
        tty_write_str(target_tty, "^Z\n", 0x0f);
    } else if (focus_pid != 0u) {
        return 0;
    } else if (ctrl_c) {
        tty_feed_key_event(target_tty, event);
    } else if (!ctrl_z) {
        tty_feed_key_event(target_tty, event);
    }
    return sigint || sigtstp;
}

uint64_t irq_dispatch(uint32_t vector, const struct syscall_frame *frame) {
    uint8_t irq_line;

    if (vector == IRQ_VECTOR_TIMER) {
        struct keyboard_event uart_event;
        struct keyboard_event usb_event;
        int usb_due;
        int uart_signal = 0;
        int usb_signal = 0;

        kernel_irq_state_record(0u, hal_syscall_frame_is_user(frame));
        hal_timer_notify_tick();
        timer_ticks++;

        usb_due = device_poll_note_timer_tick();

        if (usb_due) {
            (void)device_poll_service_usb_mouse_events(timer_ticks);
            while (device_poll_poll_usb_keyboard_event(&usb_event)) {
                if (kernel_feed_keyboard_event(&usb_event, frame)) {
                    usb_signal = 1;
                }
            }
        }

        while (device_poll_poll_uart_keyboard_event(&uart_event)) {
            if (kernel_feed_keyboard_event(&uart_event, frame)) {
                uart_signal = 1;
            }
        }

        sched_on_timer_tick(timer_ticks);
        hal_irq_ack(0);

        if (hal_syscall_frame_is_user(frame) && (usb_signal || uart_signal)) {
            sched_preempt_current(process_current_session(), frame);
            return IRQ_DISPATCH_RESUME_KERNEL;
        }

        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == IRQ_VECTOR_KEYBOARD) {
        struct keyboard_event event;
        int signal = 0;

        kernel_irq_state_record(1u, hal_syscall_frame_is_user(frame));
        event = device_poll_read_ps2_keyboard_event();
        signal = kernel_feed_keyboard_event(&event, frame);
        hal_irq_ack(1);
        if (signal && hal_syscall_frame_is_user(frame)) {
            return IRQ_DISPATCH_RESUME_KERNEL;
        }
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == IRQ_VECTOR_MOUSE) {
        kernel_irq_state_record(12u, hal_syscall_frame_is_user(frame));
        device_poll_handle_mouse_irq(&timer_ticks);
        hal_irq_ack(12);
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == IRQ_VECTOR_SERIAL) {
        struct keyboard_event event;
        int signal = 0;

        kernel_irq_state_record(4u, hal_syscall_frame_is_user(frame));
        device_poll_handle_uart_irq();
        while (device_poll_poll_uart_keyboard_event(&event)) {
            if (kernel_feed_keyboard_event(&event, frame)) {
                signal = 1;
            }
        }
        hal_irq_ack(4);
        if (signal && hal_syscall_frame_is_user(frame)) {
            return IRQ_DISPATCH_RESUME_KERNEL;
        }
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector < IRQ_VECTOR_BASE || vector > IRQ_VECTOR_LAST) {
        return IRQ_DISPATCH_CONTINUE;
    }

    irq_line = (uint8_t)(vector - IRQ_VECTOR_BASE);
    kernel_irq_state_record(irq_line, hal_syscall_frame_is_user(frame));
    device_poll_handle_network_irq(irq_line);
    hal_irq_ack(irq_line);
    return IRQ_DISPATCH_CONTINUE;
}

uint64_t kernel_prepare_user_frame_return(const struct syscall_frame *frame) {
    return sched_prepare_user_frame_return(frame);
}

uint64_t kernel_panic_dispatch_exception(uint32_t vector, const struct exception_frame *frame) {
    uint64_t rc = kernel_handle_user_exception(vector, frame);

    if (rc == IRQ_DISPATCH_RESUME_KERNEL || rc == IRQ_DISPATCH_RESUME_FAULT) {
        return rc;
    }
    kernel_panic_handle_exception(&shell_tty, g_current_user_raw_entry, vector, frame);
    return IRQ_DISPATCH_CONTINUE;
}

void kernel_main64(const struct janus_boot_info *boot_info) {
    const struct janus_memmap_entry *memmap;
    struct vfs *vfs;
    uint64_t kernel_phys_base;
    int init_started;

    hal_cpu_cli();
    hal_cpu_enable_sse();
    string_runtime_init();
    if (boot_info != 0 && boot_info->hdr.magic == JANUS_MAGIC) {
        hal_display_init(&boot_info->console);
        kernel_gfx_init(&boot_info->console);
    }
    tty_virtual_init_all(0, (uint16_t)(console_rows() - 1u), 0x0f);
    device_poll_set_mouse_selection_console(&shell_tty.console);
    kprint_init();
    kprint_set_tty(&shell_tty);  /* Route boot logs through kprint as early as possible. */
    g_kernel_boot_trace_row = 1;
    tty_set_cursor(&shell_tty, g_kernel_boot_trace_row, 0);
    kernel_boot_trace("kernel: entered");
    if (KERNEL_RUN_MEMORY_BENCHMARK) {
        string_memory_benchmark();
    }
    if (!kernel_boot_info_valid(boot_info)) {
        kernel_boot_trace("kernel: bad boot info");
        kernel_halt_forever();
    }
    kernel_boot_state_init(boot_info->cmdline != 0 ?
                               (const char *)(uintptr_t)boot_info->cmdline :
                           "",
                           "kernel64",
                           "0.1.1",
                           hal_arch_name());
    kernel_irq_state_reset();
    kernel_boot_log_arch_bootstrap(hal_arch_name());

    memmap = (const struct janus_memmap_entry *)(uintptr_t)boot_info->memmap;
    kernel_log_boot_info(boot_info);
    kernel_log_memmap(memmap, boot_info->memmap_count);
    kernel_phys_base = kernel_detect_phys_base(boot_info);
    kernel_boot_trace("kernel: paging init");
    hal_paging_init(kernel_phys_base);
    kernel_log_paging_info();
    kernel_boot_trace("kernel: pmm init");
    if (!hal_pmm_init_from_boot(boot_info, kernel_phys_base)) {
        kernel_boot_trace("kernel: pmm init failed");
        kernel_halt_forever();
    }
    if (!hal_paging_guard_kernel_page(hal_kernel_rsp0_guard_address()) ||
        !hal_paging_guard_kernel_page(hal_double_fault_guard_address())) {
        kernel_boot_trace("kernel: arch stack guard failed");
        kernel_halt_forever();
    }
    for (uint32_t i = 0u; i < USER_NESTED_KERNEL_STACK_LIMIT; i++) {
        if (!hal_paging_guard_kernel_page(process_kernel_stack_guard_address(i))) {
            kernel_boot_trace("kernel: process stack guard failed");
            kernel_halt_forever();
        }
    }
    hal_display_load_font(boot_info);
    kernel_reserve_boot_modules(boot_info);
    if (boot_info->console.type == JANUS_CONSOLE_FRAMEBUFFER) {
        uint64_t framebuffer_size =
            (uint64_t)boot_info->console.pitch * boot_info->console.height;
        int framebuffer_wc;

        kernel_boot_trace("kernel: framebuffer wc begin");
        framebuffer_wc = hal_paging_set_write_combining(boot_info->console.framebuffer_addr,
                                                        framebuffer_size);

        kernel_boot_log_framebuffer(boot_info->console.framebuffer_addr,
                                    framebuffer_size,
                                    1,
                                    (uint32_t)framebuffer_wc);
    }
    if (kernel_cmdline_has_token(boot_info, "fb.backbuffer=1")) {
        kernel_boot_trace("kernel: framebuffer backbuffer begin");
        if (hal_display_enable_backbuffer()) {
            kernel_boot_trace("kernel: framebuffer backbuffer enabled");
        } else {
            kernel_boot_trace("kernel: framebuffer backbuffer skip");
        }
    } else {
        kernel_boot_trace("kernel: framebuffer backbuffer skip");
    }
    kernel_log_pmm_info();

    kernel_boot_trace("kernel: block devices");
    kernel_init_storage_devices(boot_info);
    kernel_boot_trace("kernel: pci/ide/ata");

    kernel_boot_trace("kernel: tty/process/vfs");
    kprint_set_boot_time(&timer_ticks);  /* Enable timestamp logging (SOSP feature) */
    kernel_register_fs_service_runtime_ops();
    kernel_register_file_device_runtime_ops();
    vfs = kernel_init_core_services(&shell_tty, &timer_ticks, boot_info);
    if (vfs == 0) {
        kernel_boot_trace("kernel: core services failed");
        kernel_halt_forever();
    }
    if (kernel_apply_root_cmdline(vfs, boot_info) > 0) {
        kernel_boot_trace("kernel: root cmdline applied");
    }
    uint32_t discovered = 0;

    kernel_boot_trace("kernel: discover drivers");

    discovered += driver_discover_root(vfs, "/drivers");
    discovered += driver_discover_root(vfs, "/DRIVERS");
    discovered += driver_discover_root(vfs, "/ram/DRIVERS");
    (void)discovered;

    (void)driver_load_all(vfs);
    (void)driver_init_all();
    kernel_boot_trace("pseudo fs: devfs procfs eventfs ready");
    syscall_init(&shell_tty, &timer_ticks, vfs, boot_info, memmap, boot_info->memmap_count);
    timer_ticks = 0;

    kernel_boot_trace("kernel: interrupts");
    kernel_init_interrupts();
    if (boot_info->cmdline != 0u &&
        ioapic_configure_from_cmdline((const char *)(uintptr_t)boot_info->cmdline) > 0) {
        uint32_t ioapic_mask = ioapic_enabled_irq_mask();

        for (uint8_t irq = 0u; irq < 16u; irq++) {
            if ((ioapic_mask & (1u << irq)) != 0u) {
                hal_irq_set_mask(irq, 0);
            }
        }
    }
    kernel_boot_trace("kernel: services online");
    kernel_boot_trace("kernel: storage settle");
    kernel_boot_settle_ticks(100u);

    kernel_boot_trace("kernel: system/init");
    init_started = kernel_try_run_init(vfs, &shell_tty, &g_kernel_boot_trace_row, boot_info);
    if (init_started) {
        kernel_panic_after_init_return();
    }
    kernel_boot_trace("kernel: init missing");

    for (;;) {
        hal_display_service_pending();
        hal_cpu_halt();
    }
}
