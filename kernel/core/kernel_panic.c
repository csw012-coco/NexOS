#include <stdint.h>
#include "drivers/serial/uart.h"
#include "hal/hal.h"
#include "kernel/internal/core/kernel_panic_internal.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/mem/address_space.h"
#include "kernel/public/mem/vmm.h"

struct kernel_fault_context {
    uint64_t cr0;
    uint64_t cr2;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t fault_rsp;
    uint64_t fault_ss;
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

static void panic_write_label_value(struct tty *shell_tty, const char *label, uint64_t value) {
    panic_write_str(shell_tty, label);
    panic_write_hex64(shell_tty, value);
    panic_putc(shell_tty, '\n');
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

static void kernel_collect_fault_context(const struct exception_frame *frame,
                                         struct kernel_fault_context *context) {
    const uint64_t *raw = (const uint64_t *)frame;

    if (frame == 0 || context == 0) {
        return;
    }

    __asm__ __volatile__("mov %%cr0, %0" : "=r"(context->cr0));
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(context->cr2));
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(context->cr3));
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(context->cr4));
    __asm__ __volatile__("mov %%ss, %0" : "=r"(context->fault_ss));

    if ((frame->cs & 0x3u) == 0x3u) {
        context->fault_rsp = raw[19];
        context->fault_ss = raw[20];
    } else {
        context->fault_rsp = (uint64_t)(uintptr_t)(frame + 1);
    }
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

static void kernel_panic_write_header(struct tty *shell_tty,
                                      uint32_t vector,
                                      const struct kernel_fault_context *context) {
    panic_clear(shell_tty);
    panic_write_str(shell_tty, "[");
    panic_write_str(shell_tty, "ERROR");
    panic_write_str(shell_tty, "]\n");
    panic_write_str(shell_tty, "========[ FATAL CPU EXCEPTION / KERNEL PANIC ]==========\n");

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

    panic_write_str(shell_tty, "Fault Address    : ");
    panic_write_hex64(shell_tty, context->cr2);
    panic_putc(shell_tty, '\n');
}

static void kernel_panic_write_cpu_state(struct tty *shell_tty,
                                         const struct exception_frame *frame,
                                         const struct kernel_fault_context *context) {
    panic_write_str(shell_tty, "--- CPU STATE ---\n");
    panic_write_reg4(shell_tty, "RAX=", frame->rax, "RBX=", frame->rbx, "RCX=", frame->rcx, "RDX=", frame->rdx);
    panic_write_reg4(shell_tty, "RSI=", frame->rsi, "RDI=", frame->rdi, "RBP=", frame->rbp, "RSP=", context->fault_rsp);
    panic_write_reg4(shell_tty, "R8 =", frame->r8, "R9 =", frame->r9, "R10=", frame->r10, "R11=", frame->r11);
    panic_write_reg4(shell_tty, "R12=", frame->r12, "R13=", frame->r13, "R14=", frame->r14, "R15=", frame->r15);
    panic_write_reg4(shell_tty, "RIP=", frame->rip, "RFL=", frame->rflags, "CS =", frame->cs, "SS =", context->fault_ss);
    panic_write_label_value(shell_tty, "Error Code       : ", frame->error_code);
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
}

static void kernel_panic_write_entry_map(struct tty *shell_tty, uint64_t current_user_raw_entry) {
    uint64_t entry_phys = 0;
    uint64_t entry_flags = 0;
    uint64_t pml4e = 0;
    uint64_t pdpte = 0;
    uint64_t pde = 0;
    uint64_t pte = 0;

    if (current_user_raw_entry == 0) {
        return;
    }

        panic_write_str(shell_tty, "ENTRY MAP        : ");
    if (vmm_query_info(current_user_raw_entry, &entry_phys, &entry_flags)) {
        panic_write_hex64(shell_tty, entry_phys);
        panic_write_str(shell_tty, " flags=");
        panic_write_hex64(shell_tty, entry_flags);
    } else {
        panic_write_str(shell_tty, "<unmapped>");
    }
    panic_putc(shell_tty, '\n');

    if (vmm_query_page_walk(current_user_raw_entry, &pml4e, &pdpte, &pde, &pte)) {
        panic_write_str(shell_tty, "ENTRY PML4E      : ");
        panic_write_hex64(shell_tty, pml4e);
        panic_putc(shell_tty, '\n');
        panic_write_str(shell_tty, "ENTRY PDPTE      : ");
        panic_write_hex64(shell_tty, pdpte);
        panic_putc(shell_tty, '\n');
        panic_write_str(shell_tty, "ENTRY PDE        : ");
        panic_write_hex64(shell_tty, pde);
        panic_putc(shell_tty, '\n');
        panic_write_str(shell_tty, "ENTRY PTE        : ");
        panic_write_hex64(shell_tty, pte);
        panic_putc(shell_tty, '\n');
    }

    if (entry_phys != 0) {
        uint64_t entry_page_off = current_user_raw_entry & 0xfffull;
        const uint8_t *entry_bytes = (const uint8_t *)hal_phys_direct_map(entry_phys + entry_page_off);
        uint32_t i;

        panic_write_str(shell_tty, "ENTRY BYTES      : ");
        for (i = 0; i < 8; i++) {
            if (i != 0) {
                panic_putc(shell_tty, ' ');
            }
            panic_write_hex64(shell_tty, entry_bytes[i]);
        }
        panic_putc(shell_tty, '\n');
    }
}

static void kernel_panic_write_target_entry_map(struct tty *shell_tty,
                                                uint64_t current_user_raw_entry,
                                                const struct process *proc) {
    uint64_t target_cr3;
    uint64_t entry_phys = 0;
    uint64_t entry_flags = 0;
    uint64_t pml4e = 0;
    uint64_t pdpte = 0;
    uint64_t pde = 0;
    uint64_t pte = 0;

    if (proc == 0 || proc->address_space == 0 || current_user_raw_entry == 0) {
        return;
    }

    target_cr3 = proc->address_space->user_cr3;
    if (target_cr3 == 0) {
        return;
    }

    panic_write_str(shell_tty, "TARGET USER CR3  : ");
    panic_write_hex64(shell_tty, target_cr3);
    panic_putc(shell_tty, '\n');

    panic_write_str(shell_tty, "TARGET ENTRY MAP : ");
    if (vmm_query_mapping_in_context(target_cr3, current_user_raw_entry, &entry_phys, &entry_flags)) {
        panic_write_hex64(shell_tty, entry_phys);
        panic_write_str(shell_tty, " flags=");
        panic_write_hex64(shell_tty, entry_flags);
    } else {
        panic_write_str(shell_tty, "<unmapped>");
    }
    panic_putc(shell_tty, '\n');

    if (vmm_query_page_walk_in_context(target_cr3, current_user_raw_entry, &pml4e, &pdpte, &pde, &pte)) {
        panic_write_str(shell_tty, "TARGET PML4E     : ");
        panic_write_hex64(shell_tty, pml4e);
        panic_putc(shell_tty, '\n');
        panic_write_str(shell_tty, "TARGET PDPTE     : ");
        panic_write_hex64(shell_tty, pdpte);
        panic_putc(shell_tty, '\n');
        panic_write_str(shell_tty, "TARGET PDE       : ");
        panic_write_hex64(shell_tty, pde);
        panic_putc(shell_tty, '\n');
        panic_write_str(shell_tty, "TARGET PTE       : ");
        panic_write_hex64(shell_tty, pte);
        panic_putc(shell_tty, '\n');
    }
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

static void kernel_panic_write_switch_trace(struct tty *shell_tty, uint64_t current_user_raw_entry) {
    struct vmm_page_fault_trace trace = {0};

    vmm_get_page_fault_trace(&trace);
    if (trace.requested_cr3 == 0 && trace.previous_cr3 == 0 && trace.actual_cr3 == 0) {
        return;
    }

    panic_write_str(shell_tty, "--- CR3 SWITCH TRACE ---\n");
    panic_write_reg4(shell_tty, "REQ=", trace.requested_cr3, "PREV=", trace.previous_cr3, "ACTUAL=", trace.actual_cr3, "FLAGS=", trace.reject_flags);
    panic_write_reg4(shell_tty, "CHK RIP=", trace.current_rip, "CHK RSP=", trace.current_rsp, "CUR CR3=", vmm_get_current_cr3(), "ENTRY=", current_user_raw_entry);

    if (trace.reject_flags != 0) {
        panic_write_str(shell_tty, "REJECT REASON    : ");
        if (trace.reject_flags & VMM_SWITCH_REJECT_ZERO) {
            panic_write_str(shell_tty, "ZERO ");
        }
        if (trace.reject_flags & VMM_SWITCH_REJECT_RIP_UNMAPPED) {
            panic_write_str(shell_tty, "RIP ");
        }
        if (trace.reject_flags & VMM_SWITCH_REJECT_RSP_UNMAPPED) {
            panic_write_str(shell_tty, "RSP ");
        }
        panic_putc(shell_tty, '\n');
    }
}

static void kernel_panic_write_page_fault_info(struct tty *shell_tty, const struct exception_frame *frame) {
    uint32_t page_error = (uint32_t)frame->error_code;

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
                                          const struct kernel_fault_context *context,
                                          const struct process *proc) {
    if (context == 0) {
        return;
    }

    panic_write_str(shell_tty, "FAULT ADDR       : ");
    panic_write_hex64(shell_tty, context->cr2);
    panic_putc(shell_tty, '\n');
    kernel_panic_write_addr_map(shell_tty, "FAULT MAP        : ", context->cr2);

    if (proc != 0 && proc->stack_top >= 8u) {
        panic_write_str(shell_tty, "STACK CHECK ADDR : ");
        panic_write_hex64(shell_tty, proc->stack_top - 8u);
        panic_putc(shell_tty, '\n');
        kernel_panic_write_addr_map(shell_tty, "STACK MAP        : ", proc->stack_top - 8u);
    }
}

static void kernel_panic_write_summary(struct tty *shell_tty,
                                       uint64_t current_user_raw_entry,
                                       const struct kernel_fault_context *context,
                                       const struct exception_frame *frame,
                                       const struct process *proc) {
    struct vmm_page_fault_trace trace = {0};
    struct vmm_page_walk_info walk = {0};
    uint64_t entry_phys = 0;
    uint64_t entry_flags = 0;
    uint64_t stack_check = 0;
    uint64_t stack_phys = 0;
    uint64_t stack_flags = 0;

    if (context == 0 || frame == 0) {
        return;
    }

    vmm_get_page_fault_trace(&trace);
    (void)vmm_query_page_walk_full(context->cr3, current_user_raw_entry, &walk);
    if (proc != 0 && proc->stack_top >= 8u) {
        stack_check = proc->stack_top - 8u;
        (void)vmm_query_info(stack_check, &stack_phys, &stack_flags);
    }
    (void)vmm_query_info(current_user_raw_entry, &entry_phys, &entry_flags);

    panic_write_str(shell_tty, "--- SUMMARY ---\n");
    panic_write_pair(shell_tty, "CR2=", context->cr2, " ERR=", frame->error_code);
    panic_write_pair(shell_tty, "CR3=", context->cr3, " SW=", trace.reject_flags);
    panic_write_pair(shell_tty, "RIP=", frame->rip, " RSP=", context->fault_rsp);
    panic_write_pair(shell_tty, "ENT=", current_user_raw_entry, " EPH=", entry_phys);
    panic_write_pair(shell_tty, "EFL=", entry_flags, " STK=", stack_check);
    panic_write_pair(shell_tty, "SPH=", stack_phys, " SFL=", stack_flags);
    panic_write_pair(shell_tty, "PML4=", walk.pml4_phys, " PDPT=", walk.pdpt_phys);
    panic_write_pair(shell_tty, "PD=", walk.pd_phys, " PT=", walk.pt_phys);
}

void kernel_panic_handle_exception(struct tty *shell_tty,
                                   uint64_t current_user_raw_entry,
                                   uint32_t vector,
                                   const struct exception_frame *frame) {
    struct kernel_fault_context context = {0};
    const struct process *proc;

    if (frame == 0) {
        return;
    }

    hal_cpu_cli();
    kernel_collect_fault_context(frame, &context);
    kernel_panic_write_header(shell_tty, vector, &context);
    kernel_panic_write_cpu_state(shell_tty, frame, &context);

    panic_write_str(shell_tty, "--- PAGING REGISTERS ---\n");
    panic_write_reg4(shell_tty, "CR0=", context.cr0, "CR2=", context.cr2, "CR3=", context.cr3, "CR4=", context.cr4);
    panic_write_label_value(shell_tty, "USER ENTRY       : ", current_user_raw_entry);
    proc = process_current();
    kernel_panic_write_process(shell_tty, proc);
    kernel_panic_write_entry_map(shell_tty, current_user_raw_entry);
    kernel_panic_write_target_entry_map(shell_tty, current_user_raw_entry, proc);
    kernel_panic_write_switch_trace(shell_tty, current_user_raw_entry);

    if (vector == 14) {
        kernel_panic_write_page_fault_info(shell_tty, frame);
        kernel_panic_write_fault_maps(shell_tty, &context, proc);
        kernel_panic_write_summary(shell_tty, current_user_raw_entry, &context, frame, proc);
    }

    panic_write_str(shell_tty, "\nSystem Halted.");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
