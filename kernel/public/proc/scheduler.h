#pragma once

#include <stdint.h>
#include "kernel/public/sys/syscall.h"

struct process_session;

void sched_yield_current(struct process_session *session, const struct syscall_frame *frame);
void sched_sleep_current(struct process_session *session, const struct syscall_frame *frame, uint32_t ticks);
void sched_preempt_current(struct process_session *session, const struct syscall_frame *frame);
void sched_resume_current_syscall(struct process_session *session,
                                  const struct syscall_frame *frame,
                                  uint64_t result);
void sched_prepare_user_return(void);
uint32_t sched_current_ticks(void);
void sched_on_timer_tick(uint32_t current_ticks);
void sched_tick(void);
