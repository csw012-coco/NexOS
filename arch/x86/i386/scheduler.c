#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "scheduler.h"

enum {
    SCHEDULER_TASKS = 2,
    TASK_RUNNABLE = 1,
    TASK_DONE = 2
};

struct scheduler_task {
    struct i386_irq_frame context;
    uint32_t state;
    uint32_t ticks;
    uint32_t root;
    uint32_t result;
};

static struct scheduler_task tasks[SCHEDULER_TASKS];
static volatile uint32_t scheduler_active;
static uint32_t current_task;
static uint32_t scheduler_tick_count;
static uint32_t scheduler_switch_count;
static uint32_t scheduler_completed_count;
static uint32_t scheduler_kernel_root;

extern int i386_usermode_enter_context(struct i386_irq_frame *frame);

static void clear_context(struct i386_irq_frame *frame) {
    uint32_t *words = (uint32_t *)frame;

    for (uint32_t i = 0; i < sizeof(*frame) / sizeof(uint32_t); i++) {
        words[i] = 0;
    }
}

static void task_init(struct scheduler_task *task,
                      uint32_t entry,
                      uint32_t stack,
                      uint32_t id,
                      uint32_t root) {
    clear_context(&task->context);
    task->context.ebx = id;
    task->context.eip = entry;
    task->context.cs = I386_GDT_USER_CODE;
    task->context.eflags = 0x202u;
    task->context.user_esp = stack;
    task->context.user_ss = I386_GDT_USER_DATA;
    task->state = TASK_RUNNABLE;
    task->ticks = 0;
    task->root = root;
    task->result = 0;
}

static uint32_t next_runnable(uint32_t current) {
    for (uint32_t offset = 1; offset <= SCHEDULER_TASKS; offset++) {
        uint32_t candidate = (current + offset) % SCHEDULER_TASKS;

        if (tasks[candidate].state == TASK_RUNNABLE) {
            return candidate;
        }
    }
    return current;
}

int i386_scheduler_run(uint32_t entry0,
                       uint32_t stack0,
                       uint32_t root0,
                       uint32_t entry1,
                       uint32_t stack1,
                       uint32_t root1) {
    if (root0 == 0u || root1 == 0u || root0 == root1) {
        return 0;
    }
    task_init(&tasks[0], entry0, stack0, 1u, root0);
    task_init(&tasks[1], entry1, stack1, 2u, root1);
    current_task = 0;
    scheduler_tick_count = 0;
    scheduler_switch_count = 0;
    scheduler_completed_count = 0;
    scheduler_kernel_root = i386_paging_kernel_root();
    scheduler_active = 1;

    i386_paging_switch(tasks[0].root);
    if (!i386_usermode_enter_context(&tasks[0].context)) {
        i386_paging_switch(scheduler_kernel_root);
        scheduler_active = 0;
        return 0;
    }
    return !scheduler_active && scheduler_completed_count == SCHEDULER_TASKS;
}

struct i386_irq_frame *i386_scheduler_tick(struct i386_irq_frame *frame) {
    uint32_t next;

    if (!scheduler_active || frame == 0 || (frame->cs & 3u) != 3u) {
        return frame;
    }

    tasks[current_task].context = *frame;
    tasks[current_task].ticks++;
    scheduler_tick_count++;
    next = next_runnable(current_task);
    if (next != current_task) {
        current_task = next;
        scheduler_switch_count++;
        i386_paging_switch(tasks[current_task].root);
    }
    return &tasks[current_task].context;
}

uint32_t i386_scheduler_syscall(struct i386_irq_frame *frame) {
    uint32_t next;

    if (!scheduler_active ||
        frame == 0 ||
        frame->eax != I386_SCHEDULER_EXIT_SYSCALL ||
        frame->ebx != current_task + 1u) {
        return 0;
    }

    tasks[current_task].context = *frame;
    tasks[current_task].result = frame->edx;
    tasks[current_task].state = TASK_DONE;
    scheduler_completed_count++;
    if (scheduler_completed_count == SCHEDULER_TASKS) {
        scheduler_active = 0;
        i386_paging_switch(scheduler_kernel_root);
        return 1;
    }

    next = next_runnable(current_task);
    if (next == current_task) {
        scheduler_active = 0;
        i386_paging_switch(scheduler_kernel_root);
        return 1;
    }
    current_task = next;
    scheduler_switch_count++;
    i386_paging_switch(tasks[current_task].root);
    return (uint32_t)&tasks[current_task].context;
}

uint32_t i386_scheduler_ticks(void) {
    return scheduler_tick_count;
}

uint32_t i386_scheduler_switches(void) {
    return scheduler_switch_count;
}

uint32_t i386_scheduler_completed(void) {
    return scheduler_completed_count;
}

uint32_t i386_scheduler_task_ticks(uint32_t task) {
    if (task >= SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].ticks;
}

uint32_t i386_scheduler_task_root(uint32_t task) {
    if (task >= SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].root;
}

uint32_t i386_scheduler_task_result(uint32_t task) {
    if (task >= SCHEDULER_TASKS) {
        return 0;
    }
    return tasks[task].result;
}
