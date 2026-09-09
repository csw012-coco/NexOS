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

static void write_process_cap_names32(uint32_t caps) {
    int first = 1;

    if (caps == 0u) {
        write_str("none");
        return;
    }
#define PROC32_CAP_NAME(bit, name) \
    do { \
        if ((caps & (bit)) != 0u) { \
            if (!first) { \
                write_str(","); \
            } \
            write_str(name); \
            first = 0; \
        } \
    } while (0)
    PROC32_CAP_NAME(SYS_PROC_CAP_POWER, "power");
    PROC32_CAP_NAME(SYS_PROC_CAP_RAW_BLOCK, "raw-block");
    PROC32_CAP_NAME(SYS_PROC_CAP_MOUNT, "mount");
    PROC32_CAP_NAME(SYS_PROC_CAP_SIGNAL, "signal");
    PROC32_CAP_NAME(SYS_PROC_CAP_GRANT, "grant");
    PROC32_CAP_NAME(SYS_PROC_CAP_AUDIO, "audio");
    PROC32_CAP_NAME(SYS_PROC_CAP_NET_RAW, "net-raw");
    PROC32_CAP_NAME(SYS_PROC_CAP_DISPLAY, "display");
    PROC32_CAP_NAME(SYS_PROC_CAP_INPUT, "input");
    PROC32_CAP_NAME(SYS_PROC_CAP_CLIPBOARD, "clipboard");
    PROC32_CAP_NAME(SYS_PROC_CAP_DEBUG, "debug");
#undef PROC32_CAP_NAME
}

static const char *process_user_name32(uint32_t uid) {
    if (uid == 0u) {
        return "root";
    }
    if (uid == 1000u) {
        return "user";
    }
    return "unknown";
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
    if (caps_view) {
        write_str("SLOT PID   STATE      EXIT IMAGE NAME CAPS       CAPNAMES\n");
    } else if (user_view) {
        write_str("SLOT PID   UID  GID  USER     STATE      NAME\n");
    } else {
        write_str("SLOT PID   STATE      EXIT                         NAME\n");
    }
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (caps_view) {
            dprintf(STDOUT_FILENO,
                    "%u %u %s %d %s %s ",
                    info.slot,
                    info.pid,
                    process_state_name32(info.state),
                    info.exit_code,
                    info.image_kind == NEX_PROC_IMAGE_ELF ? "elf" : "none",
                    info.name[0] != '\0' ? info.name : "(unnamed)");
            write_hex_u32(info.caps & SYS_PROC_CAP_ALL);
            write_str(" ");
            write_process_cap_names32(info.caps & SYS_PROC_CAP_ALL);
            write_str("\n");
            continue;
        }
        if (user_view) {
            dprintf(STDOUT_FILENO,
                    "%u %u %u %u %s %s %s\n",
                    info.slot,
                    info.pid,
                    info.uid,
                    info.gid,
                    process_user_name32(info.uid),
                    process_state_name32(info.state),
                    info.name[0] != '\0' ? info.name : "(unnamed)");
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

static int current_process_info32(struct syscall_process_info *out) {
    pid_t pid = getpid();

    if (out == 0 || pid <= 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < NEX_PROC_SLOTS_MAX; i++) {
        struct syscall_process_info info;

        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid == (uint32_t)pid) {
            *out = info;
            return 1;
        }
    }
    return 0;
}

int cmd_id(int argc, char **argv) {
    struct syscall_process_info info;

    (void)argv;
    if (argc != 1) {
        write_err_usage("id", "\n");
        return 1;
    }
    if (!current_process_info32(&info)) {
        write_err_text("id: current process not found\n");
        return 1;
    }
    dprintf(STDOUT_FILENO,
            "uid=%u(%s) gid=%u\n",
            info.uid,
            process_user_name32(info.uid),
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
    if (!current_process_info32(&info)) {
        write_err_text("whoami: current process not found\n");
        return 1;
    }
    write_str(process_user_name32(info.uid));
    write_str("\n");
    return 0;
}

int cmd_kill_like(int argc, char **argv, const char *name) {
    uint32_t pid;
    int rc;

    (void)name;
    if (argc != 2 || !parse_pid32(argv[1], &pid)) {
        write_err_usage("kill", " PID\n");
        return 1;
    }
    rc = kill((pid_t)pid);
    if (rc <= 0) {
        write_err_text("kill: failed rc=");
        write_sdec((int32_t)rc);
        write_err_text("\n");
        return 1;
    }
    return 0;
}
