#include "kernel/internal/proc/process_lifecycle_internal.h"
#include "process32.h"
#include "scheduler_internal.h"

static struct process *i386_process_current_slot_mut(uint32_t *slot_out) {
    uint32_t slot;

    if (!scheduler_active) {
        return 0;
    }
    slot = i386_scheduler_current_slot();
    if (slot >= I386_SCHEDULER_TASKS ||
        tasks[slot].process.state == PROCESS_STATE_FREE ||
        tasks[slot].process.image_kind == PROCESS_IMAGE_NONE) {
        return 0;
    }
    if (slot_out != 0) {
        *slot_out = slot;
    }
    return &tasks[slot].process;
}

const struct process *process_current(void) {
    return i386_process_current_slot_mut(0);
}

struct process *process_current_mut(void) {
    return i386_process_current_slot_mut(0);
}

struct process_session *process_current_session(void) {
    uint32_t slot;
    struct process *process = i386_process_current_slot_mut(&slot);

    if (process == 0) {
        process_bind_session(0, 0);
        return 0;
    }
    return process32_bind_current_address_space(process,
                                                &tasks[slot].address_space,
                                                tasks[slot].mappings);
}

struct user_page_mapping *process_current_mappings(void) {
    uint32_t slot;

    if (process_current_session() == 0) {
        return 0;
    }
    slot = i386_scheduler_current_slot();
    return slot < I386_SCHEDULER_TASKS ? tasks[slot].mappings : 0;
}

uint32_t process_capacity(void) {
    return I386_SCHEDULER_TASKS;
}

int process_get(uint32_t slot, struct process_snapshot *out) {
    struct process_snapshot snapshot;

    if (slot >= I386_SCHEDULER_TASKS || out == 0 ||
        !i386_scheduler_process_snapshot(slot, &snapshot) ||
        snapshot.state == PROCESS_STATE_FREE) {
        return 0;
    }
    *out = snapshot;
    return 1;
}
