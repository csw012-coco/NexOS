#pragma once

#include <stdint.h>

struct process;

enum sched_runqueue_reason {
    SCHED_RUNQUEUE_PREEMPT = 0,
    SCHED_RUNQUEUE_YIELD = 1,
    SCHED_RUNQUEUE_BLOCK = 2,
    SCHED_RUNQUEUE_EXIT = 3
};

enum {
    SCHED_RUNQUEUE_NONE = 0xffffffffu
};

struct sched_runqueue {
    struct process **slots;
    uint32_t capacity;
    uint32_t current;
    uint32_t active;
    uint32_t completed;
    uint32_t switches;
};

void sched_runqueue_init(struct sched_runqueue *queue,
                         struct process **slots,
                         uint32_t capacity);
void sched_runqueue_reset(struct sched_runqueue *queue);
int sched_runqueue_activate(struct sched_runqueue *queue, uint32_t slot);
int sched_runqueue_start(struct sched_runqueue *queue, uint32_t slot);
uint32_t sched_runqueue_reschedule(struct sched_runqueue *queue,
                                   enum sched_runqueue_reason reason);
struct process *sched_runqueue_current(struct sched_runqueue *queue);
const struct process *sched_runqueue_process(const struct sched_runqueue *queue,
                                             uint32_t slot);
uint32_t sched_runqueue_current_slot(const struct sched_runqueue *queue);
uint32_t sched_runqueue_active_count(const struct sched_runqueue *queue);
uint32_t sched_runqueue_completed_count(const struct sched_runqueue *queue);
uint32_t sched_runqueue_switch_count(const struct sched_runqueue *queue);
