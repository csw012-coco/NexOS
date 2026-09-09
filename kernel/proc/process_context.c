#include "kernel/public/proc/context.h"
#include "hal/hal.h"

void process_context_reset(struct process_context *context) {
    if (context == 0) {
        return;
    }
    for (uint32_t i = 0u; i < PROCESS_CONTEXT_REGISTER_COUNT; i++) {
        context->registers[i] = 0u;
    }
    context->instruction_pointer = 0u;
    context->stack_pointer = 0u;
    context->flags = 0u;
    context->code_selector = 0u;
    context->stack_selector = 0u;
    context->user_mode = 0u;
}

int process_context_init_user(struct process_context *context,
                              uint64_t entry,
                              uint64_t stack,
                              uint64_t first_argument,
                              int user_mode) {
    if (context == 0 || entry == 0 || stack == 0) {
        return 0;
    }
    return hal_process_context_init_user(context,
                                         entry,
                                         stack,
                                         first_argument,
                                         user_mode);
}

void process_context_set_return_value(struct process_context *context,
                                      uint64_t value) {
    if (context != 0) {
        context->registers[PROCESS_CONTEXT_RETURN] = value;
    }
}

uint64_t process_context_return_value(const struct process_context *context) {
    return context != 0
        ? context->registers[PROCESS_CONTEXT_RETURN]
        : 0u;
}
