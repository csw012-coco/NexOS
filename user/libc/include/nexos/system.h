#pragma once

#include "user/public/sysapi.h"

#define NEX_QUERY_BOOT_INFO SYS_QUERY_BOOT_INFO
#define NEX_QUERY_MEMMAP SYS_QUERY_MEMMAP
#define NEX_QUERY_PMM SYS_QUERY_PMM
#define NEX_QUERY_BLOCK SYS_QUERY_BLOCK
#define NEX_QUERY_PART SYS_QUERY_PART
#define NEX_QUERY_MOUNT SYS_QUERY_MOUNT
#define NEX_QUERY_PROGRAM SYS_QUERY_PROGRAM
#define NEX_QUERY_ROOT SYS_QUERY_ROOT
#define NEX_QUERY_ROOT_FIND SYS_QUERY_ROOT_FIND
#define NEX_QUERY_FAT_ROOT SYS_QUERY_FAT_ROOT
#define NEX_QUERY_FAT_ROOT_FIND SYS_QUERY_FAT_ROOT_FIND
#define NEX_QUERY_KMSG SYS_QUERY_KMSG
#define NEX_QUERY_PCI SYS_QUERY_PCI
#define NEX_QUERY_AC97 SYS_QUERY_AC97
#define NEX_QUERY_AUDIO SYS_QUERY_AUDIO
#define NEX_QUERY_MACHINE_INFO SYS_QUERY_MACHINE_INFO
#define NEX_QUERY_RTC SYS_QUERY_RTC

int sys_query(uint32_t kind, uint64_t arg0, uint64_t arg1, void *buffer);
int kmsg_query(uint32_t offset, struct syscall_kmsg_info *info);
int pci_query(struct syscall_pci_info *info);
int machine_info_query(struct syscall_machine_info *info);
int rtc_query(struct syscall_rtc_info *info);
void clear(void);
uint32_t ticks(void);
uint64_t page_alloc(void);
int page_free(uint64_t user_page_addr);
void yield(void);
void sleep(uint32_t ticks);
int reboot(void);
__attribute__((noreturn)) void exit_with_code(uint64_t code);
__attribute__((noreturn)) void exit(void);
