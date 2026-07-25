#include "kernel/public/proc/process_user_backend.h"

static const struct process_user_backend_ops *g_process_user_backend;

void process_user_backend_register(const struct process_user_backend_ops *ops) {
    g_process_user_backend = ops;
}

int32_t process_user_spawn_from_user(const char *command,
                                     uint32_t mode,
                                     uint32_t flags) {
    if (g_process_user_backend == 0 ||
        g_process_user_backend->spawn_from_user == 0) {
        return -1;
    }
    return g_process_user_backend->spawn_from_user(command, mode, flags);
}

int32_t process_user_fork_from_user(const struct process_context *context,
                                    uint32_t *child_pid_out) {
    if (g_process_user_backend == 0 ||
        g_process_user_backend->fork_from_user == 0) {
        return -1;
    }
    return g_process_user_backend->fork_from_user(context, child_pid_out);
}

uintptr_t process_user_exec_replace_from_user(
    const struct process_context *context,
    const char *command) {
    if (g_process_user_backend == 0 ||
        g_process_user_backend->exec_replace_from_user == 0) {
        return 0u;
    }
    return g_process_user_backend->exec_replace_from_user(context, command);
}

int process_user_run_command(const char *command,
                             struct process_snapshot *process) {
    if (g_process_user_backend == 0 ||
        g_process_user_backend->run_command == 0) {
        return 0;
    }
    return g_process_user_backend->run_command(command, process);
}
