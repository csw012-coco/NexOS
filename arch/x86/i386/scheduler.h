#pragma once

#include <stdint.h>

struct process_context;
struct process_snapshot;
struct sched_policy_stats;

int i386_scheduler_run(uint32_t entry0,
                       uint32_t stack0,
                       uint32_t root0,
                       uint32_t entry1,
                       uint32_t stack1,
                       uint32_t root1);
const struct process_context *i386_scheduler_tick(
    const struct process_context *context);
void i386_scheduler_register_process_ops(void);
void i386_scheduler_register_file_ops(void);
void i386_scheduler_register_mm_ops(void);
uint32_t i386_scheduler_ticks(void);
uint32_t i386_scheduler_current_pid(void);
int32_t i386_scheduler_kill(uint32_t pid);
int i386_scheduler_process_snapshot(uint32_t task,
                                    struct process_snapshot *snapshot);
int i386_scheduler_handle_page_fault(uint32_t fault_address,
                                     uint32_t error_code);
uintptr_t i386_scheduler_fault_exit(const struct process_context *context,
                                    int exit_code);
uint32_t i386_scheduler_switches(void);
uint32_t i386_scheduler_completed(void);
int i386_scheduler_policy_stats(struct sched_policy_stats *out);
uint32_t i386_scheduler_quiet_tty_output(void);
uint32_t i386_scheduler_task_ticks(uint32_t task);
uint32_t i386_scheduler_task_root(uint32_t task);
uint32_t i386_scheduler_task_result(uint32_t task);
