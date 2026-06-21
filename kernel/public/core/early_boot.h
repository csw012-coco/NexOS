#pragma once

#include <stdint.h>

struct bootx_boot_info;

struct kernel_early_boot_report {
    uint32_t paging_root;
    uint32_t page_fault_address;
    uint32_t page_fault_error;
    uint32_t irq_count;
    uint32_t irq_user_count;
    uint32_t pic_masks;
    uint32_t keyboard_irq_count;
    uint32_t keyboard_scancode;
    uint32_t keyboard_ascii;
    uint32_t keyboard_dropped;
    uint32_t pmm_total_pages;
    uint32_t pmm_free_pages;
    uint32_t pmm_reserved_pages;
    uint32_t pmm_first_page;
    uint32_t pmm_second_page;
    uint32_t dynamic_mapping_frame;
    uint32_t pci_device_count;
    uint32_t pci_first_vendor_device;
    uint32_t pci_storage_count;
    uint32_t block_device_count;
    uint32_t block_partition_count;
    uint32_t block_first_partition_lba;
    uint32_t ata_sector_count;
    uint32_t block_boot_signature;
    uint32_t vfs_file_size;
    uint32_t vfs_read_bytes;
    uint32_t vfs_elf_class_machine;
    uint32_t vfs_elf_entry;
    uint32_t user_entry;
    uint32_t user_stack;
    uint32_t user_syscall_number;
    uint32_t user_syscall_argument;
    uint32_t scheduler_ticks;
    uint32_t scheduler_switches;
    uint32_t scheduler_completed;
    uint32_t scheduler_task0_ticks;
    uint32_t scheduler_task1_ticks;
    uint32_t scheduler_task0_root;
    uint32_t scheduler_task1_root;
    uint32_t scheduler_task0_result;
    uint32_t scheduler_task1_result;
};

struct kernel_early_boot_ops {
    const char *architecture;
    int (*segments_init)(void);
    int (*interrupts_init)(void);
    int (*breakpoint_test)(void);
    int (*irq_test)(struct kernel_early_boot_report *report);
    int (*keyboard_test)(struct kernel_early_boot_report *report);
    int (*paging_init)(const struct bootx_boot_info *boot_info,
                       struct kernel_early_boot_report *report);
    int (*page_fault_test)(struct kernel_early_boot_report *report);
    int (*pmm_init)(const struct bootx_boot_info *boot_info,
                    struct kernel_early_boot_report *report);
    int (*dynamic_mapping_test)(struct kernel_early_boot_report *report);
    int (*devices_init)(struct kernel_early_boot_report *report);
    int (*filesystem_init)(struct kernel_early_boot_report *report);
    int (*usermode_test)(struct kernel_early_boot_report *report);
    int (*scheduler_test)(struct kernel_early_boot_report *report);
};

void kernel_early_boot(const struct bootx_boot_info *boot_info,
                       const struct kernel_early_boot_ops *ops);
