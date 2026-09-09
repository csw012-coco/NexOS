#include "kernel/public/proc/sched_policy.h"
#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/proc/runqueue.h"

static enum sched_mode g_sched_mode = SCHED_MODE_INTERACTIVE;
static uint32_t g_sched_next_wake_tick;
static struct process **g_sched_slots;
static uint32_t g_sched_slot_capacity;
static const uint32_t *g_sched_current_slot;

static int sched_policy_tick_before(uint32_t lhs, uint32_t rhs) {
    return (int32_t)(lhs - rhs) < 0;
}

static int sched_policy_process_runnable(const struct process *process) {
    return process != 0 &&
           (process->state == PROCESS_STATE_READY ||
            process->state == PROCESS_STATE_RUNNING);
}

static int sched_policy_process_present(const struct process *process) {
    return process != 0 &&
           process->state != PROCESS_STATE_FREE &&
           process->image_kind != PROCESS_IMAGE_NONE;
}

void sched_policy_init(void) {
    g_sched_mode = SCHED_MODE_INTERACTIVE;
    g_sched_next_wake_tick = 0u;
    g_sched_slots = 0;
    g_sched_slot_capacity = 0u;
    g_sched_current_slot = 0;
}

void sched_policy_note_sleep(uint32_t wake_tick) {
    if (wake_tick == 0u) {
        return;
    }
    if (g_sched_next_wake_tick == 0u ||
        sched_policy_tick_before(wake_tick, g_sched_next_wake_tick)) {
        g_sched_next_wake_tick = wake_tick;
    }
}

uint32_t sched_policy_update_wake_times(uint32_t current_ticks) {
    if (g_sched_next_wake_tick == 0u ||
        sched_policy_tick_before(current_ticks, g_sched_next_wake_tick)) {
        return 0u;
    }
    g_sched_next_wake_tick = 0u;
    return 0u;
}

uint32_t sched_policy_wake_sleepers_in_slots(struct process **slots,
                                             uint32_t capacity,
                                             uint32_t current_ticks) {
    uint32_t woke_up_count = 0u;
    uint32_t next_wake_tick = 0u;

    if (slots == 0) {
        return 0u;
    }
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        struct process *process = slots[slot];
        uint32_t wake_tick;

        if (process == 0 ||
            process->state != PROCESS_STATE_SLEEPING ||
            process->wake_tick == 0u) {
            continue;
        }
        wake_tick = process->wake_tick;
        if (!sched_policy_tick_before(current_ticks, wake_tick)) {
            process->state = PROCESS_STATE_READY;
            process->wake_tick = 0u;
            woke_up_count++;
        } else if (next_wake_tick == 0u ||
                   sched_policy_tick_before(wake_tick, next_wake_tick)) {
            next_wake_tick = wake_tick;
        }
    }
    g_sched_next_wake_tick = next_wake_tick;
    return woke_up_count;
}

uint32_t sched_policy_select_next_in_slots(struct process **slots,
                                           uint32_t capacity,
                                           uint32_t current_slot) {
    if (slots == 0 || capacity == 0u) {
        return 0xffffffffu;
    }
    if (current_slot >= capacity) {
        current_slot = 0u;
    }
    for (uint32_t offset = 1u; offset <= capacity; offset++) {
        uint32_t slot = (current_slot + offset) % capacity;

        if (sched_policy_process_runnable(slots[slot])) {
            return slot;
        }
    }
    return 0xffffffffu;
}

void sched_policy_bind_slot_view(struct process **slots,
                                 uint32_t capacity,
                                 const uint32_t *current_slot) {
    g_sched_slots = slots;
    g_sched_slot_capacity = capacity;
    g_sched_current_slot = current_slot;
}

int sched_policy_apply_runqueue_transition(struct process *process,
                                           uint32_t reason,
                                           int *completed) {
    if (completed != 0) {
        *completed = 0;
    }
    if (process == 0) {
        return 0;
    }
    switch (reason) {
        case SCHED_RUNQUEUE_PREEMPT:
        case SCHED_RUNQUEUE_YIELD:
            process->state = PROCESS_STATE_READY;
            return 1;
        case SCHED_RUNQUEUE_BLOCK:
            process->state = PROCESS_STATE_WAITING;
            return 1;
        case SCHED_RUNQUEUE_SLEEP:
            process->state = PROCESS_STATE_SLEEPING;
            return 1;
        case SCHED_RUNQUEUE_EXIT:
            if (process->state != PROCESS_STATE_EXITED) {
                process->state = PROCESS_STATE_EXITED;
            }
            if (completed != 0) {
                *completed = 1;
            }
            return 1;
        default:
            return 0;
    }
}

int sched_policy_restore_runqueue_current(struct process *process,
                                          uint32_t reason) {
    if (process == 0 || reason == SCHED_RUNQUEUE_EXIT) {
        return 0;
    }
    process->state = PROCESS_STATE_RUNNING;
    return 1;
}

int sched_policy_collect_stats_from_slots(struct process **slots,
                                          uint32_t capacity,
                                          uint32_t current_slot,
                                          struct sched_policy_stats *out) {
    uint32_t total = 0u;
    uint32_t ready = 0u;
    uint32_t sleeping = 0u;

    if (out == 0) {
        return 0;
    }
    out->total_processes = 0u;
    out->ready_processes = 0u;
    out->sleeping_processes = 0u;
    out->current_slot = current_slot;
    if (slots == 0) {
        return 0;
    }
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        const struct process *process = slots[slot];

        if (!sched_policy_process_present(process)) {
            continue;
        }
        total++;
        if (sched_policy_process_runnable(process)) {
            ready++;
        } else if (process->state == PROCESS_STATE_SLEEPING) {
            sleeping++;
        }
    }
    out->total_processes = total;
    out->ready_processes = ready;
    out->sleeping_processes = sleeping;
    return 1;
}

int32_t sched_policy_select_next(void) {
    return -2;
}

void sched_policy_set_excluded_pid(uint32_t pid) {
    (void)pid;
}

void sched_policy_on_process_finished(uint32_t slot) {
    (void)slot;
}

int sched_policy_get_stats(struct sched_policy_stats *out) {
    uint32_t current_slot = 0u;

    if (out == 0) {
        return 0;
    }
    if (g_sched_current_slot != 0) {
        current_slot = *g_sched_current_slot;
    }
    return sched_policy_collect_stats_from_slots(g_sched_slots,
                                                 g_sched_slot_capacity,
                                                 current_slot,
                                                 out);
}

void sched_policy_set_mode(enum sched_mode mode) {
    if (mode < 2) {
        g_sched_mode = mode;
    }
}
