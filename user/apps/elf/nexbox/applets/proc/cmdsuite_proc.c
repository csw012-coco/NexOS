#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static const char *process_state_name_local(uint32_t state) {
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

static void write_process_state(uint32_t state) {
    write_str(process_state_name_local(state));
}

static void write_process_cap_names_local(uint32_t caps) {
    int first = 1;

    if (caps == 0u) {
        write_str("none");
        return;
    }
#define PROC_CAP_NAME(bit, name) \
    do { \
        if ((caps & (bit)) != 0u) { \
            if (!first) { \
                write_str(","); \
            } \
            write_str(name); \
            first = 0; \
        } \
    } while (0)
    PROC_CAP_NAME(SYS_PROC_CAP_POWER, "power");
    PROC_CAP_NAME(SYS_PROC_CAP_RAW_BLOCK, "raw-block");
    PROC_CAP_NAME(SYS_PROC_CAP_MOUNT, "mount");
    PROC_CAP_NAME(SYS_PROC_CAP_SIGNAL, "signal");
    PROC_CAP_NAME(SYS_PROC_CAP_GRANT, "grant");
    PROC_CAP_NAME(SYS_PROC_CAP_AUDIO, "audio");
    PROC_CAP_NAME(SYS_PROC_CAP_NET_RAW, "net-raw");
    PROC_CAP_NAME(SYS_PROC_CAP_DISPLAY, "display");
    PROC_CAP_NAME(SYS_PROC_CAP_INPUT, "input");
    PROC_CAP_NAME(SYS_PROC_CAP_CLIPBOARD, "clipboard");
    PROC_CAP_NAME(SYS_PROC_CAP_DEBUG, "debug");
#undef PROC_CAP_NAME
}

static const char *process_user_name_local(uint32_t uid) {
    if (uid == 0u) {
        return "root";
    }
    if (uid == 1000u) {
        return "user";
    }
    return "unknown";
}

static void write_process_table_header(int jobs_view, int caps_view, int user_view) {
    if (jobs_view) {
        write_str("SLOT PID   STATE      WAKE     NAME\n");
        return;
    }
    if (caps_view) {
        write_str("SLOT PID   STATE      EXIT IMAGE NAME CAPS       CAPNAMES\n");
        return;
    }
    if (user_view) {
        write_str("SLOT PID   UID  GID  USER     STATE      NAME\n");
        return;
    }
    write_str("SLOT PID   STATE      EXIT                         NAME\n");
}

static void write_process_info_line(const struct syscall_process_info *info,
                                    int jobs_view,
                                    int caps_view,
                                    int user_view) {
    if (jobs_view) {
        if (info->state == NEX_PROC_STATE_SLEEPING) {
            dprintf(STDOUT_FILENO,
                    "%u %u %s %u %s\n",
                    info->slot,
                    info->pid,
                    process_state_name_local(info->state),
                    info->wake_tick,
                    info->name[0] != '\0' ? info->name : "(unnamed)");
        } else {
            dprintf(STDOUT_FILENO,
                    "%u %u %s - %s\n",
                    info->slot,
                    info->pid,
                    process_state_name_local(info->state),
                    info->name[0] != '\0' ? info->name : "(unnamed)");
        }
    } else if (caps_view) {
        dprintf(STDOUT_FILENO,
                "%u %u %s %d %s %s ",
                info->slot,
                info->pid,
                process_state_name_local(info->state),
                info->exit_code,
                info->image_kind == NEX_PROC_IMAGE_ELF ? "elf" : "none",
                info->name[0] != '\0' ? info->name : "(unnamed)");
        write_hex_u32(info->caps & SYS_PROC_CAP_ALL);
        write_str(" ");
        write_process_cap_names_local(info->caps & SYS_PROC_CAP_ALL);
        write_str("\n");
    } else if (user_view) {
        dprintf(STDOUT_FILENO,
                "%u %u %u %u %s %s %s\n",
                info->slot,
                info->pid,
                info->uid,
                info->gid,
                process_user_name_local(info->uid),
                process_state_name_local(info->state),
                info->name[0] != '\0' ? info->name : "(unnamed)");
    } else {
        const char *reason = process_exit_reason_local(info->exit_code);

        if (reason != NULL) {
            dprintf(STDOUT_FILENO,
                    "%u %u %s %s (%d) %s %s\n",
                    info->slot,
                    info->pid,
                    process_state_name_local(info->state),
                    reason,
                    info->exit_code,
                    info->image_kind == NEX_PROC_IMAGE_ELF ? "elf" : "none",
                    info->name[0] != '\0' ? info->name : "(unnamed)");
        } else {
            dprintf(STDOUT_FILENO,
                    "%u %u %s %d %s %s\n",
                    info->slot,
                    info->pid,
                    process_state_name_local(info->state),
                    info->exit_code,
                    info->image_kind == NEX_PROC_IMAGE_ELF ? "elf" : "none",
                    info->name[0] != '\0' ? info->name : "(unnamed)");
        }
    }
}

static int find_process_info_by_pid(uint32_t pid, struct syscall_process_info *out) {
    struct syscall_process_info info;
    uint32_t i;

    if (out == NULL) {
        return 0;
    }
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid == pid) {
            *out = info;
            return 1;
        }
    }
    return 0;
}

static int parse_delay_local(const char *text, uint32_t *ticks_out) {
    char *endptr = 0;
    unsigned long value;

    if (text == 0 || text[0] == '\0' || ticks_out == 0) {
        return 0;
    }
    value = strtoul(text, &endptr, 10);
    if (endptr == text || value > 0xfffffffful) {
        return 0;
    }
    if (*endptr == '\0' || streq_local(endptr, "s")) {
        if (value > 4294967ul) {
            return 0;
        }
        *ticks_out = (uint32_t)(value * 1000ul);
        return 1;
    }
    if (streq_local(endptr, "ms")) {
        *ticks_out = (uint32_t)value;
        return 1;
    }
    if (streq_local(endptr, "tick") || streq_local(endptr, "ticks")) {
        *ticks_out = (uint32_t)(value * 10ul);
        return 1;
    }
    return 0;
}

static void capture_process_ids_local(uint32_t *out) {
    struct syscall_process_info info;
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        out[i] = 0u;
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0) {
            out[i] = info.pid;
        }
    }
}

static int pid_seen_local(const uint32_t *snapshot, uint32_t pid) {
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (snapshot[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static uint32_t find_new_process_pid_local(const uint32_t *snapshot) {
    struct syscall_process_info info;
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid != 0u && !pid_seen_local(snapshot, info.pid)) {
            return info.pid;
        }
    }
    return 0u;
}

static void write_process_action_result(const char *label, uint32_t pid) {
    struct syscall_process_info info;

    write_str(label);
    write_dec(pid);
    if (!find_process_info_by_pid(pid, &info)) {
        write_str("\n");
        return;
    }
    write_str(" state=");
    write_process_state(info.state);
    write_str(" name=");
    write_str(info.name[0] != '\0' ? info.name : "(unnamed)");
    write_str("\n");
}

static void write_jobctl_failure_local(const char *name, int rc) {
    if (rc == -NEX_ERR_ACCES || rc == -NEX_ERR_PERM) {
        (void)cmd_report_access_denied(
            name,
            "requires signal/job-control permission");
        return;
    }
    write_err_str(name);
    write_err_str(" failed rc=");
    write_sdec((int32_t)rc);
    write_err_str("\n");
}

int cmd_ps(int argc, char **argv) {
    struct syscall_process_info info;
    int caps_view = 0;
    int user_view = 0;
    uint32_t i;

    if (argc > 2 ||
        (argc == 2 &&
         !streq_ignore_case_local(argv[1], "--caps") &&
         !streq_ignore_case_local(argv[1], "-c") &&
         !streq_ignore_case_local(argv[1], "--user") &&
         !streq_ignore_case_local(argv[1], "-u"))) {
        write_err_usage("ps", " [--caps|--user]\n");
        return 1;
    }
    caps_view = argc == 2 &&
                (streq_ignore_case_local(argv[1], "--caps") ||
                 streq_ignore_case_local(argv[1], "-c"));
    user_view = argc == 2 &&
                (streq_ignore_case_local(argv[1], "--user") ||
                 streq_ignore_case_local(argv[1], "-u"));
    write_str("process slots\n");
    write_process_table_header(0, caps_view, user_view);
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        write_process_info_line(&info, 0, caps_view, user_view);
    }
    return 0;
}

static int current_process_info_local(struct syscall_process_info *out) {
    pid_t pid = getpid();

    if (out == NULL || pid <= 0) {
        return 0;
    }
    return find_process_info_by_pid((uint32_t)pid, out);
}

int cmd_id(int argc, char **argv) {
    struct syscall_process_info info;

    (void)argv;
    if (argc != 1) {
        write_err_usage("id", "\n");
        return 1;
    }
    if (!current_process_info_local(&info)) {
        write_err_str("id: current process not found\n");
        return 1;
    }
    dprintf(STDOUT_FILENO,
            "uid=%u(%s) gid=%u\n",
            info.uid,
            process_user_name_local(info.uid),
            info.gid);
    return 0;
}

int cmd_whoami(int argc, char **argv) {
    struct syscall_process_info info;

    (void)argv;
    if (argc != 1) {
        write_err_usage("whoami", "\n");
        return 1;
    }
    if (!current_process_info_local(&info)) {
        write_err_str("whoami: current process not found\n");
        return 1;
    }
    write_str(process_user_name_local(info.uid));
    write_str("\n");
    return 0;
}

int cmd_jobs(void) {
    struct syscall_process_info info;
    uint32_t count = 0;
    uint32_t running = 0;
    uint32_t sleeping = 0;
    uint32_t stopped = 0;
    uint32_t i;

    write_str("background jobs\n");
    write_process_table_header(1, 0, 0);
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_JOBS, i, &info) <= 0) {
            continue;
        }
        count++;
        if (info.state == NEX_PROC_STATE_RUNNING || info.state == NEX_PROC_STATE_READY) {
            running++;
        } else if (info.state == NEX_PROC_STATE_SLEEPING) {
            sleeping++;
        } else if (info.state == NEX_PROC_STATE_STOPPED) {
            stopped++;
        }
        write_process_info_line(&info, 1, 0, 0);
    }
    write_str("jobs=");
    write_dec(count);
    write_str(" running=");
    write_dec(running);
    write_str(" sleeping=");
    write_dec(sleeping);
    write_str(" stopped=");
    write_dec(stopped);
    write_str("\n");
    return 0;
}

int cmd_wait(int argc, char **argv) {
    struct syscall_process_info info;
    uint32_t pid;
    int rc;

    if (argc < 2) {
        rc = wait(NEX_WAIT_LAST_PID, &info);
    } else {
        if (!parse_u32_local(argv[1], &pid)) {
            write_err_usage("wait", " [pid]\n");
            return 1;
        }
        rc = wait(pid, &info);
    }
    if (rc <= 0) {
        write_jobctl_failure_local("wait", rc);
        return 1;
    }
    write_str("wait: pid=");
    write_dec(info.pid);
    write_str(" state=");
    write_process_state(info.state);
    write_str(" status=");
    write_process_exit_status(info.exit_code);
    write_str(" name=");
    write_str(info.name[0] != '\0' ? info.name : "(unnamed)");
    write_str("\n");
    return 0;
}

int cmd_alarm(int argc, char **argv) {
    uint32_t wait_ticks;
    int i;

    if (argc < 2 || !parse_delay_local(argv[1], &wait_ticks)) {
        write_err_usage("alarm", " <seconds|ms|ticks> [message...]\n");
        return 1;
    }
    sleep(wait_ticks);
    write_stdout("\a", 1);
    if (argc > 2) {
        for (i = 2; i < argc; i++) {
            if (i > 2) {
                write_str(" ");
            }
            write_str(argv[i]);
        }
    } else {
        write_str("alarm");
    }
    write_str("\n");
    return 0;
}

int cmd_timeout(int argc, char **argv) {
    uint32_t wait_ticks;
    struct syscall_process_info info;
    char command[CMD_PATH_MAX];
    uint32_t pid = 0u;
    uint32_t start;
    int rc;

    if (argc < 3 || !parse_delay_local(argv[1], &wait_ticks)) {
        write_err_usage("timeout", " <seconds|ms|ticks> <command> [args]\n");
        return 1;
    }
    if (!cmd_build_program_command(argc, argv, 2, "timeout", 0, command, sizeof(command))) {
        return 1;
    }

    rc = spawn(command, SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND);
    if (rc < 0) {
        write_err_str("timeout: spawn failed\n");
        return 1;
    }

    pid = (uint32_t)rc;
    if (pid == 0u) {
        write_err_str("timeout: could not track child pid\n");
        return 1;
    }

    start = ticks();
    while ((uint32_t)(ticks() - start) < wait_ticks) {
        if (find_process_info_by_pid(pid, &info)) {
            if (info.state == NEX_PROC_STATE_EXITED) {
                if (wait(pid, &info) > 0) {
                    return info.exit_code == 0 ? 0 : 1;
                }
                return 0;
            }
        } else {
            return 0;
        }
        yield();
    }

    rc = kill(pid);
    if (rc > 0) {
        write_err_str("timeout: expired\n");
    } else {
        write_err_str("timeout: expired (kill failed rc=");
        write_sdec((int32_t)rc);
        write_err_str(")\n");
    }
    return 1;
}

int cmd_kill_like(int argc, char **argv, const char *name) {
    uint32_t pid;
    int rc;

    if (argc < 2 || !parse_u32_local(argv[1], &pid)) {
        write_err_usage(name, " <pid>\n");
        return 1;
    }
    if (streq_local(name, "kill")) {
        rc = kill(pid);
        if (rc <= 0) {
            write_jobctl_failure_local("kill", rc);
            return 1;
        }
        write_str("killed pid=");
        write_dec(pid);
        write_str("\n");
        return 0;
    }
    if (streq_local(name, "fg")) {
        rc = fg(pid);
        if (rc <= 0) {
            write_jobctl_failure_local("fg", rc);
            return 1;
        }
        write_process_action_result("foreground pid=", pid);
        return 0;
    }
    rc = bg(pid);
    if (rc <= 0) {
        write_jobctl_failure_local("bg", rc);
        return 1;
    }
    write_process_action_result("background pid=", pid);
    return 0;
}
