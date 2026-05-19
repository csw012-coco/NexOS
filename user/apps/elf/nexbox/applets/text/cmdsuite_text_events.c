#include "user/apps/elf/nexbox/applets/text/cmdsuite_text_common.h"

static int on_token_has_dot_local(const char *text) {
    uint32_t i = 0;

    while (text != NULL && text[i] != '\0') {
        if (text[i] == '/') {
            return 0;
        }
        if (text[i] == '.') {
            return i != 0u;
        }
        i++;
    }
    return 0;
}

static int parse_on_interval_local(const char *text, uint32_t *ticks_out) {
    if (text == NULL || ticks_out == NULL) {
        return 0;
    }
    if (starts_with_text_local(text, "interval=")) {
        char *endptr = 0;
        unsigned long value = strtoul(text + 9, &endptr, 10);

        if (endptr == text + 9 || *endptr != '\0' || value > 0xfffffffful) {
            return 0;
        }
        *ticks_out = (uint32_t)value;
        return 1;
    }
    return parse_sleep_ticks_local(text, ticks_out);
}

static void on_print_event_file_local(const char *path) {
    char buffer[128];
    int fd = open(path, 0);

    if (fd < 0) {
        return;
    }
    for (;;) {
        uint32_t got = (uint32_t)read((uint32_t)fd, buffer, sizeof(buffer));

        if (got == 0u) {
            break;
        }
        write_stdout(buffer, got);
        if (got < sizeof(buffer)) {
            break;
        }
    }
    close((uint32_t)fd);
}

static uint32_t on_read_event_fd_local(int fd, char *buffer, uint32_t size) {
    uint32_t total = 0;

    if (fd < 0 || buffer == NULL || size == 0u) {
        return 0;
    }
    while (total + 1u < size) {
        uint32_t got = (uint32_t)read((uint32_t)fd, buffer + total, size - total - 1u);

        if (got == 0u) {
            break;
        }
        total += got;
        if (got < size - total - 1u) {
            break;
        }
    }
    buffer[total < size ? total : size - 1u] = '\0';
    return total;
}

static const char *on_keyboard_ascii_name_local(const char *key) {
    if (key == NULL || key[0] == '\0') {
        return NULL;
    }
    if (streq_local(key, "space")) {
        return "space";
    }
    if (streq_local(key, "tab")) {
        return "\\t";
    }
    if (streq_local(key, "any")) {
        return "any";
    }
    if (key[1] == '\0') {
        return key;
    }
    return NULL;
}

static int on_keyboard_event_matches_local(const char *events, const char *key) {
    char pattern[32];
    const char *ascii_name;

    if (events == NULL || key == NULL || !text_contains_local(events, "event input.keyboard ")) {
        return 0;
    }
    if (!text_contains_local(events, " state=press ")) {
        return 0;
    }
    ascii_name = on_keyboard_ascii_name_local(key);
    if (ascii_name == NULL) {
        return 0;
    }
    if (streq_local(ascii_name, "any")) {
        return 1;
    }
    copy_line_local(pattern, " key=", sizeof(pattern));
    {
        uint32_t pos = str_len_local(pattern);
        uint32_t i = 0;

        while (ascii_name[i] != '\0' && pos + 2u < sizeof(pattern)) {
            pattern[pos++] = ascii_name[i++];
        }
        pattern[pos++] = ' ';
        pattern[pos] = '\0';
    }
    return text_contains_local(events, pattern);
}

static int on_file_event_matches_local(const char *events, const char *path) {
    char pattern[CMD_PATH_MAX + 8u];
    uint32_t pos = 0;
    uint32_t i = 0;

    if (events == NULL || path == NULL || !text_contains_local(events, "event file.change ")) {
        return 0;
    }
    copy_line_local(pattern, " path=", sizeof(pattern));
    pos = str_len_local(pattern);
    while (path[i] != '\0' && pos + 2u < sizeof(pattern)) {
        pattern[pos++] = path[i++];
    }
    pattern[pos++] = ' ';
    pattern[pos] = '\0';
    return text_contains_local(events, pattern);
}

static int on_net_status_event_matches_local(const char *events, const char *state) {
    char pattern[32];
    uint32_t pos;
    uint32_t i = 0;

    if (events == NULL || state == NULL || !text_contains_local(events, "event net.status ")) {
        return 0;
    }
    if (streq_local(state, "any")) {
        return 1;
    }
    if (!streq_local(state, "up") && !streq_local(state, "down")) {
        return 0;
    }
    copy_line_local(pattern, " state=", sizeof(pattern));
    pos = str_len_local(pattern);
    while (state[i] != '\0' && pos + 2u < sizeof(pattern)) {
        pattern[pos++] = state[i++];
    }
    pattern[pos++] = ' ';
    pattern[pos] = '\0';
    return text_contains_local(events, pattern);
}

static int on_mouse_button_mask_local(const char *button, uint32_t *mask_out) {
    if (button == NULL || mask_out == NULL) {
        return 0;
    }
    if (streq_local(button, "any")) {
        *mask_out = 0;
        return 1;
    }
    if (streq_local(button, "left")) {
        *mask_out = 1u;
        return 1;
    }
    if (streq_local(button, "right")) {
        *mask_out = 2u;
        return 1;
    }
    if (streq_local(button, "middle")) {
        *mask_out = 4u;
        return 1;
    }
    return 0;
}

static uint32_t on_parse_u32_field_local(const char *text) {
    uint32_t value = 0;

    while (text != NULL && *text >= '0' && *text <= '9') {
        value = value * 10u + (uint32_t)(*text - '0');
        text++;
    }
    return value;
}

static int on_mouse_event_matches_local(const char *events, const char *button) {
    uint32_t mask;
    uint32_t i = 0;

    if (events == NULL || button == NULL || !on_mouse_button_mask_local(button, &mask)) {
        return 0;
    }
    while (events[i] != '\0') {
        if (starts_with_text_local(events + i, "event input.mouse ")) {
            uint32_t j = i;

            if (mask == 0u) {
                return 1;
            }
            while (events[j] != '\0' && events[j] != '\n') {
                if (starts_with_text_local(events + j, " buttons=")) {
                    uint32_t buttons = on_parse_u32_field_local(events + j + 9);

                    if ((buttons & mask) != 0u) {
                        return 1;
                    }
                    break;
                }
                j++;
            }
        }
        while (events[i] != '\0' && events[i] != '\n') {
            i++;
        }
        if (events[i] == '\n') {
            i++;
        }
    }
    return 0;
}

static int on_block_event_matches_local(const char *events, const char *op) {
    char pattern[32];
    uint32_t pos;
    uint32_t i = 0;

    if (events == NULL || op == NULL || !text_contains_local(events, "event block.change ")) {
        return 0;
    }
    if (streq_local(op, "any")) {
        return 1;
    }
    if (!streq_local(op, "add") && !streq_local(op, "partition")) {
        return 0;
    }
    copy_line_local(pattern, " op=", sizeof(pattern));
    pos = str_len_local(pattern);
    while (op[i] != '\0' && pos + 2u < sizeof(pattern)) {
        pattern[pos++] = op[i++];
    }
    pattern[pos++] = ' ';
    pattern[pos] = '\0';
    return text_contains_local(events, pattern);
}

enum {
    EVENT_JOB_COMMAND_MAX = 240u,
    EVENT_JOB_ID_MAX = 12u,
    EVENT_JOB_META_MAX = 512u
};

static const char *g_event_job_dir = "/HOME/EVENTS";

static int event_job_is_log_name_local(const char *name) {
    return text_contains_local(name, ".LOG") || text_contains_local(name, ".log");
}

static void event_job_make_id_local(char *out, uint32_t out_size) {
    uint32_t value = ticks() % 1000000u;

    if (out == NULL || out_size == 0u) {
        return;
    }
    (void)snprintf(out, out_size, "J%06u", value);
}

static int event_job_make_path_local(const char *id, const char *suffix, char *out, uint32_t out_size) {
    if (id == NULL || id[0] == '\0' || out == NULL || out_size == 0u) {
        return 0;
    }
    return snprintf(out, out_size, "%s/%s%s", g_event_job_dir, id, suffix != NULL ? suffix : "") >= 0;
}

static int event_job_append_arg_local(char *out, uint32_t out_size, const char *arg) {
    uint32_t len;
    uint32_t arg_len;

    if (out == NULL || out_size == 0u || arg == NULL) {
        return 0;
    }
    len = str_len_local(out);
    arg_len = str_len_local(arg);
    if (len + arg_len + 2u >= out_size) {
        return 0;
    }
    if (len != 0u) {
        out[len++] = ' ';
        out[len] = '\0';
    }
    copy_line_local(out + len, arg, out_size - len);
    return 1;
}

static int event_job_build_worker_command_local(int argc, char **argv, const char *id, char *out, uint32_t out_size) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (!event_job_append_arg_local(out, out_size, "/CMD/ON") ||
        !event_job_append_arg_local(out, out_size, "--event-job") ||
        !event_job_append_arg_local(out, out_size, id)) {
        return 0;
    }
    for (int i = 1, after_run = 0; i < argc; i++) {
        if (streq_local(argv[i], "run")) {
            after_run = 1;
        }
        if (!after_run && streq_local(argv[i], "--daemon")) {
            continue;
        }
        if (!after_run && streq_local(argv[i], "--event-job")) {
            i++;
            continue;
        }
        if (!event_job_append_arg_local(out, out_size, argv[i])) {
            return 0;
        }
    }
    return 1;
}

static void event_job_capture_pids_local(uint32_t *out) {
    struct syscall_process_info info;

    if (out == NULL) {
        return;
    }
    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        out[i] = 0u;
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0) {
            out[i] = info.pid;
        }
    }
}

static int event_job_pid_seen_local(const uint32_t *snapshot, uint32_t pid) {
    if (snapshot == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (snapshot[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static uint32_t event_job_find_new_pid_local(const uint32_t *snapshot) {
    struct syscall_process_info info;

    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid != 0u && !event_job_pid_seen_local(snapshot, info.pid)) {
            return info.pid;
        }
    }
    return 0u;
}

static int event_job_find_process_local(uint32_t pid, struct syscall_process_info *out) {
    struct syscall_process_info info;

    for (uint32_t i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid == pid) {
            if (out != NULL) {
                *out = info;
            }
            return 1;
        }
    }
    return 0;
}

static int event_job_read_file_local(const char *path, char *out, uint32_t out_size) {
    int fd;
    uint32_t total = 0;

    if (path == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    fd = open(path, 0);
    if (fd < 0) {
        out[0] = '\0';
        return 0;
    }
    while (total + 1u < out_size) {
        uint32_t got = (uint32_t)read((uint32_t)fd, out + total, out_size - total - 1u);

        if (got == 0u) {
            break;
        }
        total += got;
        if (got < out_size - total - 1u) {
            break;
        }
    }
    out[total] = '\0';
    close((uint32_t)fd);
    return 1;
}

static int event_job_meta_value_local(const char *meta, const char *key, char *out, uint32_t out_size) {
    uint32_t i = 0;
    uint32_t key_len = str_len_local(key);

    if (meta == NULL || key == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    while (meta[i] != '\0') {
        if ((i == 0u || meta[i - 1u] == '\n') && starts_with_text_local(meta + i, key) &&
            meta[i + key_len] == ' ') {
            uint32_t pos = 0;
            i += key_len + 1u;
            while (meta[i] != '\0' && meta[i] != '\n' && pos + 1u < out_size) {
                out[pos++] = meta[i++];
            }
            out[pos] = '\0';
            return 1;
        }
        while (meta[i] != '\0' && meta[i] != '\n') {
            i++;
        }
        if (meta[i] == '\n') {
            i++;
        }
    }
    return 0;
}

static int event_job_parse_pid_local(const char *meta, uint32_t *pid_out) {
    char value[16];

    if (!event_job_meta_value_local(meta, "pid", value, sizeof(value))) {
        return 0;
    }
    return parse_u32_local(value, pid_out);
}

static int event_job_write_meta_local(const char *id, uint32_t pid, const char *command) {
    char path[CMD_PATH_MAX];
    char log_path[CMD_PATH_MAX];
    int fd;

    if (!event_job_make_path_local(id, "", path, sizeof(path)) ||
        !event_job_make_path_local(id, ".LOG", log_path, sizeof(log_path))) {
        return 0;
    }
    fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        return 0;
    }
    fdprintf((uint32_t)fd, "id %s\npid %u\nlog %s\ncmd %s\n", id, pid, log_path, command != NULL ? command : "");
    close((uint32_t)fd);
    return 1;
}

static int event_job_attach_log_local(const char *id) {
    char log_path[CMD_PATH_MAX];
    int fd;

    (void)mkdir("/HOME");
    (void)mkdir(g_event_job_dir);
    if (!event_job_make_path_local(id, ".LOG", log_path, sizeof(log_path))) {
        return 0;
    }
    fd = open(log_path, O_CREAT | O_APPEND);
    if (fd < 0) {
        return 0;
    }
    (void)dup2(fd, 1);
    (void)dup2(fd, 2);
    if (fd > 2) {
        close((uint32_t)fd);
    }
    fdprintf(1, "event-job %s start tick=%u\n", id, ticks());
    return 1;
}

static int event_job_spawn_daemon_local(int argc, char **argv) {
    uint32_t before[NEX_PROC_SLOTS_MAX];
    uint32_t pid = 0u;
    char id[EVENT_JOB_ID_MAX];
    char command[EVENT_JOB_COMMAND_MAX];
    int rc;

    (void)mkdir("/HOME");
    (void)mkdir(g_event_job_dir);
    event_job_make_id_local(id, sizeof(id));
    if (!event_job_build_worker_command_local(argc, argv, id, command, sizeof(command))) {
        write_err_str("on: daemon command too long\n");
        return 1;
    }
    event_job_capture_pids_local(before);
    rc = spawn(command, SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND);
    if (rc != 0) {
        write_err_str("on: daemon spawn failed\n");
        return 1;
    }
    for (uint32_t i = 0; i < 100u; i++) {
        pid = event_job_find_new_pid_local(before);
        if (pid != 0u) {
            break;
        }
        yield();
    }
    (void)event_job_write_meta_local(id, pid, command);
    write_str("event job ");
    write_str(id);
    write_str(" started");
    if (pid != 0u) {
        write_str(" pid=");
        write_dec(pid);
    }
    write_str("\n");
    return 0;
}

static int on_run_command_local(int argc, char **argv) {
    char *action_argv[18];

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return 1;
    }
    if (on_token_has_dot_local(argv[0])) {
        if (argc + 2 > (int)(sizeof(action_argv) / sizeof(action_argv[0]))) {
            write_err_str("on: too many action arguments\n");
            return 1;
        }
        action_argv[0] = "action";
        action_argv[1] = "run";
        for (int i = 0; i < argc; i++) {
            action_argv[i + 2] = argv[i];
        }
        return cmdsuite_dispatch_main(argc + 2, action_argv);
    }
    return cmdsuite_dispatch_main(argc, argv);
}

int cmd_on(int argc, char **argv) {
    uint32_t wait_ticks = 1000u;
    int wait_ticks_set = 0;
    int verbose = 0;
    int single = 0;
    int argi = 1;
    int run_index;
    const char *event_job_id = NULL;

    for (int i = 1; i < argc; i++) {
        if (streq_local(argv[i], "run")) {
            break;
        }
        if (streq_local(argv[i], "--daemon")) {
            return event_job_spawn_daemon_local(argc, argv);
        }
        if (streq_local(argv[i], "--event-job")) {
            i++;
        }
    }

    while (argi < argc && argv[argi][0] == '-') {
        if (streq_local(argv[argi], "-n") && argi + 1 < argc) {
            if (!parse_on_interval_local(argv[argi + 1], &wait_ticks)) {
                write_err_usage("on", " [-n interval] [-1] <file.change|event.timer|event.input.keyboard|event.input.mouse|event.net.status|event.block.change> ... run <command> [args]\n");
                return 1;
            }
            wait_ticks_set = 1;
            argi += 2;
        } else if (streq_local(argv[argi], "-1")) {
            single = 1;
            argi++;
        } else if (streq_local(argv[argi], "-v")) {
            verbose = 1;
            argi++;
        } else if (streq_local(argv[argi], "--event-job") && argi + 1 < argc) {
            event_job_id = argv[argi + 1];
            argi += 2;
        } else {
            write_err_usage("on", " [-n interval] [-1] <file.change|event.timer|event.input.keyboard|event.input.mouse|event.net.status|event.block.change> ... run <command> [args]\n");
            return 1;
        }
    }
    if (event_job_id != NULL && !event_job_attach_log_local(event_job_id)) {
        write_err_str("on: cannot attach event job log\n");
        return 1;
    }
    if (argc - argi >= 4 && streq_local(argv[argi], "event.timer")) {
        if (starts_with_text_local(argv[argi + 1], "interval=")) {
            if (!parse_on_interval_local(argv[argi + 1], &wait_ticks)) {
                write_err_usage("on", " [-1] event.timer interval=<ms> run <command> [args]\n");
                return 1;
            }
            run_index = argi + 2;
        } else {
            run_index = argi + 1;
        }
        if (!streq_local(argv[run_index], "run") || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.timer [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/timer every ");
        write_dec(wait_ticks);
        write_str("ms (q to quit)\n");
        for (;;) {
            uint32_t start = ticks();
            char ch = 0;

            while ((uint32_t)(ticks() - start) < wait_ticks) {
                if (read_char_nonblock(&ch) == 0) {
                    if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                        return 0;
                    }
                }
                yield();
            }
            on_print_event_file_local("/event/timer");
            (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
            if (single) {
                return 0;
            }
        }
    }
    if (argc - argi >= 3 && streq_local(argv[argi], "event.net.status")) {
        const char *state = "any";
        uint32_t net_wait_ticks = wait_ticks_set ? wait_ticks : 20u;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "state=")) {
                state = argv[run_index] + 6;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &net_wait_ticks)) {
                    write_err_usage("on", " [-1] event.net.status [state=<up|down|any>] [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.net.status [state=<up|down|any>] [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if ((!streq_local(state, "up") && !streq_local(state, "down") && !streq_local(state, "any")) ||
            run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.net.status [state=<up|down|any>] [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/net/status state=");
        write_str(state);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/net/status", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/net/status\n");
                return 1;
            }
            for (;;) {
                uint32_t start = ticks();
                char ch = 0;
                char events[512];

                while ((uint32_t)(ticks() - start) < net_wait_ticks) {
                    if (read_char_nonblock(&ch) == 0) {
                        if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                            close((uint32_t)event_fd);
                            return 0;
                        }
                    }
                    yield();
                }
                if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                    continue;
                }
                if (!on_net_status_event_matches_local(events, state)) {
                    continue;
                }
                if (verbose) {
                    write_str("event net.status state=");
                    write_str(state);
                    write_str("\n");
                }
                (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
                if (single) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
        }
    }
    if (argc - argi >= 3 && streq_local(argv[argi], "event.input.mouse")) {
        const char *button = "any";
        uint32_t mouse_wait_ticks = wait_ticks_set ? wait_ticks : 20u;
        uint32_t button_mask = 0;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "button=")) {
                button = argv[run_index] + 7;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &mouse_wait_ticks)) {
                    write_err_usage("on", " [-1] event.input.mouse [button=<left|right|middle|any>] [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.input.mouse [button=<left|right|middle|any>] [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if (!on_mouse_button_mask_local(button, &button_mask) || run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.input.mouse [button=<left|right|middle|any>] [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        (void)button_mask;
        write_str("on: watching /event/input/mouse button=");
        write_str(button);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/input/mouse", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/input/mouse\n");
                return 1;
            }
            for (;;) {
                uint32_t start = ticks();
                char ch = 0;
                char events[512];

                while ((uint32_t)(ticks() - start) < mouse_wait_ticks) {
                    if (read_char_nonblock(&ch) == 0) {
                        if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                            close((uint32_t)event_fd);
                            return 0;
                        }
                    }
                    yield();
                }
                if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                    continue;
                }
                if (!on_mouse_event_matches_local(events, button)) {
                    continue;
                }
                if (verbose) {
                    write_str("event input.mouse button=");
                    write_str(button);
                    write_str("\n");
                }
                (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
                if (single) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
        }
    }
    if (argc - argi >= 3 && streq_local(argv[argi], "event.block.change")) {
        const char *op = "any";
        uint32_t block_wait_ticks = wait_ticks_set ? wait_ticks : 20u;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "op=")) {
                op = argv[run_index] + 3;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &block_wait_ticks)) {
                    write_err_usage("on", " [-1] event.block.change [op=<add|partition|any>] [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.block.change [op=<add|partition|any>] [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if ((!streq_local(op, "add") && !streq_local(op, "partition") && !streq_local(op, "any")) ||
            run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.block.change [op=<add|partition|any>] [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/block/change op=");
        write_str(op);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/block/change", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/block/change\n");
                return 1;
            }
            for (;;) {
                uint32_t start = ticks();
                char ch = 0;
                char events[512];

                while ((uint32_t)(ticks() - start) < block_wait_ticks) {
                    if (read_char_nonblock(&ch) == 0) {
                        if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                            close((uint32_t)event_fd);
                            return 0;
                        }
                    }
                    yield();
                }
                if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                    continue;
                }
                if (!on_block_event_matches_local(events, op)) {
                    continue;
                }
                if (verbose) {
                    write_str("event block.change op=");
                    write_str(op);
                    write_str("\n");
                }
                (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
                if (single) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
        }
    }
    if (argc - argi >= 4 && streq_local(argv[argi], "event.input.keyboard")) {
        const char *key = NULL;
        uint32_t keyboard_wait_ticks = wait_ticks_set ? wait_ticks : 20u;

        run_index = argi + 1;
        while (run_index < argc && !streq_local(argv[run_index], "run")) {
            if (starts_with_text_local(argv[run_index], "key=")) {
                key = argv[run_index] + 4;
            } else if (starts_with_text_local(argv[run_index], "interval=")) {
                if (!parse_on_interval_local(argv[run_index], &keyboard_wait_ticks)) {
                    write_err_usage("on", " [-1] event.input.keyboard key=<char|space|tab|any> [interval=<ms>] run <command> [args]\n");
                    return 1;
                }
            } else {
                write_err_usage("on", " [-1] event.input.keyboard key=<char|space|tab|any> [interval=<ms>] run <command> [args]\n");
                return 1;
            }
            run_index++;
        }
        if (key == NULL || on_keyboard_ascii_name_local(key) == NULL ||
            run_index >= argc || run_index + 1 >= argc) {
            write_err_usage("on", " [-1] event.input.keyboard key=<char|space|tab|any> [interval=<ms>] run <command> [args]\n");
            return 1;
        }
        write_str("on: watching /event/input/keyboard key=");
        write_str(key);
        write_str(" (q to quit)\n");
        {
            int event_fd = open("/event/input/keyboard", 0);

            if (event_fd < 0) {
                write_err_str("on: cannot open /event/input/keyboard\n");
                return 1;
            }
            for (;;) {
            uint32_t start = ticks();
            char ch = 0;
            char events[512];

            while ((uint32_t)(ticks() - start) < keyboard_wait_ticks) {
                if (read_char_nonblock(&ch) == 0) {
                    if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                        close((uint32_t)event_fd);
                        return 0;
                    }
                }
                yield();
            }
            if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
                continue;
            }
            if (!on_keyboard_event_matches_local(events, key)) {
                continue;
            }
            if (verbose) {
                write_str("event input.keyboard key=");
                write_str(key);
                write_str("\n");
            }
            (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
            if (single) {
                close((uint32_t)event_fd);
                return 0;
            }
        }
        }
    }
    if (argc - argi < 5 || !streq_local(argv[argi], "file.change")) {
        write_err_usage("on", " [-n interval] [-1] file.change <path> run <command> [args]\n");
        return 1;
    }
    run_index = argi + 3;
    if (!streq_local(argv[run_index], "run") || run_index + 1 >= argc) {
        write_err_usage("on", " [-n interval] [-1] file.change <path> run <command> [args]\n");
        return 1;
    }
    write_str("on: watching file.change ");
    write_str(argv[argi + 1]);
    write_str(" (q to quit)\n");
    {
        int event_fd = open("/event/file/change", 0);

        if (event_fd < 0) {
            write_err_str("on: cannot open /event/file/change\n");
            return 1;
        }
        for (;;) {
        uint32_t start = ticks();
        char ch = 0;
        char events[512];

        while ((uint32_t)(ticks() - start) < wait_ticks) {
            if (read_char_nonblock(&ch) == 0) {
                if (ch == 'q' || ch == 'Q' || ch == 0x03) {
                    close((uint32_t)event_fd);
                    return 0;
                }
            }
            yield();
        }
        if (on_read_event_fd_local(event_fd, events, sizeof(events)) == 0u) {
            continue;
        }
        if (!on_file_event_matches_local(events, argv[argi + 1])) {
            continue;
        }
        if (verbose) {
            write_str("event file.change path=");
            write_str(argv[argi + 1]);
            write_str("\n");
        }
        (void)on_run_command_local(argc - run_index - 1, argv + run_index + 1);
        if (single) {
            close((uint32_t)event_fd);
            return 0;
        }
    }
    }
}

static void events_write_state_local(uint32_t state) {
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
            write_str("gone");
            break;
    }
}

static int events_open_meta_by_id_local(const char *id_arg, char *path_out, uint32_t path_size) {
    char id[EVENT_JOB_ID_MAX];
    uint32_t numeric_id;

    if (id_arg == NULL || path_out == NULL || path_size == 0u) {
        return 0;
    }
    if (id_arg[0] == 'J' || id_arg[0] == 'j') {
        copy_line_local(id, id_arg, sizeof(id));
    } else if (parse_u32_local(id_arg, &numeric_id)) {
        (void)snprintf(id, sizeof(id), "J%06u", numeric_id % 1000000u);
    } else {
        return 0;
    }
    return event_job_make_path_local(id, "", path_out, path_size);
}

static void events_print_log_file_local(const char *path) {
    int fd;
    char buffer[128];

    fd = open(path, 0);
    if (fd < 0) {
        write_err_str("events: log not found\n");
        return;
    }
    for (;;) {
        uint32_t got = (uint32_t)read((uint32_t)fd, buffer, sizeof(buffer));

        if (got == 0u) {
            break;
        }
        write_stdout(buffer, got);
        if (got < sizeof(buffer)) {
            break;
        }
    }
    close((uint32_t)fd);
}

static int cmd_events_jobs_local(void) {
    struct syscall_dirent entry;
    int fd;
    uint32_t count = 0;

    fd = opendir(g_event_job_dir);
    if (fd < 0) {
        write_str("event jobs\nID      PID   STATE      LOG                  COMMAND\n");
        write_str("jobs=0\n");
        return 0;
    }
    write_str("event jobs\nID      PID   STATE      LOG                  COMMAND\n");
    while (readdir((uint32_t)fd, &entry) > 0) {
        char path[CMD_PATH_MAX];
        char meta[EVENT_JOB_META_MAX];
        char value[EVENT_JOB_COMMAND_MAX];
        char log_path[CMD_PATH_MAX];
        uint32_t pid = 0;
        struct syscall_process_info info;
        int alive;

        if (entry.name[0] == '\0' || event_job_is_log_name_local(entry.name)) {
            continue;
        }
        if (!event_job_make_path_local(entry.name, "", path, sizeof(path)) ||
            !event_job_read_file_local(path, meta, sizeof(meta)) ||
            !event_job_parse_pid_local(meta, &pid)) {
            continue;
        }
        alive = event_job_find_process_local(pid, &info);
        count++;
        write_text_padded(entry.name, 8u);
        write_dec(pid);
        if (pid < 10u) {
            write_str("     ");
        } else if (pid < 100u) {
            write_str("    ");
        } else if (pid < 1000u) {
            write_str("   ");
        } else {
            write_str("  ");
        }
        events_write_state_local(alive ? info.state : 0u);
        write_str(alive && info.state == NEX_PROC_STATE_SLEEPING ? "     " : "      ");
        if (!event_job_meta_value_local(meta, "log", log_path, sizeof(log_path))) {
            copy_line_local(log_path, "-", sizeof(log_path));
        }
        write_text_padded(log_path, 21u);
        if (!event_job_meta_value_local(meta, "cmd", value, sizeof(value))) {
            copy_line_local(value, "-", sizeof(value));
        }
        write_str(value);
        write_str("\n");
    }
    close((uint32_t)fd);
    write_str("jobs=");
    write_dec(count);
    write_str("\n");
    return 0;
}

int cmd_events(int argc, char **argv) {
    const char *op = argc >= 2 ? argv[1] : "jobs";

    if (streq_local(op, "as-table")) {
        char *as_argv[] = {"as", "event"};

        if (argc != 2) {
            write_err_usage("events", " as-table\n");
            return 1;
        }
        return cmd_as(2, as_argv);
    }
    if (streq_local(op, "jobs") || streq_local(op, "list")) {
        return cmd_events_jobs_local();
    }
    if (streq_local(op, "log")) {
        char meta_path[CMD_PATH_MAX];
        char meta[EVENT_JOB_META_MAX];
        char log_path[CMD_PATH_MAX];

        if (argc < 3 || !events_open_meta_by_id_local(argv[2], meta_path, sizeof(meta_path))) {
            write_err_usage("events", " log <id>\n");
            return 1;
        }
        if (!event_job_read_file_local(meta_path, meta, sizeof(meta)) ||
            !event_job_meta_value_local(meta, "log", log_path, sizeof(log_path))) {
            write_err_str("events: job not found\n");
            return 1;
        }
        events_print_log_file_local(log_path);
        return 0;
    }
    if (streq_local(op, "stop")) {
        char meta_path[CMD_PATH_MAX];
        char meta[EVENT_JOB_META_MAX];
        uint32_t pid = 0;

        if (argc < 3) {
            write_err_usage("events", " stop <id|pid>\n");
            return 1;
        }
        if (events_open_meta_by_id_local(argv[2], meta_path, sizeof(meta_path)) &&
            event_job_read_file_local(meta_path, meta, sizeof(meta)) &&
            event_job_parse_pid_local(meta, &pid)) {
            /* got pid from metadata */
        } else if (!parse_u32_local(argv[2], &pid)) {
            write_err_usage("events", " stop <id|pid>\n");
            return 1;
        }
        if (pid == 0u || kill(pid) <= 0) {
            write_err_str("events: stop failed\n");
            return 1;
        }
        write_str("stopped event job pid=");
        write_dec(pid);
        write_str("\n");
        return 0;
    }
    write_err_usage("events", " [jobs|as-table|log <id>|stop <id|pid>]\n");
    return 1;
}
