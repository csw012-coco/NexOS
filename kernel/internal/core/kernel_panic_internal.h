#pragma once

#include <stdint.h>

struct tty;

struct exception_frame {
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
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};

void kernel_panic_handle_exception(struct tty *shell_tty,
                                   uint64_t current_user_raw_entry,
                                   uint32_t vector,
                                   const struct exception_frame *frame);
