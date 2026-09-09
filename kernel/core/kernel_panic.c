#include <stdint.h>
#include "drivers/serial/uart.h"
#include "hal/hal.h"
#include "kernel/internal/core/kernel_panic_internal.h"
#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/public/proc/scheduler.h"
#include "kernel/public/core/console.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/public/mem/vmm.h"
#include "kernel/public/sys/syscall.h"

enum kernel_panic_detail {
    KERNEL_PANIC_DETAIL_COMPACT = 0,
    KERNEL_PANIC_DETAIL_NORMAL = 1,
    KERNEL_PANIC_DETAIL_FULL = 2
};

static const char *const exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating Point",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point",
    "Virtualization",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security",
    "Reserved"
};

static enum kernel_panic_detail kernel_panic_detail_for_console(void) {
    uint16_t width = console_width();
    uint16_t rows = console_rows();

    if (width >= 120u && rows >= 40u) {
        return KERNEL_PANIC_DETAIL_FULL;
    }
    if (width >= 100u && rows >= 32u) {
        return KERNEL_PANIC_DETAIL_NORMAL;
    }
    return KERNEL_PANIC_DETAIL_COMPACT;
}

static const char *kernel_panic_detail_name(enum kernel_panic_detail detail) {
    switch (detail) {
        case KERNEL_PANIC_DETAIL_FULL:
            return "full";
        case KERNEL_PANIC_DETAIL_NORMAL:
            return "normal";
        default:
            return "compact";
    }
}

static void panic_clear(struct tty *shell_tty) {
    if (shell_tty != 0) {
        tty_clear(shell_tty);
    }
}

static void panic_putc(struct tty *shell_tty, char ch) {
    if (shell_tty != 0) {
        tty_putc(shell_tty, ch, 0x0f);
    }
    uart_write_char(ch == '\n' ? '\r' : ch);
    if (ch == '\n') {
        uart_write_char('\n');
    }
}

static void panic_write_str(struct tty *shell_tty, const char *text) {
    if (text == 0) {
        return;
    }
    if (shell_tty != 0) {
        tty_write_str(shell_tty, text, 0x0f);
    }
    uart_write(text);
}

static void panic_write_dec(struct tty *shell_tty, uint32_t value) {
    char buffer[16];
    uint32_t i = 0;

    if (value == 0) {
        buffer[i++] = '0';
    } else {
        while (value != 0 && i < sizeof(buffer)) {
            buffer[i++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (i > 0) {
        panic_putc(shell_tty, buffer[--i]);
    }
}

static void panic_write_hex64(struct tty *shell_tty, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    int shift;

    if (shell_tty != 0) {
        tty_write_hex64(shell_tty, value, 0x0f);
    }
    for (shift = 60; shift >= 0; shift -= 4) {
        uart_write_char(digits[(value >> shift) & 0xf]);
    }
}

static void panic_write_hex8(struct tty *shell_tty, uint8_t value) {
    static const char digits[] = "0123456789abcdef";

    if (shell_tty != 0) {
        tty_putc(shell_tty, digits[(value >> 4) & 0xf], 0x0f);
        tty_putc(shell_tty, digits[value & 0xf], 0x0f);
    }
    uart_write_char(digits[(value >> 4) & 0xf]);
    uart_write_char(digits[value & 0xf]);
}

static void panic_write_label_value(struct tty *shell_tty, const char *label, uint64_t value) {
    panic_write_str(shell_tty, label);
    panic_write_hex64(shell_tty, value);
    panic_putc(shell_tty, '\n');
}

static int exception_has_cpu_error_code(uint32_t vector) {
    switch (vector) {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
        case 21:
        case 29:
        case 30:
            return 1;
        default:
            return 0;
    }
}

static void panic_write_reg4(struct tty *shell_tty,
                             const char *a_name, uint64_t a,
                             const char *b_name, uint64_t b,
                             const char *c_name, uint64_t c,
                             const char *d_name, uint64_t d) {
    panic_write_str(shell_tty, a_name);
    panic_write_hex64(shell_tty, a);
    panic_write_str(shell_tty, "  ");
    panic_write_str(shell_tty, b_name);
    panic_write_hex64(shell_tty, b);
    panic_write_str(shell_tty, "  ");
    panic_write_str(shell_tty, c_name);
    panic_write_hex64(shell_tty, c);
    panic_write_str(shell_tty, "  ");
    panic_write_str(shell_tty, d_name);
    panic_write_hex64(shell_tty, d);
    panic_putc(shell_tty, '\n');
}

static void panic_write_pair(struct tty *shell_tty,
                             const char *a_name, uint64_t a, const char *b_name, uint64_t b) {
    panic_write_str(shell_tty, a_name);
    panic_write_hex64(shell_tty, a);
    panic_write_str(shell_tty, b_name);
    panic_write_hex64(shell_tty, b);
    panic_putc(shell_tty, '\n');
}

struct kernel_panic_trace_ctx {
    struct tty *shell_tty;
};

static void kernel_panic_trace_text(void *ctx, const char *text) {
    struct kernel_panic_trace_ctx *trace_ctx = (struct kernel_panic_trace_ctx *)ctx;

    if (trace_ctx == 0) {
        return;
    }
    panic_write_str(trace_ctx->shell_tty, text);
    panic_putc(trace_ctx->shell_tty, '\n');
}

static void kernel_panic_trace_hex64(void *ctx, const char *label, uint64_t value) {
    struct kernel_panic_trace_ctx *trace_ctx = (struct kernel_panic_trace_ctx *)ctx;

    if (trace_ctx == 0) {
        return;
    }
    panic_write_label_value(trace_ctx->shell_tty, label, value);
}

static void kernel_panic_trace_ops_for_tty(struct tty *shell_tty,
                                           struct kernel_panic_trace_ctx *ctx,
                                           struct hal_boot_trace_ops *ops) {
    if (ctx == 0 || ops == 0) {
        return;
    }
    ctx->shell_tty = shell_tty;
    ops->text = kernel_panic_trace_text;
    ops->hex64 = kernel_panic_trace_hex64;
}

static const char *kernel_process_state_name(enum process_state state) {
    switch (state) {
        case PROCESS_STATE_READY:
            return "ready";
        case PROCESS_STATE_RUNNING:
            return "running";
        case PROCESS_STATE_SLEEPING:
            return "sleeping";
        case PROCESS_STATE_STOPPED:
            return "stopped";
        case PROCESS_STATE_EXITED:
            return "exited";
        case PROCESS_STATE_WAITING:
            return "waiting";
        default:
            return "free";
    }
}

static const char *kernel_process_image_kind_name(enum process_image_kind kind) {
    switch (kind) {
        case PROCESS_IMAGE_ELF:
            return "elf";
        default:
            return "none";
    }
}

static const char *kernel_syscall_name(uint64_t number) {
    switch (number) {
        case SYS_EXIT: return "exit";
        case SYS_OPEN: return "open";
        case SYS_READ: return "read";
        case SYS_WRITE: return "write";
        case SYS_CLOSE: return "close";
        case SYS_DUP2: return "dup2";
        case SYS_PIPE: return "pipe";
        case SYS_TICKS: return "ticks";
        case SYS_SEEK: return "seek";
        case SYS_EXEC: return "exec";
        case SYS_EXEC_REPLACE: return "exec_replace";
        case SYS_SPAWN: return "spawn";
        case SYS_WAIT: return "wait";
        case SYS_KILL: return "kill";
        case SYS_GETPID: return "getpid";
        case SYS_YIELD: return "yield";
        case SYS_SLEEP: return "sleep";
        case SYS_PROC_QUERY: return "proc_query";
        case SYS_FG: return "fg";
        case SYS_BG: return "bg";
        case SYS_TTY_CLAIM: return "tty_claim";
        case SYS_SETCAP: return "setcap";
        case SYS_FORK: return "fork";
        case SYS_MMAP: return "mmap";
        case SYS_MUNMAP: return "munmap";
        case SYS_MPROTECT: return "mprotect";
        case SYS_PAGE_ALLOC: return "page_alloc";
        case SYS_PAGE_FREE: return "page_free";
        case SYS_GFX: return "gfx";
        case SYS_GUI_EVENT: return "gui_event";
        default: return "unknown";
    }
}

static int kernel_addr_in_user_range(uint64_t addr) {
    return (addr >= USER_ELF_BASE && addr < USER_ELF_LIMIT) ||
           (addr >= USER_MMAP_BASE && addr < USER_MMAP_END) ||
           (addr >= USER_ALLOC_BASE && addr < USER_ALLOC_END) ||
           (addr >= USER_ELF_STACK_BOTTOM && addr < USER_ELF_STACK_TOP);
}

static void kernel_panic_write_header(struct tty *shell_tty,
                                      uint32_t vector,
                                      const struct hal_exception_snapshot *context,
                                      enum kernel_panic_detail detail) {
    panic_clear(shell_tty);
    panic_write_str(shell_tty, "[");
    panic_write_str(shell_tty, "ERROR");
    panic_write_str(shell_tty, "]\n");
    panic_write_str(shell_tty, "========[ FATAL CPU EXCEPTION / KERNEL PANIC ]==========\n");
    panic_write_str(shell_tty, "Display Detail   : ");
    panic_write_str(shell_tty, kernel_panic_detail_name(detail));
    panic_write_str(shell_tty, " (");
    panic_write_dec(shell_tty, console_width());
    panic_putc(shell_tty, 'x');
    panic_write_dec(shell_tty, console_rows());
    panic_write_str(shell_tty, " text)\n");

    panic_write_str(shell_tty, "Interrupt Number : ");
    panic_write_dec(shell_tty, vector);
    panic_putc(shell_tty, '\n');

    if (vector < 32) {
        panic_write_str(shell_tty, "Description      : ");
        panic_write_str(shell_tty, exception_messages[vector]);
        panic_putc(shell_tty, '\n');
    } else {
        panic_write_str(shell_tty, "Description      : Unknown IRQ or user-defined interrupt\n");
    }

    if (vector == 14) {
        panic_write_str(shell_tty, "Fault Address    : ");
        panic_write_hex64(shell_tty, context->fault_address);
        panic_putc(shell_tty, '\n');
    } else {
        panic_write_str(shell_tty, "Fault Address    : N/A (CR2 is only valid for #PF)\n");
        panic_write_str(shell_tty, "CR2              : ");
        panic_write_hex64(shell_tty, context->fault_address);
        panic_write_str(shell_tty, " (stale/last page-fault address)\n");
    }
}

static void kernel_panic_write_cpu_state(struct tty *shell_tty,
                                         const struct hal_exception_snapshot *context) {
    panic_write_str(shell_tty, "--- CPU STATE ---\n");
    panic_write_reg4(shell_tty,
                     "RAX=", context->general[HAL_EXCEPTION_REGISTER_RAX],
                     "RBX=", context->general[HAL_EXCEPTION_REGISTER_RBX],
                     "RCX=", context->general[HAL_EXCEPTION_REGISTER_RCX],
                     "RDX=", context->general[HAL_EXCEPTION_REGISTER_RDX]);
    panic_write_reg4(shell_tty,
                     "RSI=", context->general[HAL_EXCEPTION_REGISTER_RSI],
                     "RDI=", context->general[HAL_EXCEPTION_REGISTER_RDI],
                     "RBP=", context->general[HAL_EXCEPTION_REGISTER_RBP],
                     "SP =", context->stack_pointer);
    panic_write_reg4(shell_tty,
                     "R8 =", context->general[HAL_EXCEPTION_REGISTER_R8],
                     "R9 =", context->general[HAL_EXCEPTION_REGISTER_R9],
                     "R10=", context->general[HAL_EXCEPTION_REGISTER_R10],
                     "R11=", context->general[HAL_EXCEPTION_REGISTER_R11]);
    panic_write_reg4(shell_tty,
                     "R12=", context->general[HAL_EXCEPTION_REGISTER_R12],
                     "R13=", context->general[HAL_EXCEPTION_REGISTER_R13],
                     "R14=", context->general[HAL_EXCEPTION_REGISTER_R14],
                     "R15=", context->general[HAL_EXCEPTION_REGISTER_R15]);
    panic_write_reg4(shell_tty,
                     "EXCEPTION IP =", context->instruction_pointer,
                     "RFL=", context->flags,
                     "EXCEPTION CS =", context->code_selector,
                     "SS =", context->stack_selector);
    panic_write_label_value(shell_tty, "Error Code       : ", context->error_code);
}

static void kernel_panic_write_compact_cpu_state(struct tty *shell_tty,
                                                 const struct hal_exception_snapshot *context) {
    panic_write_str(shell_tty, "--- CPU SUMMARY ---\n");
    panic_write_pair(shell_tty, "IP =", context->instruction_pointer, " SP =", context->stack_pointer);
    panic_write_pair(shell_tty,
                     "RAX=", context->general[HAL_EXCEPTION_REGISTER_RAX],
                     " ERR=", context->error_code);
    panic_write_pair(shell_tty, "CS =", context->code_selector, " SS =", context->stack_selector);
}

static void kernel_panic_write_error_code_type(struct tty *shell_tty, uint32_t vector) {
    panic_write_str(shell_tty, "Error Code Type  : ");
    if (exception_has_cpu_error_code(vector)) {
        panic_write_str(shell_tty, "CPU-provided\n");
    } else {
        panic_write_str(shell_tty, "synthetic/fake\n");
    }
}

static void kernel_panic_write_process(struct tty *shell_tty, const struct process *proc) {
    if (proc == 0) {
        return;
    }

    panic_write_str(shell_tty, "PROCESS          : ");
    panic_write_str(shell_tty, proc->name != 0 ? proc->name : "(unnamed)");
    panic_write_str(shell_tty, " pid=");
    panic_write_dec(shell_tty, proc->pid);
    panic_write_str(shell_tty, " slot=");
    panic_write_dec(shell_tty, proc->slot);
    panic_write_str(shell_tty, " state=");
    panic_write_str(shell_tty, kernel_process_state_name(proc->state));
    panic_write_str(shell_tty, " exit=");
    panic_write_hex64(shell_tty, (uint32_t)proc->exit_code);
    panic_write_str(shell_tty, " kind=");
    panic_write_str(shell_tty, kernel_process_image_kind_name(proc->image_kind));
    panic_write_str(shell_tty, " stack=");
    panic_write_hex64(shell_tty, proc->stack_top);
    panic_putc(shell_tty, '\n');

    if (proc->state == PROCESS_STATE_EXITED || proc->state == PROCESS_STATE_FREE ||
        proc->image_kind == PROCESS_IMAGE_NONE) {
        panic_write_str(shell_tty, "CURRENT TASK BUG : current task is not runnable during exception/user-return path\n");
    }
}

static void kernel_panic_write_code_bytes(struct tty *shell_tty,
                                          const struct hal_exception_snapshot *context) {
    uint64_t phys = 0;
    uint64_t flags = 0;
    uint64_t start;
    uint64_t page_remaining;
    uint32_t count;

    if (context == 0) {
        return;
    }

    panic_write_str(shell_tty, "--- CODE BYTES AT EXCEPTION IP ---\n");
    panic_write_label_value(shell_tty, "IP               : ", context->instruction_pointer);

    if (context->instruction_pointer == 0 ||
        !hal_paging_get_mapping_info_in_root(context->paging_root,
                                             context->instruction_pointer,
                                             &phys,
                                             &flags)) {
        panic_write_str(shell_tty, "READABLE         : no\n");
        panic_write_str(shell_tty, "BYTES            : unavailable\n");
        return;
    }

    start = context->instruction_pointer;
    page_remaining = 0x1000ull - (start & 0xfffull);
    count = page_remaining < 16u ? (uint32_t)page_remaining : 16u;

    panic_write_str(shell_tty, "READABLE         : yes\n");
    panic_write_str(shell_tty, "CODE BYTES       : ");
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *byte = (const uint8_t *)hal_phys_direct_map(phys + ((start + i) & 0xfffull));

        if (i != 0) {
            panic_putc(shell_tty, ' ');
        }
        if (i == 0) {
            panic_putc(shell_tty, '<');
        }
        panic_write_hex8(shell_tty, *byte);
        if (i == 0) {
            panic_putc(shell_tty, '>');
        }
    }
    panic_putc(shell_tty, '\n');
}

static void kernel_panic_write_raw_trap_frame(struct tty *shell_tty,
                                              const struct exception_frame *frame,
                                              const struct hal_exception_snapshot *context,
                                              uint32_t vector,
                                              const struct process *proc) {
    uint32_t i;

    if (frame == 0 || context == 0) {
        return;
    }

    panic_write_str(shell_tty, "--- RAW TRAP FRAME ---\n");
    panic_write_label_value(shell_tty, "TRAP FRAME       : ", (uint64_t)(uintptr_t)frame);
    if (proc != 0) {
        panic_write_str(shell_tty, "TRAP FRAME OWNER : pid=");
        panic_write_dec(shell_tty, proc->pid);
        panic_putc(shell_tty, ' ');
        panic_write_str(shell_tty, proc->name != 0 ? proc->name : "(unnamed)");
        panic_putc(shell_tty, '\n');
    }
    for (i = 0; i < context->raw_word_count; i++) {
        panic_write_str(shell_tty, "frame+");
        panic_write_hex64(shell_tty, (uint64_t)i * 8u);
        panic_write_str(shell_tty, "       = ");
        panic_write_hex64(shell_tty, context->raw_words[i]);
        panic_putc(shell_tty, '\n');
    }
    panic_write_label_value(shell_tty, "vector           = ", vector);
}

static void kernel_panic_write_addr_map(struct tty *shell_tty, const char *label, uint64_t addr) {
    uint64_t phys = 0;
    uint64_t flags = 0;

    if (label == 0 || addr == 0) {
        return;
    }

    panic_write_str(shell_tty, label);
    if (vmm_query_info(addr, &phys, &flags)) {
        panic_write_hex64(shell_tty, phys);
        panic_write_str(shell_tty, " flags=");
        panic_write_hex64(shell_tty, flags);
    } else {
        panic_write_str(shell_tty, "<unmapped>");
    }
    panic_putc(shell_tty, '\n');
}

static void kernel_panic_write_arch_paging_details(struct tty *shell_tty,
                                                  uint64_t current_user_raw_entry,
                                                  const struct process *proc) {
    struct kernel_panic_trace_ctx trace_ctx;
    struct hal_boot_trace_ops trace_ops;
    uint64_t target_root = 0;

    kernel_panic_trace_ops_for_tty(shell_tty, &trace_ctx, &trace_ops);
    if (proc != 0 && proc->address_space != 0) {
        target_root = proc->address_space->user_root;
    }
    hal_paging_log_panic_entry(&trace_ops, &trace_ctx, current_user_raw_entry);
    hal_paging_log_panic_target_entry(&trace_ops,
                                      &trace_ctx,
                                      target_root,
                                      current_user_raw_entry);
    hal_paging_log_panic_switch_trace(&trace_ops,
                                      &trace_ctx,
                                      current_user_raw_entry);
}

static void kernel_panic_write_user_return_check(struct tty *shell_tty,
                                                 const struct hal_exception_snapshot *context,
                                                 const struct process *proc) {
    uint64_t saved_user_ip = 0;
    uint64_t saved_user_sp = 0;
    uint64_t target_root = context != 0 ? context->paging_root : 0;
    int task_runnable = 0;

    if (context == 0) {
        return;
    }

    if (proc != 0) {
        if (proc->has_saved_frame) {
            saved_user_ip = hal_syscall_frame_ip(&proc->saved_frame);
            saved_user_sp = hal_syscall_frame_sp(&proc->saved_frame);
        } else {
            saved_user_ip = proc->entry;
            saved_user_sp = proc->stack_top;
        }
        if (proc->address_space != 0 && proc->address_space->user_root != 0) {
            target_root = proc->address_space->user_root;
        }
        task_runnable = proc->image_kind != PROCESS_IMAGE_NONE &&
                        proc->state != PROCESS_STATE_FREE &&
                        proc->state != PROCESS_STATE_EXITED;
    }

    panic_write_str(shell_tty, "--- USER RETURN CHECK ---\n");
    panic_write_str(shell_tty, "RETURN MODE      : ");
    panic_write_str(shell_tty, context->user_mode ? "user\n" : "kernel\n");
    panic_write_label_value(shell_tty, "TARGET CS        : ", context->code_selector);
    panic_write_label_value(shell_tty, "TARGET SS        : ", context->stack_selector);
    panic_write_label_value(shell_tty, "TARGET IP        : ", context->instruction_pointer);
    panic_write_label_value(shell_tty, "TARGET SP        : ", context->stack_pointer);
    panic_write_label_value(shell_tty, "TARGET ROOT      : ", target_root);
    panic_write_label_value(shell_tty, "SAVED USER IP    : ", saved_user_ip);
    panic_write_label_value(shell_tty, "SAVED USER SP    : ", saved_user_sp);
    panic_write_str(shell_tty, "IP USER RANGE    : ");
    panic_write_str(shell_tty, kernel_addr_in_user_range(context->instruction_pointer) ? "PASS\n" : "FAIL\n");
    panic_write_str(shell_tty, "SP USER RANGE    : ");
    panic_write_str(shell_tty, kernel_addr_in_user_range(context->stack_pointer) ? "PASS\n" : "FAIL\n");
    panic_write_str(shell_tty, "TASK RUNNABLE    : ");
    if (task_runnable) {
        panic_write_str(shell_tty, "PASS\n");
    } else {
        panic_write_str(shell_tty, "FAIL");
        if (proc != 0) {
            panic_write_str(shell_tty, " state=");
            panic_write_str(shell_tty, kernel_process_state_name(proc->state));
        }
        panic_putc(shell_tty, '\n');
    }
}

static void kernel_panic_write_syscall_trace(struct tty *shell_tty) {
    if (!g_last_syscall_trace.valid) {
        return;
    }

    panic_write_str(shell_tty, "--- SYSCALL TRACE ---\n");
    panic_write_label_value(shell_tty, "LAST SYSCALL NO  : ", g_last_syscall_trace.number);
    panic_write_str(shell_tty, "LAST SYSCALL NAME: ");
    panic_write_str(shell_tty, kernel_syscall_name(g_last_syscall_trace.number));
    panic_putc(shell_tty, '\n');
    panic_write_label_value(shell_tty, "LAST SYSCALL ARG0: ", g_last_syscall_trace.arg0);
    panic_write_label_value(shell_tty, "LAST SYSCALL ARG1: ", g_last_syscall_trace.arg1);
    panic_write_label_value(shell_tty, "LAST SYSCALL ARG2: ", g_last_syscall_trace.arg2);
    panic_write_label_value(shell_tty, "LAST SYSCALL IP  : ", g_last_syscall_trace.instruction_pointer);
    panic_write_label_value(shell_tty, "LAST SYSCALL SP  : ", g_last_syscall_trace.stack_pointer);
    panic_write_label_value(shell_tty, "LAST SYSCALL RET : ", g_last_syscall_trace.result);
    panic_write_str(shell_tty, "SYSCALL RETURNED : ");
    panic_write_str(shell_tty, g_last_syscall_trace.returned ? "yes\n" : "no/kernel-resume\n");
    if (g_last_syscall_trace.number == SYS_EXIT && g_last_syscall_trace.returned) {
        panic_write_str(shell_tty, "BUG              : sys_exit returned to common syscall return path\n");
    }
}

static void kernel_panic_write_compact_syscall_trace(struct tty *shell_tty) {
    if (!g_last_syscall_trace.valid) {
        return;
    }

    panic_write_str(shell_tty, "--- SYSCALL SUMMARY ---\n");
    panic_write_label_value(shell_tty, "LAST SYSCALL NO  : ", g_last_syscall_trace.number);
    panic_write_str(shell_tty, "LAST SYSCALL NAME: ");
    panic_write_str(shell_tty, kernel_syscall_name(g_last_syscall_trace.number));
    panic_putc(shell_tty, '\n');
    panic_write_label_value(shell_tty, "LAST SYSCALL IP  : ", g_last_syscall_trace.instruction_pointer);
    panic_write_label_value(shell_tty, "LAST SYSCALL RET : ", g_last_syscall_trace.result);
}

static void kernel_panic_write_sched_trace_limited(struct tty *shell_tty, uint32_t max_events) {
    const struct sched_trace_event *events = 0;
    uint32_t count = 0;
    uint32_t next = 0;

    sched_trace_snapshot(&events, &count, &next);
    if (events == 0 || count == 0) {
        return;
    }
    if (max_events != 0u && count > max_events) {
        count = max_events;
    }

    panic_write_str(shell_tty, "--- LAST SCHEDULE EVENTS ---\n");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = (next + SCHED_TRACE_EVENT_COUNT - count + i) % SCHED_TRACE_EVENT_COUNT;
        const struct sched_trace_event *event = &events[index];

        panic_putc(shell_tty, '[');
        panic_write_dec(shell_tty, i);
        panic_write_str(shell_tty, "] tick=");
        panic_write_dec(shell_tty, event->tick);
        panic_write_str(shell_tty, " from=");
        panic_write_dec(shell_tty, event->from_pid);
        panic_putc(shell_tty, '(');
        panic_write_str(shell_tty, event->from_name);
        panic_write_str(shell_tty, " ");
        panic_write_str(shell_tty, kernel_process_state_name(event->from_state));
        panic_write_str(shell_tty, ") to=");
        panic_write_dec(shell_tty, event->to_pid);
        panic_putc(shell_tty, '(');
        panic_write_str(shell_tty, event->to_name);
        panic_write_str(shell_tty, " ");
        panic_write_str(shell_tty, kernel_process_state_name(event->to_state));
        panic_write_str(shell_tty, ") reason=");
        panic_write_str(shell_tty, event->reason);
        panic_putc(shell_tty, '\n');
    }
}

static void kernel_panic_write_page_fault_info(struct tty *shell_tty,
                                               const struct hal_exception_snapshot *context) {
    uint32_t page_error;

    if (context == 0) {
        return;
    }
    page_error = (uint32_t)context->error_code;

    panic_write_str(shell_tty, "--- PAGE FAULT INFO ---\n");
    panic_write_str(shell_tty, "Error Code = ");
    panic_write_hex64(shell_tty, page_error);
    panic_write_str(shell_tty, " (");
    panic_write_str(shell_tty, (page_error & 1u) ? "P " : "NP ");
    panic_write_str(shell_tty, (page_error & 2u) ? "W " : "R ");
    panic_write_str(shell_tty, (page_error & 4u) ? "U " : "S ");
    if (page_error & 8u) {
        panic_write_str(shell_tty, "RES ");
    }
    if (page_error & 16u) {
        panic_write_str(shell_tty, "IF ");
    }
    panic_write_str(shell_tty, ")\n");
}

static void kernel_panic_write_fault_maps(struct tty *shell_tty,
                                          const struct hal_exception_snapshot *context,
                                          const struct process *proc) {
    if (context == 0) {
        return;
    }

    panic_write_str(shell_tty, "FAULT ADDR       : ");
    panic_write_hex64(shell_tty, context->fault_address);
    panic_putc(shell_tty, '\n');
    kernel_panic_write_addr_map(shell_tty, "FAULT MAP        : ", context->fault_address);

    if (proc != 0 && proc->stack_top >= 8u) {
        panic_write_str(shell_tty, "STACK CHECK ADDR : ");
        panic_write_hex64(shell_tty, proc->stack_top - 8u);
        panic_putc(shell_tty, '\n');
        kernel_panic_write_addr_map(shell_tty, "STACK MAP        : ", proc->stack_top - 8u);
    }
}

static void kernel_panic_write_summary(struct tty *shell_tty,
                                       uint64_t current_user_raw_entry,
                                       const struct hal_exception_snapshot *context,
                                       const struct process *proc) {
    struct kernel_panic_trace_ctx trace_ctx;
    struct hal_boot_trace_ops trace_ops;
    uint64_t entry_phys = 0;
    uint64_t entry_flags = 0;
    uint64_t stack_check = 0;
    uint64_t stack_phys = 0;
    uint64_t stack_flags = 0;
    uint64_t target_root = context != 0 ? context->paging_root : 0;

    if (context == 0) {
        return;
    }

    if (proc != 0 && proc->stack_top >= 8u) {
        stack_check = proc->stack_top - 8u;
        (void)vmm_query_info(stack_check, &stack_phys, &stack_flags);
    }
    (void)vmm_query_info(current_user_raw_entry, &entry_phys, &entry_flags);

    panic_write_str(shell_tty, "--- SUMMARY ---\n");
    panic_write_pair(shell_tty, "FAULT=", context->fault_address, " ERR=", context->error_code);
    panic_write_pair(shell_tty, "ROOT=", context->paging_root, " IP=", context->instruction_pointer);
    panic_write_pair(shell_tty, "SP=", context->stack_pointer, " ENT=", current_user_raw_entry);
    panic_write_pair(shell_tty, "EPH=", entry_phys, " EFL=", entry_flags);
    panic_write_pair(shell_tty, "STK=", stack_check, " SPH=", stack_phys);
    panic_write_label_value(shell_tty, "SFL=", stack_flags);
    kernel_panic_trace_ops_for_tty(shell_tty, &trace_ctx, &trace_ops);
    hal_paging_log_panic_summary(&trace_ops,
                                 &trace_ctx,
                                 target_root,
                                 current_user_raw_entry);
}

void kernel_panic_handle_exception(struct tty *shell_tty,
                                   uint64_t current_user_raw_entry,
                                   uint32_t vector,
                                   const struct exception_frame *frame) {
    struct hal_exception_snapshot context = {0};
    const struct process *proc;
    enum kernel_panic_detail detail;

    if (frame == 0) {
        return;
    }

    hal_cpu_cli();
    hal_exception_snapshot(frame, &context);
    detail = kernel_panic_detail_for_console();
    kernel_panic_write_header(shell_tty, vector, &context, detail);
    if (detail == KERNEL_PANIC_DETAIL_COMPACT) {
        kernel_panic_write_compact_cpu_state(shell_tty, &context);
    } else {
        kernel_panic_write_cpu_state(shell_tty, &context);
    }
    kernel_panic_write_error_code_type(shell_tty, vector);

    panic_write_str(shell_tty, "--- PAGING REGISTERS ---\n");
    if (detail == KERNEL_PANIC_DETAIL_COMPACT) {
        panic_write_pair(shell_tty, "FAULT=", context.fault_address, " ROOT=", context.paging_root);
    } else {
        panic_write_reg4(shell_tty,
                         "CR0=", context.control0,
                         "FAULT=", context.fault_address,
                         "ROOT=", context.paging_root,
                         "CR4=", context.control4);
    }
    if (vector != 14) {
        panic_write_str(shell_tty, "FAULT NOTE       : stale/last page-fault address; not valid for this exception\n");
    }
    panic_write_label_value(shell_tty, "USER ENTRY       : ", current_user_raw_entry);
    proc = process_current(); 
    kernel_panic_write_process(shell_tty, proc);

    if (detail != KERNEL_PANIC_DETAIL_COMPACT) {
        kernel_panic_write_code_bytes(shell_tty, &context);
        kernel_panic_write_user_return_check(shell_tty, &context, proc);
    }
    if (detail == KERNEL_PANIC_DETAIL_COMPACT) {
        kernel_panic_write_compact_syscall_trace(shell_tty);
    } else {
        kernel_panic_write_syscall_trace(shell_tty);
        kernel_panic_write_sched_trace_limited(shell_tty,
                                              detail == KERNEL_PANIC_DETAIL_FULL ? 0u : 4u);
    }

    if (detail == KERNEL_PANIC_DETAIL_FULL) {
        kernel_panic_write_raw_trap_frame(shell_tty, frame, &context, vector, proc);
        kernel_panic_write_arch_paging_details(shell_tty, current_user_raw_entry, proc);
    } else {
        panic_write_str(0, "\n--- SERIAL-ONLY FULL PANIC DETAILS ---\n");
        if (detail == KERNEL_PANIC_DETAIL_COMPACT) {
            kernel_panic_write_cpu_state(0, &context);
            kernel_panic_write_code_bytes(0, &context);
            kernel_panic_write_user_return_check(0, &context, proc);
            kernel_panic_write_syscall_trace(0);
            kernel_panic_write_sched_trace_limited(0, 0u);
        }
        kernel_panic_write_raw_trap_frame(0, frame, &context, vector, proc);
        kernel_panic_write_arch_paging_details(0, current_user_raw_entry, proc);
    }

    if (vector == 14) {
        kernel_panic_write_page_fault_info(shell_tty, &context);
        if (detail != KERNEL_PANIC_DETAIL_COMPACT) {
            kernel_panic_write_fault_maps(shell_tty, &context, proc);
        } else {
            kernel_panic_write_fault_maps(0, &context, proc);
        }
        kernel_panic_write_summary(shell_tty, current_user_raw_entry, &context, proc);
    }

    panic_write_str(shell_tty, "\nSystem Halted.");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
