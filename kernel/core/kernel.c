#include <stdint.h>
#include "bootx/bootx.h"
#include "hal/hal.h"
#include "kernel/internal/core/device_poll_internal.h"
#include "kernel/internal/core/graphics_service_internal.h"
#include "kernel/internal/core/kernel_boot_internal.h"
#include "kernel/internal/core/kernel_init_internal.h"
#include "kernel/internal/core/kernel_panic_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/internal/core/tty_internal.h"
#include "kernel/public/core/console.h"
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
    IRQ_DISPATCH_RESUME_KERNEL = 0xfffffffffffffff0ull,
    IRQ_VECTOR_BASE = 32u,
    IRQ_VECTOR_TIMER = 32u,
    IRQ_VECTOR_KEYBOARD = 33u,
    IRQ_VECTOR_SERIAL = 36u,
    IRQ_VECTOR_MOUSE = 44u,
    IRQ_VECTOR_LAST = 47u
};

static void kernel_halt_forever(void) {
    for (;;) {
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

static uint64_t kernel_handle_user_exception(uint32_t vector, const struct exception_frame *frame) {
    struct process_session *session;
    const struct process *proc;
    uint64_t fault_addr = 0;
    int32_t exit_code = -11;

    if (frame == 0 || (frame->cs & 0x3u) != 0x3u) {
        return IRQ_DISPATCH_CONTINUE;
    }
    if (vector != 0u && vector != 6u && vector != 8u && vector != 13u && vector != 14u) {
        return IRQ_DISPATCH_CONTINUE;
    }

    session = process_current_session();
    proc = process_current();
    if (session == 0 || proc == 0 || proc->image_kind == PROCESS_IMAGE_NONE) {
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == 0u) {
        exit_code = -8;
    } else if (vector == 6u) {
        exit_code = -4;
    }

    if (vector == 14u) {
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_addr));
        kprint("proc: fatal user exception pid=%u vec=%u rip=%lx err=%lx cr2=%lx name=%s\n",
               proc->pid,
               vector,
               frame->rip,
               frame->error_code,
               fault_addr,
               proc->name != 0 ? proc->name : "(unnamed)");
    } else {
        kprint("proc: fatal user exception pid=%u vec=%u rip=%lx err=%lx name=%s\n",
               proc->pid,
               vector,
               frame->rip,
               frame->error_code,
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

static int kernel_feed_keyboard_event(const struct keyboard_event *event, const struct syscall_frame *frame) {
    struct tty *target_tty;
    int ctrl_c;
    int ctrl_z;
    int sigint;
    int sigtstp;

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
    sigint = ctrl_c && job_tty_sigint(target_tty);
    sigtstp = ctrl_z && job_tty_sigtstp(target_tty, frame);
    if (sigint) {
        tty_write_str(target_tty, "^C\n", 0x0f);
    } else if (sigtstp) {
        tty_write_str(target_tty, "^Z\n", 0x0f);
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
        struct keyboard_event usb_event;
        struct keyboard_event uart_event;
        int usb_signal = 0;

        hal_timer_notify_tick();
        timer_ticks++;
        device_poll_poll_usb_mouse_events(&timer_ticks);
        while (device_poll_poll_usb_keyboard_event(&usb_event)) {
            if (kernel_feed_keyboard_event(&usb_event, frame)) {
                usb_signal = 1;
            }
        }
        while (device_poll_poll_uart_keyboard_event(&uart_event)) {
            if (kernel_feed_keyboard_event(&uart_event, frame)) {
                usb_signal = 1;
            }
        }
        sched_on_timer_tick(timer_ticks);
        hal_irq_ack(0);
        if (usb_signal && frame != 0 && (frame->cs & 0x3u) == 0x3u) {
            return IRQ_DISPATCH_RESUME_KERNEL;
        }
        if (frame != 0 && (frame->cs & 0x3u) == 0x3u) {
            sched_preempt_current(process_current_session(), frame);
            return IRQ_DISPATCH_RESUME_KERNEL;
        }
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == IRQ_VECTOR_KEYBOARD) {
        struct keyboard_event event;
        int signal = 0;

        event = device_poll_read_ps2_keyboard_event();
        signal = kernel_feed_keyboard_event(&event, frame);
        hal_irq_ack(1);
        if (signal && frame != 0 && (frame->cs & 0x3u) == 0x3u) {
            return IRQ_DISPATCH_RESUME_KERNEL;
        }
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == IRQ_VECTOR_MOUSE) {
        device_poll_handle_mouse_irq(&timer_ticks);
        hal_irq_ack(12);
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector == IRQ_VECTOR_SERIAL) {
        struct keyboard_event event;
        int signal = 0;

        device_poll_handle_uart_irq();
        while (device_poll_poll_uart_keyboard_event(&event)) {
            if (kernel_feed_keyboard_event(&event, frame)) {
                signal = 1;
            }
        }
        hal_irq_ack(4);
        if (signal && frame != 0 && (frame->cs & 0x3u) == 0x3u) {
            return IRQ_DISPATCH_RESUME_KERNEL;
        }
        return IRQ_DISPATCH_CONTINUE;
    }

    if (vector < IRQ_VECTOR_BASE || vector > IRQ_VECTOR_LAST) {
        return IRQ_DISPATCH_CONTINUE;
    }

    irq_line = (uint8_t)(vector - IRQ_VECTOR_BASE);
    device_poll_handle_network_irq(irq_line);
    hal_irq_ack(irq_line);
    return IRQ_DISPATCH_CONTINUE;
}

uint64_t kernel_panic_dispatch_exception(uint32_t vector, const struct exception_frame *frame) {
    uint64_t rc = kernel_handle_user_exception(vector, frame);

    if (rc == IRQ_DISPATCH_RESUME_KERNEL) {
        return rc;
    }
    kernel_panic_handle_exception(&shell_tty, g_current_user_raw_entry, vector, frame);
    return IRQ_DISPATCH_CONTINUE;
}

void kernel_main64(const struct bootx_boot_info *boot_info) {
    const struct bootx_memmap_entry *memmap;
    struct vfs *vfs;
    uint64_t kernel_phys_base;
    int init_started;

    hal_cpu_cli();
    if (boot_info != 0 && boot_info->hdr.magic == BOOTX_MAGIC) {
        hal_display_load_font(boot_info);
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
    if (!kernel_boot_info_valid(boot_info)) {
        kernel_boot_trace("kernel: bad boot info");
        kernel_halt_forever();
    }

    memmap = (const struct bootx_memmap_entry *)(uintptr_t)boot_info->memmap;
    kernel_log_boot_info(boot_info);
    kernel_log_memmap(memmap, boot_info->memmap_count);
    kernel_phys_base = kernel_detect_phys_base(boot_info);
    kernel_boot_trace("kernel: paging init");
    hal_paging_init(kernel_phys_base);
    kernel_log_paging_info();
    kernel_boot_trace("kernel: pmm init");
    pmm_init(memmap, boot_info->memmap_count, kernel_phys_base, boot_info->kernel_phys_size);
    kernel_reserve_boot_modules(boot_info);
    (void)hal_display_enable_backbuffer();
    kernel_log_pmm_info();

    kernel_boot_trace("kernel: block devices");
    kernel_init_storage_devices(boot_info);

    kernel_boot_trace("kernel: tty/process/vfs");
    kprint_set_boot_time(&timer_ticks);  /* Enable timestamp logging (SOSP feature) */
    vfs = kernel_init_core_services(&shell_tty, &timer_ticks);
    if (vfs == 0) {
        kernel_boot_trace("kernel: core services failed");
        kernel_halt_forever();
    }
    syscall_init(&shell_tty, &timer_ticks, vfs, boot_info, memmap, boot_info->memmap_count);
    timer_ticks = 0;

    kernel_boot_trace("kernel: interrupts");
    kernel_init_interrupts();

    kernel_boot_trace("kernel: start init");
    init_started = kernel_try_run_init(vfs, &shell_tty, &g_kernel_boot_trace_row, boot_info);
    if (init_started) {
        kernel_panic_init_exit();
    }
    kernel_boot_trace("kernel: init missing");

    for (;;) {
        hal_cpu_halt();
    }
}
