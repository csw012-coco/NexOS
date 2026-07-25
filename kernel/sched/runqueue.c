#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/proc/runqueue.h"
#include "kernel/public/proc/sched_policy.h"

static int sched_runqueue_entry_valid(const struct process *proc) {
    return proc != 0 &&
           proc->state != PROCESS_STATE_FREE;
}

void sched_runqueue_init(struct sched_runqueue *queue,
                         struct process **slots,
                         uint32_t capacity) {
    if (queue == 0) {
        return;
    }
    queue->slots = slots;
    queue->capacity = capacity;
    sched_runqueue_reset(queue);
}

void sched_runqueue_reset(struct sched_runqueue *queue) {
    if (queue == 0) {
        return;
    }
    queue->current = 0u;
    queue->active = 0u;
    queue->completed = 0u;
    queue->switches = 0u;
}

int sched_runqueue_activate(struct sched_runqueue *queue, uint32_t slot) {
    if (queue == 0 || queue->slots == 0 || slot >= queue->capacity ||
        !sched_runqueue_entry_valid(queue->slots[slot])) {
        return 0;
    }
    queue->active++;
    return 1;
}

int sched_runqueue_start(struct sched_runqueue *queue, uint32_t slot) {
    if (queue == 0 || queue->slots == 0 || slot >= queue->capacity ||
        !sched_runqueue_entry_valid(queue->slots[slot]) ||
        (queue->slots[slot]->state != PROCESS_STATE_READY &&
         queue->slots[slot]->state != PROCESS_STATE_RUNNING)) {
        return 0;
    }
    queue->current = slot;
    queue->slots[slot]->state = PROCESS_STATE_RUNNING;
    return 1;
}

uint32_t sched_runqueue_reschedule(struct sched_runqueue *queue,
                                   enum sched_runqueue_reason reason) {
    struct process *current;
    int completed = 0;
    uint32_t next;

    if (queue == 0 || queue->slots == 0 ||
        queue->current >= queue->capacity) {
        return SCHED_RUNQUEUE_NONE;
    }
    current = queue->slots[queue->current];
    if (current == 0) {
        return SCHED_RUNQUEUE_NONE;
    }
    if (!sched_policy_apply_runqueue_transition(current,
                                                (uint32_t)reason,
                                                &completed)) {
        return SCHED_RUNQUEUE_NONE;
    }
    if (completed) {
        queue->completed++;
    }

    next = sched_policy_select_next_in_slots(queue->slots,
                                             queue->capacity,
                                             queue->current);
    if (next == SCHED_RUNQUEUE_NONE) {
        if (sched_policy_restore_runqueue_current(current,
                                                  (uint32_t)reason)) {
            return queue->current;
        }
        return SCHED_RUNQUEUE_NONE;
    }
    if (next != queue->current) {
        queue->current = next;
        queue->switches++;
    }
    queue->slots[queue->current]->state = PROCESS_STATE_RUNNING;
    return queue->current;
}

uint32_t sched_runqueue_tick(struct sched_runqueue *queue,
                             uint32_t current_ticks) {
    return sched_runqueue_step(queue,
                               SCHED_RUNQUEUE_PREEMPT,
                               current_ticks,
                               0u);
}

uint32_t sched_runqueue_yield(struct sched_runqueue *queue) {
    return sched_runqueue_step(queue, SCHED_RUNQUEUE_YIELD, 0u, 0u);
}

uint32_t sched_runqueue_block(struct sched_runqueue *queue) {
    return sched_runqueue_step(queue, SCHED_RUNQUEUE_BLOCK, 0u, 0u);
}

uint32_t sched_runqueue_sleep_until(struct sched_runqueue *queue,
                                    uint32_t wake_tick) {
    return sched_runqueue_step(queue,
                               SCHED_RUNQUEUE_SLEEP,
                               0u,
                               wake_tick);
}

uint32_t sched_runqueue_exit(struct sched_runqueue *queue) {
    return sched_runqueue_step(queue, SCHED_RUNQUEUE_EXIT, 0u, 0u);
}

uint32_t sched_runqueue_step(struct sched_runqueue *queue,
                             enum sched_runqueue_reason reason,
                             uint32_t current_ticks,
                             uint32_t sleep_until) {
    uint32_t current_slot;
    uint32_t next;

    if (reason == SCHED_RUNQUEUE_PREEMPT) {
        if (queue == 0) {
            return SCHED_RUNQUEUE_NONE;
        }
        (void)sched_policy_wake_sleepers_in_slots(queue->slots,
                                                  queue->capacity,
                                                  current_ticks);
        return sched_runqueue_reschedule(queue, reason);
    }
    if (reason != SCHED_RUNQUEUE_SLEEP) {
        return sched_runqueue_reschedule(queue, reason);
    }
    if (queue == 0 || queue->slots == 0 ||
        queue->current >= queue->capacity) {
        return SCHED_RUNQUEUE_NONE;
    }
    current_slot = queue->current;
    if (queue->slots[current_slot] == 0) {
        return SCHED_RUNQUEUE_NONE;
    }
    queue->slots[current_slot]->wake_tick = sleep_until;
    if (queue->slots[current_slot]->wake_tick == 0u) {
        queue->slots[current_slot]->wake_tick = 1u;
    }
    sched_policy_note_sleep(queue->slots[current_slot]->wake_tick);
    next = sched_runqueue_reschedule(queue, SCHED_RUNQUEUE_SLEEP);
    if (next == SCHED_RUNQUEUE_NONE || next == current_slot) {
        queue->slots[current_slot]->wake_tick = 0u;
        queue->slots[current_slot]->state = PROCESS_STATE_RUNNING;
        return SCHED_RUNQUEUE_NONE;
    }
    return next;
}

struct process *sched_runqueue_current(struct sched_runqueue *queue) {
    if (queue == 0 || queue->slots == 0 ||
        queue->current >= queue->capacity) {
        return 0;
    }
    return queue->slots[queue->current];
}

const struct process *sched_runqueue_process(const struct sched_runqueue *queue,
                                             uint32_t slot) {
    return queue != 0 && queue->slots != 0 && slot < queue->capacity
        ? queue->slots[slot]
        : 0;
}

uint32_t sched_runqueue_current_slot(const struct sched_runqueue *queue) {
    return queue != 0 ? queue->current : SCHED_RUNQUEUE_NONE;
}

uint32_t sched_runqueue_active_count(const struct sched_runqueue *queue) {
    return queue != 0 ? queue->active : 0u;
}

uint32_t sched_runqueue_completed_count(const struct sched_runqueue *queue) {
    return queue != 0 ? queue->completed : 0u;
}

uint32_t sched_runqueue_switch_count(const struct sched_runqueue *queue) {
    return queue != 0 ? queue->switches : 0u;
}

void sched_runqueue_mark_completed(struct sched_runqueue *queue) {
    if (queue != 0) {
        queue->completed++;
    }
}

int sched_runqueue_collect_policy_stats(const struct sched_runqueue *queue,
                                        struct sched_policy_stats *out) {
    if (queue == 0) {
        return 0;
    }
    return sched_policy_collect_stats_from_slots(queue->slots,
                                                 queue->capacity,
                                                 queue->current,
                                                 out);
}
