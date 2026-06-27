#pragma once

#include <stdint.h>

enum process_context_register {
    PROCESS_CONTEXT_RETURN = 0,
    PROCESS_CONTEXT_ARG0 = 1,
    PROCESS_CONTEXT_ARG1 = 2,
    PROCESS_CONTEXT_ARG2 = 3,
    PROCESS_CONTEXT_ARG3 = 4,
    PROCESS_CONTEXT_GENERAL0 = 5,
    PROCESS_CONTEXT_GENERAL1 = 6,
    PROCESS_CONTEXT_STACK_SNAPSHOT = 7,
    PROCESS_CONTEXT_REGISTER_COUNT = 8
};

struct process_context {
    uint64_t registers[PROCESS_CONTEXT_REGISTER_COUNT];
    uint64_t instruction_pointer;
    uint64_t stack_pointer;
    uint64_t flags;
    uint16_t code_selector;
    uint16_t stack_selector;
    uint8_t user_mode;
};

void process_context_reset(struct process_context *context);
void process_context_set_return_value(struct process_context *context,
                                      uint64_t value);
uint64_t process_context_return_value(const struct process_context *context);
