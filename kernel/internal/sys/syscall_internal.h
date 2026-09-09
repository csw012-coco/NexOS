#pragma once

#include <stdint.h>

enum {
    SYSCALL_EXIT_TO_KERNEL = 0xfffffffffffffff0ull
};

struct bootx_boot_info;
struct vfs;

struct syscall_trace {
    uint64_t number;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t instruction_pointer;
    uint64_t stack_pointer;
    uint64_t result;
    uint32_t pid;
    uint8_t valid;
    uint8_t returned;
};

extern volatile uint32_t *g_syscall_ticks;
extern struct vfs *g_syscall_vfs;
extern const struct bootx_boot_info *g_syscall_boot_info;

extern struct syscall_trace g_last_syscall_trace;

uint64_t syscall_kill_bad_user_pointer(void);
int syscall_user_readable(uint64_t user_addr, uint32_t size);
int syscall_user_writable(uint64_t user_addr, uint32_t size);
int syscall_user_page_arg_valid(uint64_t user_addr);
int syscall_copy_from_user(void *dest, uint64_t user_addr, uint32_t size);
int syscall_copy_user_cstr(char *dest, uint64_t user_addr, uint32_t max_len);
int syscall_copy_to_user(uint64_t user_addr, const void *src, uint32_t size);
