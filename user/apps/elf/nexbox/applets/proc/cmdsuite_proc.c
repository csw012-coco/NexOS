#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static void write_process_state(uint32_t state) {
    switch (state) {
        case NEX_PROC_STATE_READY:
            write_str("ready");
            break;
        case NEX_PROC_STATE_RUNNING:
            write_str("running");
            break;
        case NEX_PROC_STATE_SLEEPING:
            write_str("sleeping");
            break;
        case NEX_PROC_STATE_STOPPED:
            write_str("stopped");
            break;
        case NEX_PROC_STATE_EXITED:
            write_str("exited");
            break;
        case NEX_PROC_STATE_WAITING:
            write_str("waiting");
            break;
        default:
            write_str("free");
            break;
    }
}

static void write_process_kind(uint32_t kind) {
    switch (kind) {
        case NEX_PROC_IMAGE_ELF:
            write_str("elf");
            break;
        default:
            write_str("none");
            break;
    }
}

static void write_process_table_header(int jobs_view) {
    if (jobs_view) {
        write_str("SLOT PID   STATE      WAKE     NAME\n");
        return;
    }
    write_str("SLOT PID   STATE      EXIT                         NAME\n");
}

static void write_process_info_line(const struct syscall_process_info *info, int jobs_view) {
    write_dec(info->slot);
    write_str(info->slot < 10u ? "    " : "   ");
    write_dec(info->pid);
    if (info->pid < 10u) {
        write_str("     ");
    } else if (info->pid < 100u) {
        write_str("    ");
    } else if (info->pid < 1000u) {
        write_str("   ");
    } else if (info->pid < 10000u) {
        write_str("  ");
    } else {
        write_str(" ");
    }
    write_process_state(info->state);
    if (info->state == NEX_PROC_STATE_READY) {
        write_str("        ");
    } else if (info->state == NEX_PROC_STATE_RUNNING) {
        write_str("       ");
    } else if (info->state == NEX_PROC_STATE_SLEEPING) {
        write_str("     ");
    } else if (info->state == NEX_PROC_STATE_STOPPED) {
        write_str("      ");
    } else if (info->state == NEX_PROC_STATE_EXITED) {
        write_str("       ");
    } else if (info->state == NEX_PROC_STATE_WAITING) {
        write_str("       ");
    } else {
        write_str("         ");
    }
    if (jobs_view) {
        if (info->state == NEX_PROC_STATE_SLEEPING) {
            write_dec(info->wake_tick);
        } else {
            write_str("-");
        }
        if (info->state != NEX_PROC_STATE_SLEEPING || info->wake_tick < 10u) {
            write_str("        ");
        } else if (info->wake_tick < 100u) {
            write_str("       ");
        } else if (info->wake_tick < 1000u) {
            write_str("      ");
        } else if (info->wake_tick < 10000u) {
            write_str("     ");
        } else {
            write_str("    ");
        }
    } else {
        write_process_exit_status(info->exit_code);
        write_str(" ");
        write_process_kind(info->image_kind);
        if (info->exit_code == 0) {
            write_str("                    ");
        } else {
            write_str(" ");
        }
    }
    write_str(info->name[0] != '\0' ? info->name : "(unnamed)");
    write_str("\n");
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

int cmd_ps(void) {
    struct syscall_process_info info;
    uint32_t i;

    write_str("process slots\n");
    write_process_table_header(0);
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        write_process_info_line(&info, 0);
    }
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
    write_process_table_header(1);
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
        write_process_info_line(&info, 1);
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
        write_err_str("no exited process\n");
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
    uint32_t before[NEX_PROC_SLOTS_MAX];
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

    capture_process_ids_local(before);
    rc = spawn(command, SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND);
    if (rc != 0) {
        write_err_str("timeout: spawn failed\n");
        return 1;
    }

    start = ticks();
    while ((uint32_t)(ticks() - start) < 100u) {
        pid = find_new_process_pid_local(before);
        if (pid != 0u) {
            break;
        }
        yield();
    }
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

    if (kill(pid) > 0) {
        write_err_str("timeout: expired\n");
    } else {
        write_err_str("timeout: expired (kill failed)\n");
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
            write_err_str("kill failed\n");
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
            write_err_str("fg failed\n");
            return 1;
        }
        write_process_action_result("foreground pid=", pid);
        return 0;
    }
    rc = bg(pid);
    if (rc <= 0) {
        write_err_str("bg failed\n");
        return 1;
    }
    write_process_action_result("background pid=", pid);
    return 0;
}
