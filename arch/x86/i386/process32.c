#include "process32.h"
#include "process32_internal.h"

#include "fs/early_vfs.h"
#include "fs/vfs.h"
#include "kernel/public/proc/process_user_backend.h"

struct early_vfs *g_process32_vfs;
struct vfs *g_process32_runtime_vfs;

void process32_init(struct early_vfs *vfs) {
    g_process32_vfs = vfs;
}

void process32_init_runtime_vfs(struct vfs *vfs) {
    g_process32_runtime_vfs = vfs;
}

static const struct process_user_backend_ops process32_backend_ops = {
    process32_spawn_from_user,
    process32_fork_from_user,
    process32_exec_replace_from_user,
    process32_run_command
};

void process32_register_backend(void) {
    process_user_backend_register(&process32_backend_ops);
}
