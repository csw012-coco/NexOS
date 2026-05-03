#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

static const char *g_nexctl_service_dir = "/SYSTEM/SERVICE";

static int nexctl_entry_has_ext_local(const char *name, const char *ext) {
    uint32_t name_len = str_len_local(name);
    uint32_t ext_len = str_len_local(ext);

    if (name == NULL || ext == NULL || name_len <= ext_len + 1u) {
        return 0;
    }
    if (name[name_len - ext_len - 1u] != '.') {
        return 0;
    }
    return streq_local(name + name_len - ext_len, ext);
}

static uint32_t nexctl_count_services_local(void) {
    struct syscall_dirent entry;
    uint32_t count = 0;
    int fd = opendir(g_nexctl_service_dir);

    if (fd < 0) {
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (nexctl_entry_has_ext_local(entry.name, "svc")) {
            count++;
        }
    }
    close((uint32_t)fd);
    return count;
}

static uint32_t nexctl_count_processes_local(void) {
    struct syscall_process_info info;
    uint32_t count = 0;
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0 &&
            info.state != NEX_PROC_STATE_FREE) {
            count++;
        }
    }
    return count;
}

static uint32_t nexctl_count_mounts_local(void) {
    struct syscall_mount_info info;
    uint32_t count = 0;

    while (mount_query(count, &info) > 0) {
        count++;
    }
    return count;
}

static uint32_t nexctl_count_programs_local(void) {
    struct syscall_program_info info;
    uint32_t count = 0;

    while (program_query(count, &info) > 0) {
        count++;
    }
    return count;
}

static int nexctl_status_local(void) {
    struct syscall_machine_info machine;
    struct syscall_pmm_info pmm;
    struct syscall_rtc_info rtc;
    uint32_t now_ticks = ticks();

    write_str("NexOS control\n");
    if (machine_info_query(&machine) > 0) {
        write_str("system:   ");
        write_str(machine.os_name);
        write_str(" ");
        write_str(machine.kernel_name);
        write_str(" ");
        write_str(machine.kernel_version);
        write_str(" ");
        write_str(machine.arch_name);
        write_str("\nbuild:    ");
        write_str(machine.build_date);
        write_str("\nconsole:  ");
        write_dec(machine.text_columns);
        write_str("x");
        write_dec(machine.text_rows);
        write_str("\n");
    } else {
        write_str("system:   unavailable\n");
    }

    write_str("uptime:   ");
    write_dec(now_ticks);
    write_str(" ticks\n");

    if (rtc_query(&rtc) > 0 && rtc.present && rtc.valid) {
        write_str("rtc:      ");
        write_dec(rtc.year);
        write_str("-");
        if (rtc.month < 10u) {
            write_str("0");
        }
        write_dec(rtc.month);
        write_str("-");
        if (rtc.day < 10u) {
            write_str("0");
        }
        write_dec(rtc.day);
        write_str(" ");
        if (rtc.hour < 10u) {
            write_str("0");
        }
        write_dec(rtc.hour);
        write_str(":");
        if (rtc.minute < 10u) {
            write_str("0");
        }
        write_dec(rtc.minute);
        write_str(":");
        if (rtc.second < 10u) {
            write_str("0");
        }
        write_dec(rtc.second);
        write_str("\n");
    }

    if (pmm_query(&pmm) > 0) {
        write_str("memory:   ");
        write_dec(pmm.free_pages);
        write_str("/");
        write_dec(pmm.total_pages);
        write_str(" pages free, ");
        write_dec(pmm.used_pages);
        write_str(" used\n");
    } else {
        write_str("memory:   unavailable\n");
    }

    write_str("mounts:   ");
    write_dec(nexctl_count_mounts_local());
    write_str("\nprograms: ");
    write_dec(nexctl_count_programs_local());
    write_str("\nprocess:  ");
    write_dec(nexctl_count_processes_local());
    write_str("\nservices: ");
    write_dec(nexctl_count_services_local());
    write_str(" defined\n");
    return 0;
}

static int nexctl_service_delegate_local(int argc, char **argv) {
    char *service_argv[8];
    int i;

    service_argv[0] = "service";
    if (argc == 2) {
        service_argv[1] = "list";
        return cmd_service(2, service_argv);
    }
    if (argc > 8) {
        write_err_usage("nexctl services", " [list|info|start|stop|restart|enable|disable|boot] ...\n");
        return 1;
    }
    for (i = 2; i < argc; i++) {
        service_argv[i - 1] = argv[i];
    }
    return cmd_service(argc - 1, service_argv);
}

static void nexctl_help_local(void) {
    write_str("usage: nexctl <command> [args]\n");
    write_str("commands:\n");
    write_str("  info,status       show system summary\n");
    write_str("  services [args]   list or manage services\n");
    write_str("  logs              show kernel log\n");
    write_str("  apps              list registered programs\n");
    write_str("  mounts            list mounted filesystems\n");
    write_str("  storage [args]    show filesystem space usage\n");
    write_str("  ps                show processes\n");
    write_str("  help              show this help\n");
}

int cmd_nexctl(int argc, char **argv) {
    if (argc < 2 ||
        streq_ignore_case_local(argv[1], "help") ||
        streq_local(argv[1], "-h") ||
        streq_local(argv[1], "--help")) {
        nexctl_help_local();
        return 0;
    }
    if (streq_ignore_case_local(argv[1], "info") ||
        streq_ignore_case_local(argv[1], "status")) {
        return nexctl_status_local();
    }
    if (streq_ignore_case_local(argv[1], "services") ||
        streq_ignore_case_local(argv[1], "service")) {
        return nexctl_service_delegate_local(argc, argv);
    }
    if (streq_ignore_case_local(argv[1], "logs") ||
        streq_ignore_case_local(argv[1], "log")) {
        return cmd_dmesg();
    }
    if (streq_ignore_case_local(argv[1], "apps") ||
        streq_ignore_case_local(argv[1], "programs")) {
        return cmd_progs();
    }
    if (streq_ignore_case_local(argv[1], "mounts")) {
        return cmd_mounts();
    }
    if (streq_ignore_case_local(argv[1], "storage") ||
        streq_ignore_case_local(argv[1], "df")) {
        char *df_argv[4];
        int i;

        if (argc > 5) {
            write_err_usage("nexctl storage", " [df-args]\n");
            return 1;
        }
        df_argv[0] = "df";
        for (i = 2; i < argc; i++) {
            df_argv[i - 1] = argv[i];
        }
        return cmd_df(argc - 1, df_argv);
    }
    if (streq_ignore_case_local(argv[1], "ps") ||
        streq_ignore_case_local(argv[1], "processes")) {
        return cmd_ps();
    }

    write_err_str("nexctl: unknown command: ");
    write_err_str(argv[1]);
    write_err_str("\n");
    nexctl_help_local();
    return 1;
}
