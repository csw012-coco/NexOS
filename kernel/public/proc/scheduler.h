#pragma once

#include <stdint.h>
#include "kernel/public/proc/process.h"
#include "kernel/public/sys/syscall.h"

struct process_session;

enum {
    SCHED_TRACE_EVENT_COUNT = 8
};

struct sched_trace_event {
    uint32_t tick;
    uint32_t from_pid;
    uint32_t to_pid;
    enum process_state from_state;
    enum process_state to_state;
    char from_name[32];
    char to_name[32];
    char reason[16];
};

void sched_yield_current(struct process_session *session, const struct syscall_frame *frame);
void sched_sleep_current(struct process_session *session, const struct syscall_frame *frame, uint32_t ticks);
void sched_preempt_current(struct process_session *session, const struct syscall_frame *frame);
void sched_resume_current_syscall(struct process_session *session,
                                  const struct syscall_frame *frame,
                                  uint64_t result);
void sched_prepare_user_return(void);
uint64_t sched_prepare_user_frame_return(const struct syscall_frame *frame);
uint32_t sched_current_ticks(void);
void sched_on_timer_tick(uint32_t current_ticks);
void sched_tick(void);
void sched_tick_excluding_pid(uint32_t pid);
void sched_trace_snapshot(const struct sched_trace_event **events_out,
                          uint32_t *count_out,
                          uint32_t *next_out);
