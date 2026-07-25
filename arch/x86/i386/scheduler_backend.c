#include "context.h"
#include "paging.h"
#include "scheduler_internal.h"

void i386_scheduler_backend_save_frame(uint32_t slot) {
    if (slot >= I386_SCHEDULER_TASKS ||
        tasks[slot].process.state == PROCESS_STATE_FREE) {
        return;
    }
    i386_scheduler_fpu_save(&tasks[slot].fpu_state);
    tasks[slot].fpu_valid = 1u;
}

void i386_scheduler_backend_restore_frame(uint32_t slot) {
    if (slot >= I386_SCHEDULER_TASKS) {
        i386_scheduler_fpu_restore(0, 0u);
        return;
    }
    i386_scheduler_fpu_restore(&tasks[slot].fpu_state,
                               tasks[slot].fpu_valid);
}

void i386_scheduler_backend_switch_task(uint32_t previous, uint32_t next) {
    if (next >= I386_SCHEDULER_TASKS) {
        return;
    }
    if (previous != next && previous < I386_SCHEDULER_TASKS) {
        i386_scheduler_backend_save_frame(previous);
    }
    i386_scheduler_backend_restore_frame(next);
    i386_paging_switch(tasks[next].root);
}

void i386_scheduler_backend_switch_to_task(uint32_t slot) {
    if (slot >= I386_SCHEDULER_TASKS) {
        return;
    }
    i386_scheduler_backend_restore_frame(slot);
    i386_paging_switch(tasks[slot].root);
}

void i386_scheduler_backend_switch_to_kernel(void) {
    i386_paging_switch(scheduler_kernel_root);
}

int i386_scheduler_backend_enter_task(uint32_t slot) {
    if (slot >= I386_SCHEDULER_TASKS) {
        return 0;
    }
    i386_scheduler_backend_switch_to_task(slot);
    return i386_context_enter(&tasks[slot].process.context);
}

void i386_scheduler_backend_init_user_context(struct scheduler_task *task,
                                              uint32_t entry,
                                              uint32_t stack,
                                              uint32_t id) {
    if (task == 0) {
        return;
    }
    i386_context_init_user(&task->process.context, entry, stack, id);
}

uint32_t i386_scheduler_backend_task_result(
    const struct process_context *context) {
    return i386_context_task_result(context);
}
