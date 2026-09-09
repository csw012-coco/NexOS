#include "arch/x86/i386/syscall/compat32_internal.h"
#include "kernel/internal/sys/syscall_common_request_core.h"
#include "kernel/public/arch/arch_ops.h"

int syscall_compat32_copy_from_user(void *dest,
                                uint64_t user_addr,
                                uint32_t size) {
    if (user_addr > 0xffffffffu) {
        return 0;
    }
    return arch_copy_from_user(dest, (uint32_t)user_addr, size);
}

int syscall_compat32_copy_to_user(uint64_t user_addr,
                              const void *src,
                              uint32_t size) {
    if (user_addr > 0xffffffffu) {
        return 0;
    }
    return arch_copy_to_user((uint32_t)user_addr, src, size);
}

int syscall_compat32_copy_user_cstr(char *dest,
                                uint64_t user_addr,
                                uint32_t size) {
    if (user_addr > 0xffffffffu) {
        return 0;
    }
    return arch_copy_user_cstr(dest, (uint32_t)user_addr, size);
}

void syscall_compat32_user_copy_ops(struct syscall_common_user_copy_ops *ops,
                                int copy_from_user,
                                int copy_to_user,
                                int copy_user_cstr,
                                uint64_t bad_pointer_value) {
    if (ops == 0) {
        return;
    }
    ops->copy_from_user = copy_from_user ? syscall_compat32_copy_from_user : 0;
    ops->copy_to_user = copy_to_user ? syscall_compat32_copy_to_user : 0;
    ops->copy_user_cstr = copy_user_cstr ? syscall_compat32_copy_user_cstr : 0;
    ops->bad_pointer = 0;
    ops->bad_pointer_value = bad_pointer_value;
}
