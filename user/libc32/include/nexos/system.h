#pragma once

#include "user/public/sysapi.h"
#include <stdint.h>
#include <unistd.h>

#define NEX_QUERY_BOOT_INFO SYS_QUERY_BOOT_INFO
#define NEX_QUERY_MEMMAP SYS_QUERY_MEMMAP
#define NEX_QUERY_PMM SYS_QUERY_PMM
#define NEX_QUERY_VM SYS_QUERY_VM
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
#define NEX_QUERY_TTY SYS_QUERY_TTY
#define NEX_QUERY_FD SYS_QUERY_FD
#define NEX_QUERY_FB SYS_QUERY_FB
#define NEX_QUERY_PROFILE SYS_QUERY_PROFILE
#define NEX_QUERY_STABILITY SYS_QUERY_STABILITY

int sys_query(uint32_t kind, uint32_t arg0, uint32_t arg1, void *buffer);
int kmsg_query(uint32_t offset, struct syscall_kmsg_info *info);
int pci_query(struct syscall_pci_info *info);
int pci_query_at(uint32_t index, struct syscall_pci_info *info);
int machine_info_query(struct syscall_machine_info *info);
int framebuffer_query(struct syscall_framebuffer_info *info);
int rtc_query(struct syscall_rtc_info *info);
int tty_query(uint32_t fd, struct syscall_tty_info *info);
int fd_query(uint32_t fd, struct syscall_fd_info *info);
int profile_query(uint32_t index, uint32_t flags, struct syscall_profile_info *info);
int stability_query(struct syscall_stability_info *info);
uint32_t ticks(void);
void yield(void);
void sleep(uint32_t ms);
int reboot(void);
int poweroff(void);
int capability_event(const struct syscall_capability_event *event);
int capability_get(uint32_t *caps);
int capability_drop(uint32_t mask);
int capability_grant(uint32_t mask);
int capability_auth_grant(uint32_t mask, const char *token);
int capability_spawn_get(uint32_t *caps);
int capability_spawn_set(uint32_t mask);
int capability_spawn_clear(void);
int identity_get(struct syscall_identity_info *info);
int identity_drop_user(uint32_t uid);
int identity_auth_root(const char *token);
int identity_push(void);
int identity_pop(void);
void clear(void);
int clipboard_get(char *buffer, uint32_t size);
int clipboard_set(const char *text, uint32_t len);
int clipboard_clear(void);
int clipboard_size(void);
uint64_t page_alloc(void);
int page_free(uint64_t user_page_addr);
__attribute__((noreturn)) void exit_with_code(uint64_t code);
