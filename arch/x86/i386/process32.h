#pragma once

#include <stdint.h>
#include "kernel/public/proc/context.h"

struct early_vfs;
struct process_snapshot;

void process32_init(struct early_vfs *vfs);
int32_t process32_spawn_from_user(const char *command,
                                  uint32_t mode,
                                  uint32_t flags);
uintptr_t process32_exec_replace_from_user(
    const struct process_context *context,
    const char *command);
int process32_run_command(const char *command,
                          struct process_snapshot *process);
