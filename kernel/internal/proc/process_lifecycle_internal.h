#pragma once

#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/mem/address_space_internal.h"

void process_clear_slot_state(struct process *proc);
void process_init_stdio(struct process *proc);
uint32_t sched_current_ticks(void);
void process_set_name(struct process *proc, const char *name);
void process_refresh_name_ptr(struct process *proc);
void process_snapshot_fill(struct process_snapshot *out, const struct process *proc);
int job_process_ignores_sigint(const struct process *proc);
void job_set_process_foreground_pid(const struct process *proc, uint32_t pid);
void job_clear_process_foreground_pid(const struct process *proc);
struct process *process_alloc_slot(struct process_session *session, const struct process *parent_proc);
void process_clear_current(struct process_session *session);
void process_discard_files(struct process *proc);
void process_mark_exited(struct process *proc, int32_t exit_code);
