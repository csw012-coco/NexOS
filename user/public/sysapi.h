#pragma once

#include <stdint.h>
#include "abi/syscall_abi.h"

enum bootx_memmap_type {
    BOOTX_MEMMAP_USABLE = 1,
    BOOTX_MEMMAP_RESERVED = 2,
    BOOTX_MEMMAP_ACPI_RECLAIMABLE = 3,
    BOOTX_MEMMAP_ACPI_NVS = 4,
    BOOTX_MEMMAP_BAD = 5,
    BOOTX_MEMMAP_BOOTLOADER_RECLAIMABLE = 0x1000
};


/*
 * SYS_QUERY(kind, arg0, arg1, buffer)
 *   BOOT_INFO:         arg0=0              arg1=0          buffer=struct syscall_boot_info*
 *   MEMMAP:            arg0=index          arg1=0          buffer=struct syscall_memmap_info*
 *   PMM:               arg0=0              arg1=0          buffer=struct syscall_pmm_info*
 *   BLOCK:             arg0=index          arg1=0          buffer=struct syscall_block_info*
 *   PART:              arg0=disk_index     arg1=slot       buffer=struct syscall_partition_info*
 *   MOUNT:             arg0=index          arg1=0          buffer=struct syscall_mount_info*
 *   PROGRAM:           arg0=index          arg1=0          buffer=struct syscall_program_info*
 *   ROOT:              arg0=index          arg1=0          buffer=struct syscall_root_entry_info*
 *   ROOT_FIND:         arg0=user_name_addr arg1=0          buffer=struct syscall_root_entry_info*
 *   FAT_ROOT:          arg0=index          arg1=0          buffer=struct syscall_fat_entry_info*
 *   FAT_ROOT_FIND:     arg0=user_name_addr arg1=0          buffer=struct syscall_fat_entry_info*
 *   KMSG:              arg0=offset         arg1=0          buffer=struct syscall_kmsg_info*
 *   PCI:               arg0=0              arg1=0          buffer=struct syscall_pci_info*
 *   AC97:              arg0=0              arg1=0          buffer=struct syscall_ac97_info*
 *   HDA:               arg0=0              arg1=0          buffer=struct syscall_hda_info*
 *   RTL8139:           arg0=0              arg1=0          buffer=struct syscall_rtl8139_info*
 *   AUDIO:             arg0=index          arg1=0          buffer=struct syscall_audio_info*
 *   MACHINE_INFO:      arg0=0              arg1=0          buffer=struct syscall_machine_info*
 *   RTC:               arg0=0              arg1=0          buffer=struct syscall_rtc_info*
 *   TTY:               arg0=fd             arg1=0          buffer=struct syscall_tty_info*
 */
