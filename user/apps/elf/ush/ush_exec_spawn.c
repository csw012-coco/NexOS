#include "user/apps/elf/ush/ush_exec_internal.h"

static const char *ush_exit_reason_local(int32_t exit_code) {
    switch (exit_code) {
        case 130:
            return "Interrupted";
        case -4:
            return "Illegal instruction";
        case -8:
            return "Floating point exception";
        case -11:
            return "Segmentation fault";
        default:
            return NULL;
    }
}

static void ush_report_foreground_exit_status(const struct syscall_process_info *info) {
    static uint32_t last_reported_pid = 0;
    const char *reason;

    if (info == NULL) {
        return;
    }
    reason = ush_exit_reason_local(info->exit_code);
    if (reason == NULL) {
        return;
    }
    if (info->pid == last_reported_pid) {
        return;
    }
    last_reported_pid = info->pid;
    write_err_str(reason);
    write_err_str("\n");
}

int ush_last_foreground_status_local(void) {
    struct syscall_process_info info;

    if (proc_query(NEX_PROC_QUERY_LAST_EXIT, 0, &info) <= 0) {
        return g_ush_last_foreground_status;
    }
    return info.exit_code == 0 ? 0 : 1;
}

static void ush_report_background_start_local(uint32_t pid, const char *command) {
    if (pid != 0u) {
        printf("[bg] pid=%u ", pid);
    } else {
        write_str("[bg] pid=? ");
    }
    write_str(command);
    write_str("\n");
}

static void ush_cleanup_failed_foreground_spawn(uint32_t pid,
                                                const struct syscall_process_info *info) {
    struct syscall_process_info killed;

    if (pid == 0u || (info != NULL && info->pid == pid)) {
        return;
    }
    if (kill(pid) > 0) {
        (void)wait(pid, &killed);
    }
}

int ush_spawn_command_local(const char *command, uint32_t mode, int background) {
    uint32_t pid = 0u;
    int rc;

    g_ush_last_background_pid = 0u;
    rc = spawn(command, mode, background ? SYS_SPAWN_BACKGROUND : 0);
    if (rc < 0) {
        return rc;
    }
    if (background) {
        pid = (uint32_t)rc;
        g_ush_last_background_pid = pid;
        if (!g_ush_suppress_background_report) {
            ush_report_background_start_local(pid, command);
        }
    } else {
        struct syscall_process_info info;
        int fg_rc;
        int wait_rc;

        fg_rc = rc != 0 ? fg((uint32_t)rc) : -NEX_ERR_INVAL;
        if (fg_rc <= 0) {
            if (rc == 0) {
                write_err_str("fg failed rc=");
                eprintf("%d\n", fg_rc);
                g_ush_last_foreground_status = 1;
                return 1;
            }
            wait_rc = wait((uint32_t)rc, &info);
            if (wait_rc <= 0) {
                write_err_str("fg failed rc=");
                eprintf("%d\n", fg_rc);
                write_err_str("wait failed rc=");
                eprintf("%d\n", wait_rc);
                ush_cleanup_failed_foreground_spawn((uint32_t)rc, NULL);
                g_ush_last_foreground_status = 1;
                return 1;
            }
            g_ush_last_foreground_status = info.exit_code == 0 ? 0 : 1;
            ush_report_foreground_exit_status(&info);
            (void)tty_claim();
            return 0;
        }
        wait_rc = wait((uint32_t)rc, &info);
        if (wait_rc <= 0) {
            write_err_str("wait failed rc=");
            eprintf("%d\n", wait_rc);
            g_ush_last_foreground_status = 1;
            return 1;
        }
        g_ush_last_foreground_status = info.exit_code == 0 ? 0 : 1;
        ush_report_foreground_exit_status(&info);
        (void)tty_claim();
    }
    return 0;
}
