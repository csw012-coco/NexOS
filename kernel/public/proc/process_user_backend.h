#pragma once

#include <stdint.h>
#include "kernel/public/proc/context.h"

struct process_snapshot;

struct process_user_backend_ops {
    int32_t (*spawn_from_user)(const char *command,
                               uint32_t mode,
                               uint32_t flags);
    int32_t (*fork_from_user)(const struct process_context *context,
                              uint32_t *child_pid_out);
    uintptr_t (*exec_replace_from_user)(const struct process_context *context,
                                        const char *command);
    int (*run_command)(const char *command,
                       struct process_snapshot *process);
};

void process_user_backend_register(const struct process_user_backend_ops *ops);
int32_t process_user_spawn_from_user(const char *command,
                                     uint32_t mode,
                                     uint32_t flags);
int32_t process_user_fork_from_user(const struct process_context *context,
                                    uint32_t *child_pid_out);
uintptr_t process_user_exec_replace_from_user(
    const struct process_context *context,
    const char *command);
int process_user_run_command(const char *command,
                             struct process_snapshot *process);
