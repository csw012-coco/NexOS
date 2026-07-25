#include "kernel/public/proc/process_scheduler_ops.h"

static const struct process_scheduler_ops *g_process_scheduler_ops;

void process_scheduler_ops_register(const struct process_scheduler_ops *ops) {
    g_process_scheduler_ops = ops;
}

int32_t process_scheduler_spawn_image(uint32_t entry,
                                      uint32_t stack,
                                      uint32_t root,
                                      const char *name) {
    struct process_loaded_image image;

    image.entry = entry;
    image.stack = stack;
    image.root = root;
    image.name = name;
    return process_scheduler_spawn_loaded(&image);
}

int32_t process_scheduler_spawn_loaded(
    const struct process_loaded_image *image) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->spawn_loaded == 0 ||
        image == 0) {
        return -1;
    }
    return g_process_scheduler_ops->spawn_loaded(image);
}

int process_scheduler_run_loaded(const struct process_loaded_image *image,
                                 struct process_snapshot *snapshot) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->run_loaded == 0 ||
        image == 0 ||
        snapshot == 0) {
        return 0;
    }
    return g_process_scheduler_ops->run_loaded(image, snapshot);
}

int process_scheduler_run_loaded_quiet(const struct process_loaded_image *image,
                                       struct process_snapshot *snapshot) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->run_loaded_quiet == 0 ||
        image == 0 ||
        snapshot == 0) {
        return 0;
    }
    return g_process_scheduler_ops->run_loaded_quiet(image, snapshot);
}

int32_t process_scheduler_fork_current(const struct process_context *context,
                                       uint32_t *child_pid_out) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->fork_current == 0) {
        return -1;
    }
    return g_process_scheduler_ops->fork_current(context, child_pid_out);
}

uintptr_t process_scheduler_exec_replace(const struct process_context *context,
                                         uint32_t entry,
                                         uint32_t stack,
                                         uint32_t root,
                                         const char *name) {
    struct process_loaded_image image;

    image.entry = entry;
    image.stack = stack;
    image.root = root;
    image.name = name;
    return process_scheduler_exec_replace_loaded(context, &image);
}

uintptr_t process_scheduler_exec_replace_loaded(
    const struct process_context *context,
    const struct process_loaded_image *image) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->exec_replace_loaded == 0 ||
        image == 0) {
        return 0u;
    }
    return g_process_scheduler_ops->exec_replace_loaded(context, image);
}

uintptr_t process_scheduler_wait(const struct process_context *context,
                                 uint32_t pid,
                                 int32_t *status,
                                 int *blocked,
                                 uint32_t user_info,
                                 struct process_snapshot *snapshot) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->wait == 0) {
        return 0u;
    }
    return g_process_scheduler_ops->wait(context,
                                         pid,
                                         status,
                                         blocked,
                                         user_info,
                                         snapshot);
}

uintptr_t process_scheduler_exit(const struct process_context *context,
                                 int exit_code) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->exit == 0) {
        return 0u;
    }
    return g_process_scheduler_ops->exit(context, exit_code);
}

uintptr_t process_scheduler_yield(const struct process_context *context) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->yield == 0) {
        return 0u;
    }
    return g_process_scheduler_ops->yield(context);
}

uintptr_t process_scheduler_sleep(const struct process_context *context,
                                  uint32_t ticks) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->sleep == 0) {
        return 0u;
    }
    return g_process_scheduler_ops->sleep(context, ticks);
}

uint32_t process_scheduler_ticks(void) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->ticks == 0) {
        return 0u;
    }
    return g_process_scheduler_ops->ticks();
}

uint32_t process_scheduler_current_pid(void) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->current_pid == 0) {
        return 0u;
    }
    return g_process_scheduler_ops->current_pid();
}

int32_t process_scheduler_kill(uint32_t pid) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->kill == 0) {
        return -1;
    }
    return g_process_scheduler_ops->kill(pid);
}

int process_scheduler_snapshot(uint32_t task,
                               struct process_snapshot *snapshot) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->snapshot == 0) {
        return 0;
    }
    return g_process_scheduler_ops->snapshot(task, snapshot);
}

int32_t process_scheduler_reap_exited_pid(uint32_t pid) {
    if (g_process_scheduler_ops == 0 ||
        g_process_scheduler_ops->reap_exited_pid == 0) {
        return -1;
    }
    return g_process_scheduler_ops->reap_exited_pid(pid);
}
