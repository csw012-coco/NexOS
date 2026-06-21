#include "bootx.h"
#include "hal/early.h"
#include "kernel/public/core/early_boot.h"
#include "kernel/public/core/early_console.h"
#include "kernel/public/core/early_kprint.h"
#include "lib/string.h"

static void early_write_pair(const char *label, uint32_t value) {
    early_console_write(label);
    early_console_write_hex32(value);
}

static void early_fail(const char *message) {
    early_console_write(message);
    early_console_write("\n");
    hal_early_halt();
}

static int early_string_self_test(void) {
    char source[32];
    char target[32];

    string_runtime_init();
    memset(source, 0, sizeof(source));
    memset(target, 0xa5, sizeof(target));
    memcpy(source, "NexOS-i386", 11u);
    memcpy(target, source, 11u);
    memmove(target + 2, target, 9u);

    return str_len(source) == 10u &&
           streq(source, "NexOS-i386") &&
           starts_with(source, "NexOS") &&
           target[0] == 'N' &&
           target[1] == 'e' &&
           target[2] == 'N' &&
           target[10] == '8';
}

void kernel_early_boot(const struct bootx_boot_info *boot_info,
                       const struct kernel_early_boot_ops *ops) {
    struct kernel_early_boot_report report = {0};

    if (ops == 0) {
        hal_early_halt();
    }

    early_console_init();
    early_console_clear();
    early_console_write("NexOS ");
    early_console_write(ops->architecture != 0 ? ops->architecture : "unknown");
    early_console_write(" early boot\n");

    if (boot_info == 0 || boot_info->hdr.magic != BOOTX_MAGIC ||
        boot_info->hdr.version < BOOTX_PROTOCOL_VERSION) {
        early_fail("boot/x protocol invalid");
    }

    early_write_pair("boot/x OK, protocol=", boot_info->hdr.version);
    early_write_pair(" info=", (uint32_t)boot_info);
    early_console_write("\nELF protected mode entry OK\n");

    if (!early_string_self_test()) {
        early_fail("Common string runtime verification failed");
    }
    early_console_write("Common string runtime OK\n");

    if (ops->segments_init == 0 || !ops->segments_init()) {
        early_fail("GDT verification failed");
    }
    early_console_write("GDT OK\n");

    if (ops->interrupts_init == 0 || !ops->interrupts_init()) {
        early_fail("IDT verification failed");
    }
    early_console_write("IDT OK\n");

    if (ops->irq_test == 0 || !ops->irq_test(&report)) {
        early_fail("PIC/IRQ verification failed");
    }
    early_kprint("PIC remap + IRQ0 OK, ticks=%u masks=%x\n",
                 report.irq_count,
                 report.pic_masks);
    if (ops->keyboard_test == 0 || !ops->keyboard_test(&report)) {
        early_fail("PS/2 keyboard IRQ1 verification failed");
    }
    early_kprint("PS/2 keyboard IRQ1 OK, count=%u scancode=%x ascii=%x dropped=%u\n",
                 report.keyboard_irq_count,
                 report.keyboard_scancode,
                 report.keyboard_ascii,
                 report.keyboard_dropped);

    if (ops->breakpoint_test == 0 || !ops->breakpoint_test()) {
        early_fail("#BP exception verification failed");
    }
    early_console_write("#BP exception OK\n");

    if (ops->paging_init == 0 || !ops->paging_init(boot_info, &report)) {
        early_fail("Paging verification failed");
    }
    early_write_pair("Paging OK, root=", report.paging_root);
    early_console_write("\n");

    if (ops->page_fault_test == 0 || !ops->page_fault_test(&report)) {
        early_write_pair("#PF failed CR2=", report.page_fault_address);
        early_write_pair(" error=", report.page_fault_error);
        early_fail("");
    }
    early_write_pair("#PF handler/recovery OK, CR2=", report.page_fault_address);
    early_write_pair(" error=", report.page_fault_error);
    early_console_write("\n");

    if (ops->pmm_init == 0 || !ops->pmm_init(boot_info, &report)) {
        early_fail("PMM verification failed");
    }
    early_write_pair("PMM OK, total=", report.pmm_total_pages);
    early_write_pair(" free=", report.pmm_free_pages);
    early_write_pair(" reserved=", report.pmm_reserved_pages);
    early_console_write("\n");
    early_write_pair("PMM alloc/free/reuse OK, first=", report.pmm_first_page);
    early_write_pair(" second=", report.pmm_second_page);
    early_console_write("\n");

    if (ops->dynamic_mapping_test == 0 || !ops->dynamic_mapping_test(&report)) {
        early_fail("Dynamic mapping verification failed");
    }
    early_write_pair("Dynamic mapping OK, frame=", report.dynamic_mapping_frame);
    early_console_write("\n");
    early_console_write("Common console + early HAL OK\n");

    if (ops->devices_init == 0 || !ops->devices_init(&report)) {
        early_fail("PCI/block initialization failed");
    }
    early_kprint("PCI OK, devices=%u first=%x storage=%u\n",
                 report.pci_device_count,
                 report.pci_first_vendor_device,
                 report.pci_storage_count);
    early_kprint("Block layer OK, devices=%u partitions=%u\n",
                 report.block_device_count,
                 report.block_partition_count);
    early_kprint("ATA PIO OK, sectors=%u first_partition_lba=%u\n",
                 report.ata_sector_count,
                 report.block_first_partition_lba);
    early_kprint("FAT32 boot sector OK, signature=%x\n",
                 report.block_boot_signature);
    early_console_write("early_kprint formatting OK\n");

    if (ops->filesystem_init == 0 || !ops->filesystem_init(&report)) {
        early_fail("FAT32/VFS file verification failed");
    }
    early_kprint("FAT32/VFS OK, BOOT/NEX386.ELF size=%u read=%u\n",
                 report.vfs_file_size,
                 report.vfs_read_bytes);
    early_kprint("ELF32 file OK, class/machine=%x entry=%x\n",
                 report.vfs_elf_class_machine,
                 report.vfs_elf_entry);

    if (ops->usermode_test == 0 || !ops->usermode_test(&report)) {
        early_fail("ELF32 user/Ring3 verification failed");
    }
    early_kprint("ELF32 user loader OK, entry=%x stack=%x\n",
                 report.user_entry,
                 report.user_stack);
    early_kprint("Ring3 + int 0x40 OK, number=%x argument=%x\n",
                 report.user_syscall_number,
                 report.user_syscall_argument);
    early_kprint("Ring3 IRQ0 OK, user_ticks=%u total_ticks=%u\n",
                 report.irq_user_count,
                 report.irq_count);
    if (ops->scheduler_test == 0 || !ops->scheduler_test(&report)) {
        early_fail("Preemptive scheduler verification failed");
    }
    early_kprint("Preemptive RR scheduler OK, ticks=%u switches=%u completed=%u\n",
                 report.scheduler_ticks,
                 report.scheduler_switches,
                 report.scheduler_completed);
    early_kprint("Scheduler task ticks: task0=%u task1=%u\n",
                 report.scheduler_task0_ticks,
                 report.scheduler_task1_ticks);
    early_kprint("Per-task CR3 OK, task0=%x task1=%x\n",
                 report.scheduler_task0_root,
                 report.scheduler_task1_root);
    early_kprint("Address-space isolation OK, task0=%x task1=%x\n",
                 report.scheduler_task0_result,
                 report.scheduler_task1_result);
    early_console_write("Architecture early boot complete\n");
    early_console_write("Next: shared kernel services\n");
    hal_early_halt();
}
