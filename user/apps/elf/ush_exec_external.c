#include "user/apps/elf/ush_exec_internal.h"

static const char ush_external_ansi_error[] = "\x1b[1;31m";
static const char ush_external_ansi_value[] = "\x1b[1;33m";

static int ush_spawn_command_local(const char *command, uint32_t mode, int background);

static int ush_program_name_needs_path(const char *name, int resolve_dot_name) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '/' || name[0] == '.' || contains_char_local(name, '/')) {
        return 1;
    }
    return resolve_dot_name && contains_char_local(name, '.');
}

static int ush_build_program_command(const char *arg,
                                     const char *verb,
                                     int resolve_dot_name,
                                     char *out,
                                     uint32_t out_size) {
    char token[64];
    const char *rest = arg;
    const char *name = token;
    uint32_t out_len = 0;

    if (out == NULL || out_size == 0) {
        return 0;
    }
    if (!read_token_local(&rest, token, sizeof(token))) {
        write_err_str("usage: ");
        write_err_str(verb);
        write_err_str(" <name> [args]\n");
        out[0] = '\0';
        return 0;
    }

    (void)resolve_dot_name;

    copy_line_local(out, name, out_size);
    out_len = str_len_local(out);
    rest = skip_spaces_local(rest);
    if (rest != NULL && *rest != '\0') {
        uint32_t i = 0;

        if (out_len + 1u >= out_size) {
            write_err_str(verb);
            write_err_str(": command line too long\n");
            out[0] = '\0';
            return 0;
        }
        out[out_len++] = ' ';
        while (rest[i] != '\0') {
            if (out_len + 1u >= out_size) {
                write_err_str(verb);
                write_err_str(": command line too long\n");
                out[0] = '\0';
                return 0;
            }
            out[out_len++] = rest[i++];
        }
        out[out_len] = '\0';
    }

    return 1;
}

static int ush_build_cmd_search_command_from(const char *line,
                                             const char *cmd_dir,
                                             int lower_name,
                                             char *out,
                                             uint32_t out_size) {
    char token[64];
    const char *rest = line;
    uint32_t out_len;

    if (out == NULL || out_size == 0 || cmd_dir == NULL) {
        return 0;
    }
    if (!read_token_local(&rest, token, sizeof(token))) {
        out[0] = '\0';
        return 0;
    }
    if (lower_name) {
        lower_in_place_local(token);
    } else {
        upper_in_place_local(token);
    }

    if (snprintf(out, out_size, "%s/%s", cmd_dir, token) < 0 || out[0] == '\0') {
        out[0] = '\0';
        return 0;
    }
    out_len = str_len_local(out);
    rest = skip_spaces_local(rest);
    if (rest != NULL && *rest != '\0') {
        uint32_t i = 0;

        if (out_len + 1u >= out_size) {
            out[0] = '\0';
            return 0;
        }
        out[out_len++] = ' ';
        while (rest[i] != '\0') {
            if (out_len + 1u >= out_size) {
                out[0] = '\0';
                return 0;
            }
            out[out_len++] = rest[i++];
        }
        out[out_len] = '\0';
    }
    return 1;
}

static int ush_build_cmd_search_command(const char *line, char *out, uint32_t out_size) {
    return ush_build_cmd_search_command_from(line, "/cmd", 0, out, out_size);
}

static int ush_build_cmd_search_command_lower(const char *line, char *out, uint32_t out_size) {
    return ush_build_cmd_search_command_from(line, "/cmd", 1, out, out_size);
}

static int ush_build_prefixed_command(const char *prefix,
                                      const char *line,
                                      char *out,
                                      uint32_t out_size) {
    uint32_t out_len;
    uint32_t i = 0;

    if (prefix == NULL || line == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    copy_line_local(out, prefix, out_size);
    out_len = str_len_local(out);
    if (out_len == 0 || out_len + 1u >= out_size) {
        out[0] = '\0';
        return 0;
    }
    out[out_len++] = ' ';
    while (line[i] != '\0') {
        if (out_len + 1u >= out_size) {
            out[0] = '\0';
            return 0;
        }
        out[out_len++] = line[i++];
    }
    out[out_len] = '\0';
    return 1;
}

static int ush_is_nexbox32_applet_name(const char *name) {
    static const char *const applets[] = {
        "help", "actions", "action", "mapper", "echo", "yes", "clear",
        "pwd", "tty", "env", "font", "which", "type", "ls", "cat",
        "less", "hexdump", "grep", "date", "hwclock", "sleep", "watch",
        "on", "events", "clipboard", "wc", "head", "tail", "find",
        "as", "pick", "select", "sort-by", "count-by", "to", "view",
        "ed", "vi", "vim", "touch", "mv", "cp", "mkdir", "rmdir",
        "rm", "asm", "stat", "du", "tree", "file", "blk", "parts",
        "fdisk", "dd", "mkfs", "df", "mounts", "progs", "fatls",
        "fatfind", "fatread", "cpio", "mount", "umount", "hotplug",
        "run", "runelf", "runbg", "ps", "session", "service", "jobs",
        "wait", "alarm", "timeout", "kill", "fg", "bg", "switch_root",
        "reboot", "dmesg", "lspci", "ac97", "hda", "rtl8139",
        "rtl8139tx", "rtl8139rx", "arp", "route", "netstat", "ping",
        "dns", "dhcp", "ifconfig", "http", "wget", "nc", "audio",
        "tone", "wav", "mplay", "doctor", "nexctl", "sysinfo",
        "meminfo", "minfo", "uname", "cpuinfo", "config", "dbg",
        "nexbox", "nexbox32"
    };

    for (uint32_t i = 0u; i < sizeof(applets) / sizeof(applets[0]); i++) {
        if (streq_local(name, applets[i])) {
            return 1;
        }
    }
    return 0;
}

static int ush_build_action_command(const char *line, char *out, uint32_t out_size) {
    uint32_t prefix_len;
    uint32_t i = 0;

    if (line == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    prefix_len = (uint32_t)snprintf(out, out_size, "/cmd/action run ");
    if (prefix_len == 0u || prefix_len >= out_size) {
        out[0] = '\0';
        return 0;
    }
    while (line[i] != '\0') {
        if (prefix_len + 1u >= out_size) {
            out[0] = '\0';
            return 0;
        }
        out[prefix_len++] = line[i++];
    }
    out[prefix_len] = '\0';
    return 1;
}

static int ush_parse_shebang_command(const char *path,
                                     const char *original_line,
                                     char *out,
                                     uint32_t out_size) {
    char header[128];
    char interpreter[64];
    char extra[64];
    const char *cursor;
    uint32_t out_len;
    uint32_t i;
    int fd;

    fd = open(path, 0);
    if (fd < 0) {
        return 0;
    }
    if (read_line((uint32_t)fd, header, sizeof(header)) == 0) {
        close((uint32_t)fd);
        return 0;
    }
    close((uint32_t)fd);
    if (header[0] != '#' || header[1] != '!') {
        return 0;
    }

    cursor = header + 2;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (!read_token_local(&cursor, interpreter, sizeof(interpreter))) {
        return 0;
    }
    cursor = skip_spaces_local(cursor);
    copy_line_local(extra, cursor != NULL ? cursor : "", sizeof(extra));

    copy_line_local(out, interpreter, out_size);
    out_len = str_len_local(out);
    if (extra[0] != '\0') {
        if (out_len + str_len_local(extra) + 2u >= out_size) {
            return 0;
        }
        out[out_len++] = ' ';
        for (i = 0; extra[i] != '\0'; i++) {
            out[out_len++] = extra[i];
        }
        out[out_len] = '\0';
    }
    if (out_len + str_len_local(path) + 2u >= out_size) {
        return 0;
    }
    out[out_len++] = ' ';
    for (i = 0; path[i] != '\0'; i++) {
        out[out_len++] = path[i];
    }

    cursor = original_line;
    if (!read_token_local(&cursor, interpreter, sizeof(interpreter))) {
        out[out_len] = '\0';
        return 1;
    }
    cursor = skip_spaces_local(cursor);
    if (cursor != NULL && *cursor != '\0') {
        if (out_len + str_len_local(cursor) + 2u >= out_size) {
            return 0;
        }
        out[out_len++] = ' ';
        for (i = 0; cursor[i] != '\0'; i++) {
            out[out_len++] = cursor[i];
        }
    }
    out[out_len] = '\0';
    return 1;
}

static int ush_try_shebang_command(const char *line, const char *path, int background) {
    char command[256];
    int rc;

    if (!ush_parse_shebang_command(path, line, command, sizeof(command))) {
        return 0;
    }
    rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
    if (rc == 0) {
        return 1;
    }
    ush_write_colored_err(ush_external_ansi_error, "script exec failed: ");
    ush_write_colored_err(ush_external_ansi_value, command);
    write_err_str(" rc=");
    eprintf("%d\n", rc);
    return 1;
}

static int ush_try_sh_script_command(char *cwd,
                                     const char *line,
                                     const char *path,
                                     int background) {
    char arg_storage[10][USH_LINE_MAX + 1];
    char *argv[10];
    const char *cursor = line;
    int argc = 0;

    if (path == NULL || !ends_with_ignore_case_local(path, ".sh")) {
        return 0;
    }
    if (background) {
        write_err_str("background: shell script cannot run in background\n");
        return 1;
    }
    while (argc < 10 && read_token_local(&cursor,
                                         arg_storage[argc],
                                         sizeof(arg_storage[argc]))) {
        argv[argc] = arg_storage[argc];
        argc++;
    }
    cursor = skip_spaces_local(cursor);
    if (argc == 0 || (cursor != NULL && *cursor != '\0')) {
        write_err_str("script: too many or invalid arguments\n");
        return 1;
    }
    argv[0] = (char *)path;
    return ush_run_script_file(cwd, path, argc, argv);
}

static int ush_parse_wav_command(const char *original_line,
                                 const char *path,
                                 char *out,
                                 uint32_t out_size) {
    char token[64];
    const char *cursor = original_line;
    uint32_t out_len = 0;
    uint32_t i;

    if (!ends_with_ignore_case_local(path, ".wav")) {
        return 0;
    }
    copy_line_local(out, "/cmd/mplay", out_size);
    out_len = str_len_local(out);
    if (out_len + str_len_local(path) + 2u >= out_size) {
        return 0;
    }
    out[out_len++] = ' ';
    for (i = 0; path[i] != '\0'; i++) {
        out[out_len++] = path[i];
    }
    if (!read_token_local(&cursor, token, sizeof(token))) {
        out[out_len] = '\0';
        return 1;
    }
    cursor = skip_spaces_local(cursor);
    if (cursor != NULL && *cursor != '\0') {
        if (out_len + str_len_local(cursor) + 2u >= out_size) {
            return 0;
        }
        out[out_len++] = ' ';
        for (i = 0; cursor[i] != '\0'; i++) {
            out[out_len++] = cursor[i];
        }
    }
    out[out_len] = '\0';
    return 1;
}

static int ush_try_wav_command(const char *line, const char *path, int background) {
    char command[256];
    int rc;

    if (!ush_parse_wav_command(line, path, command, sizeof(command))) {
        return 0;
    }
    rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
    if (rc == 0) {
        return 1;
    }
    ush_write_colored_err(ush_external_ansi_error, "mplay exec failed: ");
    ush_write_colored_err(ush_external_ansi_value, command);
    write_err_str(" rc=");
    eprintf("%d\n", rc);
    return 1;
}

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

static void ush_report_foreground_exit_status(void) {
    static uint32_t last_reported_pid = 0;
    struct syscall_process_info info;
    const char *reason;

    if (proc_query(NEX_PROC_QUERY_LAST_EXIT, 0, &info) <= 0) {
        return;
    }
    reason = ush_exit_reason_local(info.exit_code);
    if (reason == NULL) {
        return;
    }
    if (info.pid == last_reported_pid) {
        return;
    }
    last_reported_pid = info.pid;
    write_err_str(reason);
    write_err_str("\n");
}

static int ush_last_foreground_status_local(void) {
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

static int ush_spawn_command_local(const char *command, uint32_t mode, int background) {
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
        int status = 0;

        (void)ush_wait_pipeline_pid((uint32_t)rc, &status);
        g_ush_last_foreground_status = status;
        ush_report_foreground_exit_status();
    }
    return 0;
}

int ush_try_external_command(char *cwd, const char *line, int background, int *handled_out) {
    char token[64];
    char command[256];
    char action_command[256];
    char search_command[256];
    const char *cursor = line;
    int explicit_path;
    int rc;

    if (!read_token_local(&cursor, token, sizeof(token))) {
        if (handled_out != NULL) {
            *handled_out = 0;
        }
        return 0;
    }

    if (contains_char_local(token, '.') &&
        token[0] != '.' &&
        !contains_char_local(token, '/') &&
        ush_build_action_command(line, action_command, sizeof(action_command))) {
        rc = ush_spawn_command_local(action_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
    }

    explicit_path = ush_program_name_needs_path(token, 1);
    if (explicit_path) {
        if (!ush_build_program_command(line, token, 1, command, sizeof(command))) {
            return 1;
        }
        rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
        if (ends_with_ignore_case_local(token, ".sh")) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return ush_try_sh_script_command(cwd, line, token, background);
        }
        if (ush_try_shebang_command(line, token, background)) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 0;
        }
        if (ush_try_wav_command(line, token, background)) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return 0;
        }
        ush_write_colored_err(ush_external_ansi_error, "exec failed: ");
        ush_write_colored_err(ush_external_ansi_value, command);
        write_err_str(" rc=");
        eprintf("%d\n", rc);
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 1;
    }

    if (ush_is_nexbox32_applet_name(token) &&
        ush_build_prefixed_command("/BOOT/NEXBOX32.ELF",
                                   line,
                                   search_command,
                                   sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
    }

    (void)cwd;
    /*
     * Let the kernel's program registry resolve built-in and NexBox applets
     * before probing wrapper scripts in /ram/CMD and /cmd. This avoids up to
     * three failed path lookups and an extra ush process for each applet.
     */
    rc = ush_spawn_command_local(line, SYS_SPAWN_AUTO, background);
    if (rc == 0) {
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return background ? 0 : ush_last_foreground_status_local();
    }

    if (ush_build_cmd_search_command_from(line, "/ram/CMD", 0, search_command, sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
    }

    if (ush_build_cmd_search_command_lower(line, search_command, sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
    }

    if (ush_build_cmd_search_command(line, search_command, sizeof(search_command))) {
        rc = ush_spawn_command_local(search_command, SYS_SPAWN_ELF, background);
        if (rc == 0) {
            if (handled_out != NULL) {
                *handled_out = 1;
            }
            return background ? 0 : ush_last_foreground_status_local();
        }
    }

    if (ush_try_wav_command(line, token, background)) {
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return 0;
    }

    if (ends_with_ignore_case_local(token, ".sh")) {
        if (handled_out != NULL) {
            *handled_out = 1;
        }
        return ush_try_sh_script_command(cwd, line, token, background);
    }

    if (handled_out != NULL) {
        *handled_out = 0;
    }
    return 0;
}
