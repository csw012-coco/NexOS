#include "kernel/internal/sys/syscall_internal.h"
#include "kernel/internal/core/system_query_internal.h"

static void syscall_copy_name(char *dst, uint32_t dst_size, const char *src) {
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

static int syscall_prepare_user_output(uint64_t user_info_addr, uint32_t size) {
    if (!syscall_user_writable(user_info_addr, size)) {
        return 0;
    }
    return 1;
}

static uint64_t syscall_finish_user_output(uint64_t user_info_addr, const void *info, uint32_t size) {
    if (!syscall_copy_to_user(user_info_addr, info, size)) {
        return syscall_kill_bad_user_pointer();
    }
    return 1;
}

uint64_t syscall_handle_boot_info_query(uint64_t user_info_addr) {
    struct syscall_boot_info info;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    info.boot_drive = g_syscall_boot_info != 0 ? g_syscall_boot_info->boot_drive : 0;
    info.partition_lba = g_syscall_boot_info != 0 ? g_syscall_boot_info->partition_lba : 0;
    info.partition_sectors = g_syscall_boot_info != 0 ? g_syscall_boot_info->partition_sectors : 0;
    info.module_count = g_syscall_boot_info != 0 ? g_syscall_boot_info->module_count : 0;
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_memmap_query(uint32_t index, uint64_t user_info_addr) {
    struct syscall_memmap_info info;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (g_syscall_memmap == 0 || index >= g_syscall_memmap_count) {
        return 0;
    }
    info.base = g_syscall_memmap[index].base;
    info.length = g_syscall_memmap[index].length;
    info.type = g_syscall_memmap[index].type;
    info.reserved = g_syscall_memmap[index].reserved;
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_pmm_query(uint64_t user_info_addr) {
    struct syscall_pmm_info info;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    info.total_pages = pmm_total_pages();
    info.free_pages = pmm_free_pages();
    info.used_pages = pmm_used_pages();
    info.dropped_pages = pmm_dropped_pages();
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_block_read(uint32_t disk_index, uint64_t lba, uint64_t user_info_addr) {
    struct syscall_block_read_info info;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!kernel_block_read(disk_index, lba, &info)) {
        return 0;
    }
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_block_write(uint32_t disk_index, uint64_t lba, uint64_t user_info_addr) {
    struct syscall_block_write_info info;

    if (!syscall_user_readable(user_info_addr, sizeof(info)) ||
        !syscall_user_writable(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!syscall_copy_from_user(&info, user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!kernel_block_write(disk_index, lba, &info)) {
        return 0;
    }
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_program_query(uint32_t index, uint64_t user_info_addr) {
    struct syscall_program_info info;
    const char *name;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    name = process_program_name(index);
    if (name == 0) {
        return 0;
    }
    syscall_copy_name(info.name, sizeof(info.name), name);
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_block_query(uint32_t index, uint64_t user_info_addr) {
    struct syscall_block_info info;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!kernel_query_block_info(index, &info)) {
        return 0;
    }
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_part_query(uint32_t disk_index, uint32_t slot, uint64_t user_info_addr) {
    struct syscall_partition_info info;

    if (!syscall_prepare_user_output(user_info_addr, sizeof(info))) {
        return syscall_kill_bad_user_pointer();
    }
    if (!kernel_query_part_info(disk_index, slot, &info)) {
        return 0;
    }
    return syscall_finish_user_output(user_info_addr, &info, sizeof(info));
}

uint64_t syscall_handle_query(uint32_t kind, uint64_t arg0, uint64_t arg1, uint64_t user_info_addr) {
    switch (kind) {
        case SYS_QUERY_BOOT_INFO:
            return syscall_handle_boot_info_query(user_info_addr);
        case SYS_QUERY_MEMMAP:
            return syscall_handle_memmap_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_PMM:
            return syscall_handle_pmm_query(user_info_addr);
        case SYS_QUERY_BLOCK:
            return syscall_handle_block_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_PART:
            return syscall_handle_part_query((uint32_t)arg0, (uint32_t)arg1, user_info_addr);
        case SYS_QUERY_MOUNT:
            return syscall_handle_mount_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_PROGRAM:
            return syscall_handle_program_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_ROOT:
            return syscall_handle_root_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_ROOT_FIND:
            return syscall_handle_root_find(arg0, user_info_addr);
        case SYS_QUERY_FAT_ROOT:
            return syscall_handle_fat_root_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_FAT_ROOT_FIND:
            return syscall_handle_fat_root_find(arg0, user_info_addr);
        case SYS_QUERY_KMSG:
            return syscall_handle_kmsg_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_PCI:
            return syscall_handle_pci_query(user_info_addr);
        case SYS_QUERY_AC97:
            return syscall_handle_ac97_query(user_info_addr);
        case SYS_QUERY_RTL8139:
            return syscall_handle_rtl8139_query(user_info_addr);
        case SYS_QUERY_AUDIO:
            return syscall_handle_audio_query((uint32_t)arg0, user_info_addr);
        case SYS_QUERY_MACHINE_INFO:
            return syscall_handle_machine_info_query(user_info_addr);
        case SYS_QUERY_RTC:
            return syscall_handle_rtc_query(user_info_addr);
        default:
            return 0;
    }
}
