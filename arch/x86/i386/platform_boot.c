typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#include "bootx.h"
#include "block/blockdev.h"
#include "drivers/bus/pci.h"
#include "drivers/storage/ata.h"
#include "fs/early_vfs.h"
#include "context.h"
#include "gdt.h"
#include "hal/early.h"
#include "idt.h"
#include "kernel/internal/core/boot_state_internal.h"
#include "kernel/public/arch/arch_ops.h"
#include "kernel/public/core/early_boot.h"
#include "kernel/public/core/early_console.h"
#include "kernel/public/core/kprint.h"
#include "kernel/public/proc/context.h"
#include "kernel/public/proc/process.h"
#include "kernel/internal/sys/syscall_compat32_internal.h"
#include "keyboard.h"
#include "lib/string.h"
#include "paging.h"
#include "process32.h"
#include "arch/x86/common/pic.h"
#include "pmm.h"
#include "scheduler.h"
#include "user.h"

enum {
    COM1 = 0x3f8,
    VGA_WIDTH = 80,
    VGA_HEIGHT = 25
};

static volatile uint16_t *const vga = (volatile uint16_t *)0xb8000;
static uint32_t vga_row;
static uint32_t vga_column;
static volatile uint32_t breakpoint_count;
static volatile uint32_t page_fault_count;
static volatile uint32_t page_fault_expected;
static volatile uint32_t page_fault_cr2;
static volatile uint32_t page_fault_error;
static volatile uint32_t irq_count;
static volatile uint32_t irq_user_count;
static volatile uint32_t irq_line_count[16];
static volatile uint32_t paging_probe = 0x12345678u;
static volatile uint32_t user_syscall_count;
static volatile uint32_t user_syscall_number;
static volatile uint32_t user_syscall_argument;
static struct block_device early_test_block;
static struct early_vfs early_filesystem;

extern void hal_display_load_font(const struct bootx_boot_info *boot_info);
static uint32_t i386_context_action_to_frame(uintptr_t action);
extern void hal_display_init(const struct bootx_console_info *console);
extern void kernel_i386_query_init(const struct syscall_boot_info *info,
                                   const struct syscall_framebuffer_info *fb_info,
                                   const struct bootx_boot_info *raw_boot_info,
                                   uint32_t cmdline,
                                   uint32_t memmap,
                                   uint32_t memmap_count);
extern void kernel_i386_compat32_context(
    struct syscall_compat32_context *ctx);

static int early_test_block_read(struct block_device *dev,
                                 uint64_t lba,
                                 uint32_t count,
                                 void *buffer) {
    uint8_t *bytes = (uint8_t *)buffer;

    (void)dev;
    if (lba != 0u || count != 1u || buffer == 0) {
        return -1;
    }
    memset(bytes, 0, 512u);
    bytes[446] = 0x80u;
    bytes[450] = 0x83u;
    bytes[454] = 0x01u;
    bytes[458] = 0x20u;
    bytes[510] = 0x55u;
    bytes[511] = 0xaau;
    return 0;
}

static void i386_console_clear(void) {
    const uint16_t blank = (uint16_t)(0x0fu << 8) | (uint8_t)' ';

    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    vga_row = 0;
    vga_column = 0;
}

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    out8(COM1 + 1, 0x00);
    out8(COM1 + 3, 0x80);
    out8(COM1 + 0, 0x03);
    out8(COM1 + 1, 0x00);
    out8(COM1 + 3, 0x03);
    out8(COM1 + 2, 0xc7);
    out8(COM1 + 4, 0x0b);
}

static void serial_putc(char ch) {
    while ((in8(COM1 + 5) & 0x20u) == 0u) {
    }
    out8(COM1, (uint8_t)ch);
}

static void serial_write(const char *text) {
    while (text != 0 && *text != '\0') {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}

static void i386_console_putc(char ch) {
    if (ch == '\n') {
        vga_column = 0;
        if (++vga_row >= VGA_HEIGHT) {
            vga_row = 0;
        }
        serial_putc('\r');
        serial_putc('\n');
        return;
    }

    vga[vga_row * VGA_WIDTH + vga_column] = (uint16_t)(0x0fu << 8) | (uint8_t)ch;
    if (++vga_column >= VGA_WIDTH) {
        vga_column = 0;
        if (++vga_row >= VGA_HEIGHT) {
            vga_row = 0;
        }
    }
    serial_putc(ch);
}

static uint16_t read_cs(void) {
    uint16_t value;
    __asm__ volatile("mov %%cs, %0" : "=r"(value));
    return value;
}

static uint16_t read_ds(void) {
    uint16_t value;
    __asm__ volatile("mov %%ds, %0" : "=r"(value));
    return value;
}

static uint16_t read_es(void) {
    uint16_t value;
    __asm__ volatile("mov %%es, %0" : "=r"(value));
    return value;
}

static uint16_t read_fs(void) {
    uint16_t value;
    __asm__ volatile("mov %%fs, %0" : "=r"(value));
    return value;
}

static uint16_t read_gs(void) {
    uint16_t value;
    __asm__ volatile("mov %%gs, %0" : "=r"(value));
    return value;
}

static uint16_t read_ss(void) {
    uint16_t value;
    __asm__ volatile("mov %%ss, %0" : "=r"(value));
    return value;
}

static int gdt_verify(void) {
    struct i386_gdtr current;
    uint16_t cs = read_cs();
    uint16_t ds = read_ds();
    uint16_t es = read_es();
    uint16_t fs = read_fs();
    uint16_t gs = read_gs();
    uint16_t ss = read_ss();

    i386_gdt_read(&current);
    if (current.base == i386_gdt_base() &&
        current.limit == i386_gdt_limit() &&
        cs == I386_GDT_KERNEL_CODE &&
        ds == I386_GDT_KERNEL_DATA &&
        es == I386_GDT_KERNEL_DATA &&
        fs == I386_GDT_KERNEL_DATA &&
        gs == I386_GDT_KERNEL_DATA &&
        ss == I386_GDT_KERNEL_DATA) {
        return 1;
    }

    return 0;
}

static void i386_context_from_exception(struct process_context *context,
                                        const struct i386_exception_frame *frame) {
    if (context == 0 || frame == 0) {
        return;
    }
    process_context_reset(context);
    context->registers[PROCESS_CONTEXT_RETURN] = frame->eax;
    context->registers[PROCESS_CONTEXT_ARG0] = frame->ebx;
    context->registers[PROCESS_CONTEXT_ARG1] = frame->ecx;
    context->registers[PROCESS_CONTEXT_ARG2] = frame->edx;
    context->registers[PROCESS_CONTEXT_ARG3] = frame->esi;
    context->registers[PROCESS_CONTEXT_GENERAL0] = frame->edi;
    context->registers[PROCESS_CONTEXT_GENERAL1] = frame->ebp;
    context->registers[PROCESS_CONTEXT_STACK_SNAPSHOT] = frame->saved_esp;
    context->instruction_pointer = frame->eip;
    context->stack_pointer = frame->user_esp;
    context->flags = frame->eflags;
    context->code_selector = (uint16_t)frame->cs;
    context->stack_selector = (uint16_t)frame->user_ss;
    context->user_mode = (frame->cs & 3u) == 3u;
}

uint32_t i386_exception_handler(uint32_t vector, struct i386_exception_frame *frame) {
    if (vector == I386_IDT_VECTOR_BREAKPOINT) {
        breakpoint_count++;
        return 0u;
    }

    if (vector == I386_IDT_VECTOR_PAGE_FAULT) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(page_fault_cr2));
        page_fault_error = frame->error_code;
        page_fault_count++;

        if (page_fault_expected &&
            frame->eip == (uint32_t)i386_page_fault_test_fault) {
            frame->eip = (uint32_t)i386_page_fault_test_recovery;
            return 0u;
        }
        if ((frame->cs & 3u) == 3u &&
            i386_scheduler_handle_page_fault(page_fault_cr2,
                                              frame->error_code)) {
            return 0u;
        }
        if ((frame->cs & 3u) == 3u) {
            struct process_context context;
            struct syscall_compat32_context service_context;
            uintptr_t action;

            i386_context_from_exception(&context, frame);
            kernel_i386_compat32_context(&service_context);
            service_context.pid = i386_scheduler_current_pid();
            service_context.ticks = irq_line_count[0];
            syscall_compat32_cleanup_pid(&service_context, service_context.pid);
            action = i386_scheduler_fault_exit(&context, -14);
            return i386_context_action_to_frame(action);
        }
    }

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static uint32_t i386_context_action_to_frame(uintptr_t action) {
    if (action <= 1u) {
        return (uint32_t)action;
    }
    return (uint32_t)i386_context_to_irq(
        (const struct process_context *)action);
}

uint32_t i386_syscall_handler(struct i386_syscall_frame *frame) {
    struct process_context schedule_context;
    struct syscall_compat32_context service_context;
    struct kernel_syscall_request request = {0};
    struct kernel_syscall_result result;

    kernel_i386_compat32_context(&service_context);
    service_context.pid = i386_scheduler_current_pid();
    service_context.ticks = irq_line_count[0];
    arch->syscall_decode(frame, &request);
    if (!syscall_compat32_dispatch_request(&service_context, &request, &result)) {
        result.value = 0u;
        result.action = SYSCALL_RESULT_RETURN;
    }

    user_syscall_count++;
    user_syscall_number = request.number;
    user_syscall_argument = kernel_syscall_arg_u32(&request, 0);

    if (result.action == SYSCALL_RESULT_EXEC) {
        char command[512];
        uintptr_t action;

        if (!arch_copy_user_cstr(command, (uint32_t)result.value, sizeof(command))) {
            arch->syscall_set_return(frame, (uint32_t)-1);
            return 0u;
        }
        i386_context_from_syscall(&schedule_context, frame);
        action = process32_exec_replace_from_user(&schedule_context, command);
        if (action == 0u) {
            arch->syscall_set_return(frame, (uint32_t)-1);
            return 0u;
        }
        return i386_context_action_to_frame(action);
    }
    if (result.action == SYSCALL_RESULT_WAIT) {
        int32_t status = -1;
        int blocked = 0;
        uintptr_t action;

        i386_context_from_syscall(&schedule_context, frame);
        action = syscall_compat32_wait(&service_context,
                                        &schedule_context,
                                        (uint32_t)result.value,
                                        &status,
                                        &blocked);
        if (blocked && action != 0u) {
            return i386_context_action_to_frame(action);
        }
        arch->syscall_set_return(frame, (uint32_t)status);
        return 0u;
    }
    if (result.action == SYSCALL_RESULT_SLEEP) {
        uintptr_t action;

        arch->syscall_set_return(frame, 0u);
        i386_context_from_syscall(&schedule_context, frame);
        action = syscall_compat32_sleep(&service_context,
                                         &schedule_context,
                                         (uint32_t)result.value);
        return i386_context_action_to_frame(action);
    }
    if (result.action == SYSCALL_RESULT_EXIT) {
        uintptr_t action;

        i386_context_from_syscall(&schedule_context, frame);
        action = syscall_compat32_exit(&service_context,
                                        &schedule_context,
                                        (int)result.value);
        return action != 0u
            ? i386_context_action_to_frame(action)
            : 1u;
    }
    if (result.action == SYSCALL_RESULT_YIELD) {
        uintptr_t action;

        i386_context_from_syscall(&schedule_context, frame);
        action = syscall_compat32_yield(&service_context,
                                         &schedule_context);
        if (action != 0u) {
            return i386_context_action_to_frame(action);
        }
    }
    arch->syscall_set_return(frame, (uintptr_t)result.value);
    return 0;
}

struct i386_irq_frame *i386_irq_handler(uint32_t irq,
                                        struct i386_irq_frame *frame) {
    irq_count++;
    if (irq < 16u) {
        irq_line_count[irq]++;
    }
    if ((frame->cs & 3u) == 3u) {
        irq_user_count++;
    }
    kernel_irq_state_record(irq, (frame->cs & 3u) == 3u);
    if (irq == 1u) {
        i386_keyboard_handle_irq();
    }
    i386_pic_send_eoi((uint8_t)irq);
    if (irq == 0u) {
        struct process_context current_context;
        const struct process_context *next_context;

        i386_context_from_irq(&current_context, frame);
        next_context = i386_scheduler_tick(&current_context);
        return next_context == &current_context
            ? frame
            : i386_context_to_irq(next_context);
    }
    return frame;
}

static int idt_verify(void) {
    struct i386_idtr current;

    i386_idt_read(&current);
    return current.base == i386_idt_base() &&
           current.limit == i386_idt_limit() &&
           i386_idt_gate_matches(I386_IDT_VECTOR_BREAKPOINT,
                                 (uint32_t)i386_isr3,
                                 I386_GDT_KERNEL_CODE,
                                 I386_IDT_GATE_INTERRUPT32) &&
           i386_idt_gate_matches(I386_IDT_VECTOR_PAGE_FAULT,
                                 (uint32_t)i386_isr14,
                                 I386_GDT_KERNEL_CODE,
                                 I386_IDT_GATE_INTERRUPT32) &&
           i386_idt_gate_matches(I386_IDT_VECTOR_IRQ_BASE,
                                 (uint32_t)i386_irq0,
                                 I386_GDT_KERNEL_CODE,
                                 I386_IDT_GATE_INTERRUPT32) &&
           i386_idt_gate_matches(I386_IDT_VECTOR_IRQ_BASE + 15u,
                                 (uint32_t)i386_irq15,
                                 I386_GDT_KERNEL_CODE,
                                 I386_IDT_GATE_INTERRUPT32) &&
           i386_idt_gate_matches(I386_IDT_VECTOR_SYSCALL,
                                 (uint32_t)i386_syscall_stub,
                                 I386_GDT_KERNEL_CODE,
                                 I386_IDT_GATE_USER_INTERRUPT32);
}

static int paging_identity_matches(uint32_t address) {
    uint32_t physical = 0;
    return i386_paging_translate(address, &physical) && physical == address;
}

static int paging_verify(const struct bootx_boot_info *boot_info) {
    uint32_t stack_address;

    __asm__ volatile("mov %%esp, %0" : "=r"(stack_address));
    return i386_paging_enabled() &&
           i386_paging_root() != 0u &&
           paging_identity_matches((uint32_t)paging_verify) &&
           paging_identity_matches(stack_address) &&
           paging_identity_matches((uint32_t)boot_info) &&
           paging_identity_matches((uint32_t)vga) &&
           paging_identity_matches((uint32_t)&paging_probe);
}

static void i386_halt(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static int i386_segments_init(void) {
    i386_gdt_init();
    return gdt_verify();
}

static int i386_interrupts_init(void) {
    i386_idt_init();
    if (!idt_verify()) {
        return 0;
    }
    i386_pic_init();
    i386_pit_init(1000u);
    i386_keyboard_init();
    i386_pic_set_mask(0u, 0);
    i386_pic_set_mask(1u, 0);
    return i386_pic_master_mask() == 0xfcu &&
           i386_pic_slave_mask() == 0xffu;
}

static int i386_irq_test(struct kernel_early_boot_report *report) {
    irq_count = 0;
    irq_user_count = 0;
    kernel_irq_state_reset();
    for (uint32_t irq = 0; irq < 16u; irq++) {
        irq_line_count[irq] = 0;
    }
    __asm__ volatile("sti");
    while (irq_count == 0u) {
        __asm__ volatile("hlt");
    }
    __asm__ volatile("cli");

    report->irq_count = irq_count;
    report->irq_user_count = irq_user_count;
    report->pic_masks =
        ((uint32_t)i386_pic_master_mask() << 8) |
        i386_pic_slave_mask();
    return irq_count != 0u &&
           irq_user_count == 0u &&
           report->pic_masks == 0x0000fcffu;
}

static int i386_keyboard_test(struct kernel_early_boot_report *report) {
    struct i386_key_event event;
    uint32_t timer_start = irq_line_count[0];
    uint32_t keyboard_start = i386_keyboard_irq_count();

    if (!i386_keyboard_inject_scancode(0x1eu)) {
        return 0;
    }

    __asm__ volatile("sti");
    while (i386_keyboard_irq_count() == keyboard_start &&
           irq_line_count[0] - timer_start < 100u) {
        __asm__ volatile("hlt");
    }
    __asm__ volatile("cli");

    if (i386_keyboard_irq_count() != keyboard_start + 1u ||
        !i386_keyboard_pop(&event) ||
        event.scancode != 0x1eu ||
        !event.pressed ||
        event.ascii != 'a') {
        return 0;
    }

    report->keyboard_irq_count = i386_keyboard_irq_count();
    report->keyboard_scancode = event.scancode;
    report->keyboard_ascii = (uint32_t)(uint8_t)event.ascii;
    report->keyboard_dropped = i386_keyboard_dropped();
    return 1;
}

static int i386_breakpoint_test(void) {
    breakpoint_count = 0;
    __asm__ volatile("int3");
    return breakpoint_count == 1u;
}

static int i386_paging_stage(const struct bootx_boot_info *boot_info,
                             struct kernel_early_boot_report *report) {
    if (!i386_paging_init() || !paging_verify(boot_info)) {
        return 0;
    }
    paging_probe ^= 0xffffffffu;
    if (paging_probe != 0xedcba987u) {
        return 0;
    }
    report->paging_root = i386_paging_root();
    return 1;
}

static int i386_page_fault_test(struct kernel_early_boot_report *report) {
    page_fault_count = 0;
    page_fault_expected = 1;
    page_fault_cr2 = 0;
    page_fault_error = 0xffffffffu;
    i386_trigger_page_fault();
    page_fault_expected = 0;

    report->page_fault_address = page_fault_cr2;
    report->page_fault_error = page_fault_error;
    return page_fault_count == 1u &&
           page_fault_cr2 == 0x02000000u &&
           page_fault_error == 0x00000002u;
}

static int i386_pmm_stage(const struct bootx_boot_info *boot_info,
                          struct kernel_early_boot_report *report) {
    uint32_t free_before;
    uint32_t page_a;
    uint32_t page_b;
    uint32_t page_c;

    if (!i386_pmm_init(boot_info)) {
        return 0;
    }

    free_before = i386_pmm_free_pages();
    page_a = i386_pmm_alloc_page();
    page_b = i386_pmm_alloc_page();
    if (page_a == I386_PMM_INVALID_PAGE ||
        page_b == I386_PMM_INVALID_PAGE ||
        page_a == page_b ||
        page_a < I386_PAGING_IDENTITY_LIMIT ||
        page_b < I386_PAGING_IDENTITY_LIMIT ||
        i386_pmm_free_pages() + 2u != free_before ||
        !i386_pmm_free_page(page_a)) {
        return 0;
    }

    page_c = i386_pmm_alloc_page();
    if (page_c != page_a ||
        !i386_pmm_free_page(page_b) ||
        !i386_pmm_free_page(page_c) ||
        i386_pmm_free_pages() != free_before) {
        return 0;
    }

    report->pmm_total_pages = i386_pmm_total_pages();
    report->pmm_free_pages = i386_pmm_free_pages();
    report->pmm_reserved_pages = i386_pmm_reserved_pages();
    report->pmm_first_page = page_a;
    report->pmm_second_page = page_b;
    return 1;
}

static int i386_dynamic_mapping_test(struct kernel_early_boot_report *report) {
    uint32_t mapped_frame = i386_pmm_alloc_page();
    uint32_t translated = 0;
    uint32_t unmapped_frame = 0;
    volatile uint32_t *mapping_a =
        (volatile uint32_t *)(I386_PAGING_DYNAMIC_BASE + 0x0000u);
    volatile uint32_t *mapping_b =
        (volatile uint32_t *)(I386_PAGING_DYNAMIC_BASE + 0x1000u);

    if (mapped_frame == I386_PMM_INVALID_PAGE ||
        !i386_paging_map_page((uint32_t)mapping_a, mapped_frame, 1, 0) ||
        !i386_paging_translate((uint32_t)mapping_a, &translated) ||
        translated != mapped_frame) {
        return 0;
    }

    mapping_a[0] = 0x4e45584fu;
    mapping_a[1023] = 0x69333836u;
    if (!i386_paging_unmap_page((uint32_t)mapping_a, &unmapped_frame) ||
        unmapped_frame != mapped_frame ||
        i386_paging_translate((uint32_t)mapping_a, &translated) ||
        !i386_paging_map_page((uint32_t)mapping_b, mapped_frame, 1, 0) ||
        mapping_b[0] != 0x4e45584fu ||
        mapping_b[1023] != 0x69333836u) {
        return 0;
    }

    if (!i386_paging_unmap_page((uint32_t)mapping_b, &unmapped_frame) ||
        unmapped_frame != mapped_frame ||
        !i386_pmm_free_page(mapped_frame)) {
        return 0;
    }
    report->dynamic_mapping_frame = mapped_frame;
    return 1;
}

static int i386_devices_init(struct kernel_early_boot_report *report) {
    struct pci_device_info device;
    uint32_t index = 0;
    uint32_t storage_count = 0;
    struct blockdev_partition partition;
    uint8_t boot_sector[512];

    while (pci_find_device_by_index(index, &device)) {
        if (index == 0u) {
            report->pci_first_vendor_device =
                ((uint32_t)device.vendor_id << 16) | device.device_id;
        }
        if (device.class_code == 0x01u) {
            storage_count++;
        }
        index++;
    }
    if (index == 0u) {
        return 0;
    }
    report->pci_device_count = index;
    report->pci_storage_count = storage_count;

    blockdev_init();
    memset(&early_test_block, 0, sizeof(early_test_block));
    early_test_block.name = "early-mbr";
    early_test_block.block_size = 512u;
    early_test_block.block_count = 64u;
    early_test_block.read = early_test_block_read;
    if (blockdev_register(&early_test_block) != 0 ||
        blockdev_count() != 1u ||
        blockdev_partition_count(&early_test_block) != 1u ||
        blockdev_partition_get(&early_test_block, 0u, &partition) != 0 ||
        partition.type != 0x83u ||
        partition.start_lba != 1u ||
        partition.sector_count != 32u) {
        return 0;
    }
    if (blockdev_unregister(&early_test_block) != 0) {
        return 0;
    }

    blockdev_init();
    ata_init();
    struct ata_device *primary = ata_get_primary_master();
    if (primary == 0 || !primary->present ||
        blockdev_count() == 0u ||
        blockdev_get(0u) != &primary->blockdev ||
        blockdev_partition_count(&primary->blockdev) == 0u ||
        blockdev_partition_get(&primary->blockdev, 0u, &partition) != 0 ||
        !partition.bootable ||
        partition.start_lba != 2048u ||
        partition.type != 0x0cu ||
        blockdev_read(&primary->blockdev, partition.start_lba, 1u, boot_sector) != 0 ||
        boot_sector[510] != 0x55u ||
        boot_sector[511] != 0xaau ||
        boot_sector[82] != 'F' ||
        boot_sector[83] != 'A' ||
        boot_sector[84] != 'T' ||
        boot_sector[85] != '3' ||
        boot_sector[86] != '2') {
        return 0;
    }

    report->block_device_count = blockdev_count();
    report->block_partition_count = blockdev_partition_count(&primary->blockdev);
    report->block_first_partition_lba = (uint32_t)partition.start_lba;
    report->ata_sector_count = primary->sector_count;
    report->block_boot_signature =
        ((uint32_t)boot_sector[510] << 8) | boot_sector[511];
    return 1;
}

static int i386_filesystem_init(struct kernel_early_boot_report *report) {
    struct early_vfs_node node;
    uint8_t elf_header[52];
    uint32_t bytes_read = 0;
    uint16_t machine;
    uint32_t entry;

    early_vfs_init(&early_filesystem);
    if (early_vfs_mount_fat32(&early_filesystem, 0u, 0u) != 0 ||
        early_vfs_open(&early_filesystem, "/BOOT/NEX386.ELF", &node) != 0 ||
        early_vfs_read(&early_filesystem,
                       &node,
                       0u,
                       elf_header,
                       sizeof(elf_header),
                       &bytes_read) != 0 ||
        bytes_read != sizeof(elf_header) ||
        elf_header[0] != 0x7fu ||
        elf_header[1] != 'E' ||
        elf_header[2] != 'L' ||
        elf_header[3] != 'F' ||
        elf_header[4] != 1u ||
        elf_header[5] != 1u) {
        return 0;
    }

    machine = (uint16_t)elf_header[18] | ((uint16_t)elf_header[19] << 8);
    entry = (uint32_t)elf_header[24] |
            ((uint32_t)elf_header[25] << 8) |
            ((uint32_t)elf_header[26] << 16) |
            ((uint32_t)elf_header[27] << 24);
    if (machine != 3u || entry != 0x00100000u) {
        return 0;
    }

    report->vfs_file_size = early_vfs_file_size(&node);
    report->vfs_read_bytes = bytes_read;
    report->vfs_elf_class_machine = ((uint32_t)elf_header[4] << 16) | machine;
    report->vfs_elf_entry = entry;
    return 1;
}

static int i386_usermode_test(struct kernel_early_boot_report *report) {
    uint32_t entry;
    uint32_t stack_top;

    if (!i386_user_load_elf(&early_filesystem,
                            "/BOOT/USER32.ELF",
                            &entry,
                            &stack_top)) {
        return 0;
    }

    user_syscall_count = 0;
    user_syscall_number = 0;
    user_syscall_argument = 0;
    if (!i386_usermode_enter(entry, stack_top)) {
        return 0;
    }
    if (user_syscall_count != 1u ||
        user_syscall_number != 0u ||
        user_syscall_argument != 0x52494e47u ||
        irq_user_count == 0u) {
        return 0;
    }
    report->user_entry = entry;
    report->user_stack = stack_top;
    report->user_syscall_number = user_syscall_number;
    report->user_syscall_argument = user_syscall_argument;
    report->irq_count = irq_count;
    report->irq_user_count = irq_user_count;
    return 1;
}

static int i386_scheduler_test(struct kernel_early_boot_report *report) {
    enum {
        TASK0_STACK = 0xbfffe000u,
        TASK1_STACK = 0xbfffe000u,
        SCHEDULER_COUNTER = 0x40011000u,
        SCHEDULER_EXPECTED_RESULT = 0x00300000u
    };
    struct i386_user_image task0;
    struct i386_user_image task1;
    uint32_t task0_counter_frame;
    uint32_t task1_counter_frame;
    struct process_snapshot process0;
    struct process_snapshot process1;

    if (!i386_user_load_elf_space(&early_filesystem,
                                  "/BOOT/SCHED32.ELF",
                                  TASK0_STACK,
                                  &task0) ||
        !i386_user_load_elf_space(&early_filesystem,
                                  "/BOOT/SCHED32.ELF",
                                  TASK1_STACK,
                                  &task1) ||
        task0.root == task1.root ||
        !i386_paging_translate_in(task0.root,
                                  SCHEDULER_COUNTER,
                                  &task0_counter_frame) ||
        !i386_paging_translate_in(task1.root,
                                  SCHEDULER_COUNTER,
                                  &task1_counter_frame) ||
        (task0_counter_frame & ~(I386_PAGE_SIZE - 1u)) ==
            (task1_counter_frame & ~(I386_PAGE_SIZE - 1u)) ||
        !i386_scheduler_run(task0.entry,
                            task0.stack_top,
                            task0.root,
                            task1.entry,
                            task1.stack_top,
                            task1.root) ||
        i386_paging_root() != i386_paging_kernel_root() ||
        !i386_scheduler_process_snapshot(0u, &process0) ||
        !i386_scheduler_process_snapshot(1u, &process1)) {
        return 0;
    }

    report->scheduler_ticks = i386_scheduler_ticks();
    report->scheduler_switches = i386_scheduler_switches();
    report->scheduler_completed = i386_scheduler_completed();
    report->scheduler_task0_ticks = i386_scheduler_task_ticks(0u);
    report->scheduler_task1_ticks = i386_scheduler_task_ticks(1u);
    report->scheduler_task0_root = i386_scheduler_task_root(0u);
    report->scheduler_task1_root = i386_scheduler_task_root(1u);
    report->scheduler_task0_result = i386_scheduler_task_result(0u);
    report->scheduler_task1_result = i386_scheduler_task_result(1u);
    return report->scheduler_completed == 2u &&
           report->scheduler_switches >= 2u &&
           report->scheduler_task0_ticks != 0u &&
           report->scheduler_task1_ticks != 0u &&
           report->scheduler_task0_root != report->scheduler_task1_root &&
           report->scheduler_task0_result == SCHEDULER_EXPECTED_RESULT &&
           report->scheduler_task1_result == SCHEDULER_EXPECTED_RESULT &&
           process0.pid != 0u &&
           process1.pid != 0u &&
           process0.pid != process1.pid &&
           process0.state == PROCESS_STATE_EXITED &&
           process1.state == PROCESS_STATE_EXITED &&
           process0.exit_code == (int32_t)process0.pid &&
           process1.exit_code == (int32_t)process1.pid;
}

static const struct kernel_early_boot_ops i386_early_boot_ops
    __attribute__((unused)) = {
    .architecture = "i386",
    .segments_init = i386_segments_init,
    .interrupts_init = i386_interrupts_init,
    .irq_test = i386_irq_test,
    .keyboard_test = i386_keyboard_test,
    .breakpoint_test = i386_breakpoint_test,
    .paging_init = i386_paging_stage,
    .page_fault_test = i386_page_fault_test,
    .pmm_init = i386_pmm_stage,
    .dynamic_mapping_test = i386_dynamic_mapping_test,
    .devices_init = i386_devices_init,
    .filesystem_init = i386_filesystem_init,
    .usermode_test = i386_usermode_test,
    .scheduler_test = i386_scheduler_test,
};

static const struct hal_early_ops i386_hal_early_ops = {
    .console_init = serial_init,
    .console_clear = i386_console_clear,
    .console_putc = i386_console_putc,
    .halt = i386_halt,
};

extern int kernel_i386_shared_services_init(void);
extern void kernel_i386_shared_services_run(void);
extern int kernel_i386_selftest_verbose(void);

int kernel_i386_run_test32(struct process_snapshot *process0,
                           struct process_snapshot *process1) {
    enum {
        TEST_STACK_TOP = 0xbfffe000u
    };
    struct i386_user_image task0;

    if (!i386_user_load_elf_space(&early_filesystem,
                                  "/BOOT/TEST32.ELF",
                                  TEST_STACK_TOP,
                                  &task0) ||
        !(kernel_i386_selftest_verbose()
              ? i386_scheduler_run_one(task0.entry,
                                      task0.stack_top,
                                      task0.root,
                                      "test32")
              : i386_scheduler_run_one_quiet(task0.entry,
                                             task0.stack_top,
                                             task0.root,
                                             "test32")) ||
        !i386_scheduler_process_snapshot(0u, process0)) {
        return 0;
    }
    *process1 = *process0;
    if (process0->state != PROCESS_STATE_EXITED ||
        process0->exit_code != 0) {
        early_console_write("test32: exit0=");
        early_console_write_hex32((uint32_t)process0->exit_code);
        early_console_write(" exit1=");
        early_console_write_hex32((uint32_t)process0->exit_code);
        early_console_write("\n");
        return 0;
    }
    return 1;
}

int32_t kernel_i386_spawn_command(const char *command,
                                  uint32_t mode,
                                  uint32_t flags) {
    return process32_spawn_from_user(command, mode, flags);
}

int kernel_i386_run_command(const char *command,
                            struct process_snapshot *process) {
    return process32_run_command(command, process);
}

static int i386_prepare_shared_kernel(const struct bootx_boot_info *boot_info) {
    struct kernel_early_boot_report report = {0};

    string_runtime_init();
    if (boot_info == 0 ||
        boot_info->hdr.magic != BOOTX_MAGIC ||
        !i386_segments_init() ||
        !i386_interrupts_init() ||
        !i386_paging_stage(boot_info, &report) ||
        !i386_pmm_stage(boot_info, &report) ||
        !i386_devices_init(&report) ||
        !i386_filesystem_init(&report) ||
        !i386_scheduler_test(&report)) {
        return 0;
    }
    process32_init(&early_filesystem);
    serial_write("i386: architecture bootstrap passed\n");
    return 1;
}

static int i386_identity_map_range(uint64_t address, uint64_t size) {
    uint64_t start;
    uint64_t end;

    if (address == 0u || size == 0u || address > 0xffffffffull ||
        size - 1u > 0xffffffffull - address) {
        return 0;
    }
    start = address & ~0xfffull;
    end = (address + size + 0xfffull) & ~0xfffull;
    for (uint64_t page = start; page < end; page += 0x1000u) {
        uint32_t physical = 0u;

        if (i386_paging_translate((uint32_t)page, &physical)) {
            if (physical != (uint32_t)page) {
                return 0;
            }
            continue;
        }
        if (!i386_paging_map_page((uint32_t)page, (uint32_t)page, 1, 0)) {
            return 0;
        }
    }
    return 1;
}

static int i386_map_boot_modules(const struct bootx_boot_info *boot_info) {
    const struct bootx_module *modules;

    if (boot_info == 0 || boot_info->module_count == 0u ||
        boot_info->modules == 0u) {
        return 1;
    }
    if (!i386_identity_map_range(boot_info->modules,
                                 boot_info->module_count *
                                     sizeof(struct bootx_module))) {
        return 0;
    }
    modules = (const struct bootx_module *)(uintptr_t)boot_info->modules;
    for (uint32_t i = 0u; i < boot_info->module_count; i++) {
        if (modules[i].address != 0u && modules[i].size != 0u &&
            !i386_identity_map_range(modules[i].address, modules[i].size)) {
            return 0;
        }
    }
    return 1;
}

static int i386_map_framebuffer_console(const struct bootx_console_info *console) {
    uint64_t framebuffer_size;

    if (console == 0 || console->type != BOOTX_CONSOLE_FRAMEBUFFER) {
        return 1;
    }
    framebuffer_size = (uint64_t)console->pitch * console->height;
    return i386_identity_map_range(console->framebuffer_addr, framebuffer_size);
}

void kernel_main32(const struct bootx_boot_info *boot_info) {
    struct syscall_boot_info query_boot_info;
    struct syscall_framebuffer_info query_fb_info;

    query_boot_info.boot_drive = boot_info->boot_drive;
    query_boot_info.partition_lba = boot_info->partition_lba;
    query_boot_info.partition_sectors = boot_info->partition_sectors;
    query_boot_info.module_count = boot_info->module_count;
    query_fb_info.present =
        boot_info->console.type == BOOTX_CONSOLE_FRAMEBUFFER ? 1u : 0u;
    query_fb_info.type = boot_info->console.type;
    query_fb_info.addr = boot_info->console.framebuffer_addr;
    query_fb_info.width = boot_info->console.width;
    query_fb_info.height = boot_info->console.height;
    query_fb_info.pitch = boot_info->console.pitch;
    query_fb_info.bpp = boot_info->console.framebuffer_bpp;
    query_fb_info.red_mask_size = boot_info->console.red_mask_size;
    query_fb_info.red_mask_shift = boot_info->console.red_mask_shift;
    query_fb_info.green_mask_size = boot_info->console.green_mask_size;
    query_fb_info.green_mask_shift = boot_info->console.green_mask_shift;
    query_fb_info.blue_mask_size = boot_info->console.blue_mask_size;
    query_fb_info.blue_mask_shift = boot_info->console.blue_mask_shift;
    query_fb_info.text_columns = boot_info->console.text_columns;
    query_fb_info.text_rows = boot_info->console.text_rows;
    query_fb_info.text_color = boot_info->console.text_color;
    kernel_i386_query_init(&query_boot_info,
                           &query_fb_info,
                           boot_info,
                           boot_info->cmdline,
                           boot_info->memmap,
                           boot_info->memmap_count);
    if (!i386_map_boot_modules(boot_info) ||
        !i386_map_framebuffer_console(&boot_info->console)) {
        early_console_write("kernel_main32: display module mapping failed\n");
        i386_halt();
    }
    hal_display_load_font(boot_info);
    hal_display_init(&boot_info->console);
    if (!kernel_i386_shared_services_init()) {
        early_console_write("kernel_main32: shared service initialization failed\n");
        i386_halt();
    }
    kprint("kernel: services online\n");
    kernel_i386_shared_services_run();
}

void i386_kernel_main(const struct bootx_boot_info *boot_info) {
    hal_early_bind(&i386_hal_early_ops);
    serial_init();
    if (!i386_prepare_shared_kernel(boot_info)) {
        early_console_write("i386: architecture bootstrap failed\n");
        i386_halt();
    }
    kernel_main32(boot_info);
}
