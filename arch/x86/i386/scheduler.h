#pragma once

typedef unsigned int uint32_t;

struct i386_irq_frame;

enum {
    I386_SCHEDULER_EXIT_SYSCALL = 0x53434845u
};

int i386_scheduler_run(uint32_t entry0,
                       uint32_t stack0,
                       uint32_t root0,
                       uint32_t entry1,
                       uint32_t stack1,
                       uint32_t root1);
struct i386_irq_frame *i386_scheduler_tick(struct i386_irq_frame *frame);
uint32_t i386_scheduler_syscall(struct i386_irq_frame *frame);
uint32_t i386_scheduler_ticks(void);
uint32_t i386_scheduler_switches(void);
uint32_t i386_scheduler_completed(void);
uint32_t i386_scheduler_task_ticks(uint32_t task);
uint32_t i386_scheduler_task_root(uint32_t task);
uint32_t i386_scheduler_task_result(uint32_t task);
