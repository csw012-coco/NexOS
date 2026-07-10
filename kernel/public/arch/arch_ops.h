#pragma once

#include <stdint.h>

#include "kernel/public/sys/syscall_request.h"

struct arch_ops {
    const char *name;
    uint32_t word_bits;

    void (*halt)(void);
    void (*wait_for_interrupt)(void);

    uint64_t (*current_address_space)(void);
    void (*switch_address_space)(uint64_t root);

    void (*syscall_decode)(const void *arch_frame,
                           struct kernel_syscall_request *out);
    void (*syscall_set_return)(void *arch_frame, uintptr_t value);

    int (*copy_from_user)(void *dst, uintptr_t src, uint32_t size);
    int (*copy_to_user)(uintptr_t dst, const void *src, uint32_t size);
    int (*copy_user_cstr)(char *dst, uintptr_t src, uint32_t size);
};

extern const struct arch_ops *arch;

static inline int arch_copy_from_user(void *dst, uintptr_t src, uint32_t size) {
    return arch != 0 && arch->copy_from_user != 0 &&
           arch->copy_from_user(dst, src, size);
}

static inline int arch_copy_to_user(uintptr_t dst, const void *src, uint32_t size) {
    return arch != 0 && arch->copy_to_user != 0 &&
           arch->copy_to_user(dst, src, size);
}

static inline int arch_copy_user_cstr(char *dst, uintptr_t src, uint32_t size) {
    return arch != 0 && arch->copy_user_cstr != 0 &&
           arch->copy_user_cstr(dst, src, size);
}
