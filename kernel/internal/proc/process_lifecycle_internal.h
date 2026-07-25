#pragma once

#include "kernel/internal/proc/process_internal_base.h"
#include "kernel/internal/mem/address_space_internal.h"

void process_clear_slot_state(struct process *proc);
void process_model_reset(struct process *proc,
                         uint32_t slot,
                         enum process_state state);
void process_init_stdio(struct process *proc);
uint32_t sched_current_ticks(void);
void process_set_name(struct process *proc, const char *name);
void process_refresh_name_ptr(struct process *proc);
void process_snapshot_fill(struct process_snapshot *out, const struct process *proc);
int process_lifecycle_child_matches_wait(const struct process *child,
                                         uint32_t child_parent_pid,
                                         uint32_t waiter_pid,
                                         uint32_t requested_pid,
                                         uint32_t wait_last_pid);
void process_lifecycle_prepare_wait(struct process *waiter);
void process_lifecycle_clear_wait(struct process *waiter);
int process_lifecycle_waiter_matches_exit(const struct process *waiter,
                                          uint32_t waiter_wait_pid,
                                          uint32_t exited_pid);
void process_lifecycle_finish_wait(struct process *waiter,
                                   int32_t exit_code);
int process_lifecycle_wake_file_waiter(struct process *proc,
                                       void *private_data,
                                       uint8_t file_kind);
int process_lifecycle_can_signal_child(const struct process *child,
                                       uint32_t child_parent_pid,
                                       uint32_t caller_pid,
                                       uint32_t target_pid);
int process_lifecycle_find_wait_child(struct process *const *slots,
                                      uint32_t capacity,
                                      uint32_t waiter_pid,
                                      uint32_t requested_pid,
                                      uint32_t wait_last_pid,
                                      uint32_t *slot_out,
                                      int *exited_out);
uint32_t process_lifecycle_wake_exit_waiters(
    struct process **slots,
    uint32_t capacity,
    uint32_t exited_pid,
    int32_t exit_code,
    void (*copy_wait_info)(uint32_t slot,
                           const struct process *exited,
                           void *context),
    void *copy_context);
void process_lifecycle_mark_exited_for_scheduler(struct process *proc,
                                                 int32_t exit_code);
void process_forget_file_array(struct file files[PROCESS_FILE_MAX]);
void process_discard_file_array(struct file files[PROCESS_FILE_MAX]);
int process_clone_spawn_files(const struct process *parent,
                              struct file out[PROCESS_FILE_MAX]);
int process_clone_all_files(const struct process *parent,
                            struct file out[PROCESS_FILE_MAX]);
void process_install_cloned_files(struct process *proc,
                                  struct file cloned[PROCESS_FILE_MAX]);
int job_process_ignores_sigint(const struct process *proc);
void job_set_process_foreground_pid(const struct process *proc, uint32_t pid);
void job_clear_process_foreground_pid(const struct process *proc);
struct process *process_alloc_slot(struct process_session *session, const struct process *parent_proc);
void process_clear_current(struct process_session *session);
void process_discard_files(struct process *proc);
void process_discard_non_stdio_files(struct process *proc);
void process_forget_files(struct process *proc);
void process_wake_file_waiters(void *private_data, uint8_t file_kind);
void process_mark_exit_pending(struct process *proc, int32_t exit_code);
void process_mark_exited(struct process *proc, int32_t exit_code);
