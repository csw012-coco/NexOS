#include "kernel/internal/proc/process_types_internal.h"
#include "kernel/public/proc/runqueue.h"

static int sched_runqueue_runnable(const struct process *proc) {
    return proc != 0 &&
           (proc->state == PROCESS_STATE_READY ||
            proc->state == PROCESS_STATE_RUNNING);
}

static uint32_t sched_runqueue_select(const struct sched_runqueue *queue) {
    if (queue == 0 || queue->slots == 0 || queue->capacity == 0u) {
        return SCHED_RUNQUEUE_NONE;
    }
    for (uint32_t offset = 1u; offset <= queue->capacity; offset++) {
        uint32_t slot = (queue->current + offset) % queue->capacity;

        if (sched_runqueue_runnable(queue->slots[slot])) {
            return slot;
        }
    }
    return SCHED_RUNQUEUE_NONE;
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
        queue->slots[slot] == 0 ||
        queue->slots[slot]->state == PROCESS_STATE_FREE) {
        return 0;
    }
    queue->active++;
    return 1;
}

int sched_runqueue_start(struct sched_runqueue *queue, uint32_t slot) {
    if (queue == 0 || queue->slots == 0 || slot >= queue->capacity ||
        !sched_runqueue_runnable(queue->slots[slot])) {
        return 0;
    }
    queue->current = slot;
    queue->slots[slot]->state = PROCESS_STATE_RUNNING;
    return 1;
}

uint32_t sched_runqueue_reschedule(struct sched_runqueue *queue,
                                   enum sched_runqueue_reason reason) {
    struct process *current;
    uint32_t next;

    if (queue == 0 || queue->slots == 0 ||
        queue->current >= queue->capacity) {
        return SCHED_RUNQUEUE_NONE;
    }
    current = queue->slots[queue->current];
    if (current == 0) {
        return SCHED_RUNQUEUE_NONE;
    }
    switch (reason) {
        case SCHED_RUNQUEUE_PREEMPT:
        case SCHED_RUNQUEUE_YIELD:
            current->state = PROCESS_STATE_READY;
            break;
        case SCHED_RUNQUEUE_BLOCK:
            current->state = PROCESS_STATE_WAITING;
            break;
        case SCHED_RUNQUEUE_EXIT:
            if (current->state != PROCESS_STATE_EXITED) {
                current->state = PROCESS_STATE_EXITED;
            }
            queue->completed++;
            break;
        default:
            return SCHED_RUNQUEUE_NONE;
    }

    next = sched_runqueue_select(queue);
    if (next == SCHED_RUNQUEUE_NONE) {
        if (reason != SCHED_RUNQUEUE_EXIT) {
            current->state = PROCESS_STATE_RUNNING;
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
