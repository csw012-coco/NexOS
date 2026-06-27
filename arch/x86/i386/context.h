#pragma once

#include <stdint.h>

struct i386_irq_frame;
struct i386_syscall_frame;
struct process_context;

void i386_context_init_user(struct process_context *context,
                            uint32_t entry,
                            uint32_t stack,
                            uint32_t first_argument);
void i386_context_from_irq(struct process_context *context,
                           const struct i386_irq_frame *frame);
void i386_context_from_syscall(struct process_context *context,
                               const struct i386_syscall_frame *frame);
struct i386_irq_frame *i386_context_to_irq(
    const struct process_context *context);
uint32_t i386_context_task_result(const struct process_context *context);
int i386_context_enter(const struct process_context *context);
