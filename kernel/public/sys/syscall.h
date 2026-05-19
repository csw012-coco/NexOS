#pragma once

#include <stdint.h>
#include "bootx/bootx.h"
#include "kernel/public/sys/system_limits.h"
#include "abi/syscall_abi.h"

struct tty;
struct vfs;


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
 */


struct syscall_frame {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

void syscall_init(struct tty *tty,
                  volatile uint32_t *timer_ticks,
                  struct vfs *vfs,
                  const struct bootx_boot_info *boot_info,
                  const struct bootx_memmap_entry *memmap,
                  uint32_t memmap_count);
uint64_t syscall_dispatch(struct syscall_frame *frame);
