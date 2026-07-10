#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static const char *process_state_name32(uint32_t state) {
    switch (state) {
        case NEX_PROC_STATE_READY:
            return "ready";
        case NEX_PROC_STATE_RUNNING:
            return "running";
        case NEX_PROC_STATE_SLEEPING:
            return "sleeping";
        case NEX_PROC_STATE_STOPPED:
            return "stopped";
        case NEX_PROC_STATE_EXITED:
            return "exited";
        case NEX_PROC_STATE_WAITING:
            return "waiting";
        default:
            return "free";
    }
}

static int parse_pid32(const char *text, uint32_t *pid_out) {
    return parse_u32_local(text, pid_out) && pid_out != 0 && *pid_out != 0u;
}

int cmd_ps(void) {
    struct syscall_process_info info;
    uint32_t i;

    write_str("process slots\n");
    write_str("SLOT PID   STATE      EXIT                         NAME\n");
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        dprintf(STDOUT_FILENO,
                "%u %u %s %d %s %s\n",
                info.slot,
                info.pid,
                process_state_name32(info.state),
                info.exit_code,
                info.image_kind == NEX_PROC_IMAGE_ELF ? "elf" : "none",
                info.name[0] != '\0' ? info.name : "(unnamed)");
    }
    return 0;
}

int cmd_kill_like(int argc, char **argv, const char *name) {
    uint32_t pid;

    (void)name;
    if (argc != 2 || !parse_pid32(argv[1], &pid)) {
        write_err_usage("kill", " PID\n");
        return 1;
    }
    if (kill((pid_t)pid) < 0) {
        write_err_text("kill: failed\n");
        return 1;
    }
    return 0;
}
