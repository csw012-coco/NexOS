#include "scheduler_internal.h"

struct scheduler_task tasks[I386_SCHEDULER_TASKS];
struct process *task_processes[I386_SCHEDULER_TASKS];
struct sched_runqueue runqueue;
volatile uint32_t scheduler_active;
uint32_t scheduler_tick_count;
uint32_t scheduler_kernel_root;
uint32_t scheduler_next_pid = 1u;
uint32_t scheduler_quiet_tty_output;

uint32_t i386_scheduler_current_slot(void) {
    return sched_runqueue_current_slot(&runqueue);
}

struct scheduler_task *i386_scheduler_task_by_pid(uint32_t pid) {
    if (pid == 0u) {
        return 0;
    }
    for (uint32_t slot = 0u; slot < I386_SCHEDULER_TASKS; slot++) {
        if (tasks[slot].process.pid == pid &&
            tasks[slot].process.state != PROCESS_STATE_FREE) {
            return &tasks[slot];
        }
    }
    return 0;
}

int i386_scheduler_is_active(void) {
    return scheduler_active != 0u;
}

struct scheduler_task *i386_scheduler_current_task_mut(void) {
    uint32_t slot;

    if (!scheduler_active) {
        return 0;
    }
    slot = i386_scheduler_current_slot();
    return slot < I386_SCHEDULER_TASKS ? &tasks[slot] : 0;
}
