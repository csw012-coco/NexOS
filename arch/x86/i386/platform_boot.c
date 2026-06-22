typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#include "bootx.h"
#include "block/blockdev.h"
#include "drivers/bus/pci.h"
#include "drivers/storage/ata.h"
#include "fs/early_vfs.h"
#include "gdt.h"
#include "hal/early.h"
#include "idt.h"
#include "kernel/public/core/early_boot.h"
#include "kernel/public/core/early_console.h"
#include "kernel/public/proc/process.h"
#include "kernel/public/sys/syscall_dispatch.h"
#include "keyboard.h"
#include "lib/string.h"
#include "paging.h"
#include "pic.h"
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
static int i386_load_command(const char *command,
                             struct i386_user_image *image,
                             char *name_out,
                             uint32_t name_size);

extern uint32_t kernel_i386_syscall_write(void *opaque,
                                          uint32_t fd,
                                          uint32_t user_address,
                                          uint32_t size);
extern uint32_t kernel_i386_syscall_open(void *opaque,
                                         uint32_t user_path,
                                         uint32_t flags);
extern uint32_t kernel_i386_syscall_read(void *opaque,
                                         uint32_t fd,
                                         uint32_t user_address,
                                         uint32_t size,
                                         uint32_t flags);
extern uint32_t kernel_i386_syscall_close(void *opaque, uint32_t fd);
extern uint32_t kernel_i386_syscall_page_alloc(void *opaque);
extern uint32_t kernel_i386_syscall_page_free(void *opaque,
                                              uint32_t user_page);
extern uint32_t kernel_i386_syscall_spawn(void *opaque,
                                          uint32_t user_command,
                                          uint32_t mode,
                                          uint32_t flags);
extern uint32_t kernel_i386_syscall_exec(void *opaque,
                                         uint32_t user_command);
extern uint32_t kernel_i386_syscall_chdir(void *opaque,
                                          uint32_t user_path);
extern uint32_t kernel_i386_syscall_getcwd(void *opaque,
                                           uint32_t user_buffer,
                                           uint32_t size);
extern uint32_t kernel_i386_syscall_opendir(void *opaque,
                                            uint32_t user_path);
extern uint32_t kernel_i386_syscall_readdir(void *opaque,
                                            uint32_t fd,
                                            uint32_t user_entry);
extern uint32_t kernel_i386_syscall_dup2(void *opaque,
                                         uint32_t src_fd,
                                         uint32_t dst_fd);
extern uint32_t kernel_i386_syscall_pipe(void *opaque,
                                         uint32_t user_pair);
extern int kernel_i386_copy_user_string(uint32_t user_address,
                                        char *buffer,
                                        uint32_t size);

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

void i386_exception_handler(uint32_t vector, struct i386_exception_frame *frame) {
    if (vector == I386_IDT_VECTOR_BREAKPOINT) {
        breakpoint_count++;
        return;
    }

    if (vector == I386_IDT_VECTOR_PAGE_FAULT) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(page_fault_cr2));
        page_fault_error = frame->error_code;
        page_fault_count++;

        if (page_fault_expected &&
            frame->eip == (uint32_t)i386_page_fault_test_fault) {
            frame->eip = (uint32_t)i386_page_fault_test_recovery;
            return;
        }
    }

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

uint32_t i386_syscall_handler(struct i386_syscall_frame *frame) {
    struct syscall_register_request request = {
        .number = frame->eax,
        .arg0 = frame->ebx,
        .arg1 = frame->ecx,
        .arg2 = frame->edx,
        .arg3 = frame->esi,
        .instruction_pointer = frame->eip,
        .stack_pointer = frame->user_esp
    };
    struct syscall_common_context context = {
        .pid = i386_scheduler_current_pid(),
        .ticks = irq_line_count[0],
        .opaque = 0,
        .open = kernel_i386_syscall_open,
        .read = kernel_i386_syscall_read,
        .write = kernel_i386_syscall_write,
        .close = kernel_i386_syscall_close,
        .page_alloc = kernel_i386_syscall_page_alloc,
        .page_free = kernel_i386_syscall_page_free,
        .spawn = kernel_i386_syscall_spawn,
        .exec = kernel_i386_syscall_exec,
        .chdir = kernel_i386_syscall_chdir,
        .getcwd = kernel_i386_syscall_getcwd,
        .opendir = kernel_i386_syscall_opendir,
        .readdir = kernel_i386_syscall_readdir,
        .dup2 = kernel_i386_syscall_dup2,
        .pipe = kernel_i386_syscall_pipe
    };
    struct syscall_common_result result;

    if (request.number == SYS_EXEC ||
        request.number == SYS_EXEC_REPLACE) {
        char command[512];
        struct i386_user_image image;
        char name[NOS_NAME_BUFFER_SIZE];
        uint32_t current_root = i386_paging_root();
        uint32_t action;

        user_syscall_count++;
        user_syscall_number = request.number;
        user_syscall_argument = request.arg0;
        if (!kernel_i386_copy_user_string(request.arg0,
                                          command,
                                          sizeof(command))) {
            frame->eax = (uint32_t)-1;
            return 0u;
        }
        i386_paging_switch(i386_paging_kernel_root());
        if (!i386_load_command(command, &image, name, sizeof(name))) {
            i386_paging_switch(current_root);
            frame->eax = (uint32_t)-1;
            return 0u;
        }
        i386_paging_switch(current_root);
        action = i386_scheduler_exec((struct i386_irq_frame *)frame,
                                     image.entry,
                                     image.stack_top,
                                     image.root,
                                     name);
        return action != 0u ? action : 0u;
    }
    if (request.number == SYS_WAIT) {
        int32_t status = -1;
        int blocked = 0;
        uint32_t action = i386_scheduler_wait(
            (struct i386_irq_frame *)frame,
            request.arg0,
            &status,
            &blocked);

        user_syscall_count++;
        user_syscall_number = request.number;
        user_syscall_argument = request.arg0;
        if (blocked && action != 0u) {
            return action;
        }
        frame->eax = (uint32_t)status;
        return 0u;
    }
    result = syscall_dispatch_common(&request, &context);

    user_syscall_count++;
    user_syscall_number = request.number;
    user_syscall_argument = request.arg0;
    frame->eax = result.value;

    if (result.action == SYSCALL_COMMON_EXIT) {
        uint32_t action =
            i386_scheduler_exit((struct i386_irq_frame *)frame,
                                (int)result.value);

        return action != 0u ? action : 1u;
    }
    if (result.action == SYSCALL_COMMON_YIELD) {
        uint32_t action =
            i386_scheduler_yield((struct i386_irq_frame *)frame);

        if (action != 0u) {
            return action;
        }
    }
    frame->eax = result.value;
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
    if (irq == 1u) {
        i386_keyboard_handle_irq();
    }
    i386_pic_send_eoi((uint8_t)irq);
    if (irq == 0u) {
        return i386_scheduler_tick(frame);
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

int kernel_i386_run_test32(struct process_snapshot *process0,
                           struct process_snapshot *process1) {
    enum {
        TEST_STACK_TOP = 0xbfffe000u
    };
    struct i386_user_image task0;
    struct i386_user_image task1;

    if (!i386_user_load_elf_space(&early_filesystem,
                                  "/BOOT/TEST32.ELF",
                                  TEST_STACK_TOP,
                                  &task0) ||
        !i386_user_load_elf_space(&early_filesystem,
                                  "/BOOT/TEST32.ELF",
                                  TEST_STACK_TOP,
                                  &task1) ||
        !i386_scheduler_run(task0.entry,
                            task0.stack_top,
                            task0.root,
                            task1.entry,
                            task1.stack_top,
                            task1.root) ||
        !i386_scheduler_process_snapshot(0u, process0) ||
        !i386_scheduler_process_snapshot(1u, process1)) {
        return 0;
    }
    return process0->state == PROCESS_STATE_EXITED &&
           process1->state == PROCESS_STATE_EXITED &&
           process0->exit_code == 0 &&
           process1->exit_code == 0;
}

static int i386_parse_command(char *command,
                              const char *argv[],
                              int *argc_out) {
    int argc = 0;
    char *cursor = command;

    while (*cursor != '\0' && argc < I386_USER_ARG_MAX) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        argv[argc++] = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }
    *argc_out = argc;
    return argc != 0;
}

static int i386_load_command(const char *command,
                             struct i386_user_image *image,
                             char *name_out,
                             uint32_t name_size) {
    char copy[512];
    const char *argv[I386_USER_ARG_MAX + 1];
    static const char *const envp[] = {
        "ARCH=i386",
        "OS=NexOS",
        0
    };
    int argc;
    uint32_t length = 0u;

    if (command == 0 || image == 0) {
        return 0;
    }
    while (command[length] != '\0' && length + 1u < sizeof(copy)) {
        copy[length] = command[length];
        length++;
    }
    copy[length] = '\0';
    if (!i386_parse_command(copy, argv, &argc)) {
        return 0;
    }
    argv[argc] = 0;
    if (name_out != 0 && name_size != 0u) {
        uint32_t i = 0u;

        while (argv[0][i] != '\0' && i + 1u < name_size) {
            name_out[i] = argv[0][i];
            i++;
        }
        name_out[i] = '\0';
    }
    return i386_user_load_elf_space_args(&early_filesystem,
                                          argv[0],
                                          0xbfffe000u,
                                          argc,
                                          argv,
                                          envp,
                                          image);
}

int32_t kernel_i386_spawn_command(const char *command) {
    struct i386_user_image image;
    char name[NOS_NAME_BUFFER_SIZE];
    uint32_t current_root = i386_paging_root();

    i386_paging_switch(i386_paging_kernel_root());
    if (!i386_load_command(command, &image, name, sizeof(name))) {
        i386_paging_switch(current_root);
        return -1;
    }
    i386_paging_switch(current_root);
    return i386_scheduler_spawn(image.entry,
                                image.stack_top,
                                image.root,
                                name);
}

int kernel_i386_run_command(const char *command,
                            struct process_snapshot *process) {
    struct i386_user_image image;
    char name[NOS_NAME_BUFFER_SIZE];

    if (!i386_load_command(command, &image, name, sizeof(name)) ||
        !i386_scheduler_run_one(image.entry,
                                image.stack_top,
                                image.root,
                                name) ||
        !i386_scheduler_process_snapshot(0u, process)) {
        return 0;
    }
    return process->state == PROCESS_STATE_EXITED;
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
    return 1;
}

void kernel_main32(const struct bootx_boot_info *boot_info) {
    (void)boot_info;
    if (!kernel_i386_shared_services_init()) {
        early_console_write("kernel_main32: shared service initialization failed\n");
        i386_halt();
    }
    serial_write("kernel_main32: common VFS/TTY/HAL services online\n");
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
