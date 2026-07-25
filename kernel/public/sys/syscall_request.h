#pragma once

#include <stdint.h>

enum syscall_result_action {
    SYSCALL_RESULT_RETURN = 0,
    SYSCALL_RESULT_YIELD = 1,
    SYSCALL_RESULT_EXIT = 2,
    SYSCALL_RESULT_EXEC = 3,
    SYSCALL_RESULT_WAIT = 4,
    SYSCALL_RESULT_SLEEP = 5
};

struct kernel_syscall_request {
    uint32_t number;
    uint8_t user_bits;
    uint8_t reserved0;
    uint16_t reserved1;
    uint64_t args[6];
    uint64_t instruction_pointer;
    uint64_t stack_pointer;
};

struct kernel_syscall_result {
    uint64_t value;
    uint64_t extra;
    enum syscall_result_action action;
};

static inline uint32_t kernel_syscall_arg_u32(const struct kernel_syscall_request *request,
                                       uint32_t index) {
    return request != 0 && index < 6u ? (uint32_t)request->args[index] : 0u;
}

static inline uint64_t kernel_syscall_arg_u64(const struct kernel_syscall_request *request,
                                      uint32_t index) {
    return request != 0 && index < 6u ? request->args[index] : 0u;
}

static inline int32_t kernel_syscall_arg_i32(const struct kernel_syscall_request *request,
                                      uint32_t index) {
    return (int32_t)kernel_syscall_arg_u32(request, index);
}

static inline int64_t kernel_syscall_arg_i64(const struct kernel_syscall_request *request,
                                      uint32_t index) {
    return (int64_t)kernel_syscall_arg_u64(request, index);
}
