#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

extern char **environ;

enum {
    SESSION_NAME_MAX = 24u,
    SESSION_LINE_MAX = 128u,
    SERVICE_NAME_MAX = 24u,
    SERVICE_LINE_MAX = 160u,
    SERVICE_COMMAND_MAX = 128u
};

static const char *g_session_dir = "/SYSTEM/SESSION/images";
static const char *g_service_dir = "/SYSTEM/SERVICE";

static int session_name_valid_local(const char *name) {
    uint32_t i = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    while (name[i] != '\0') {
        char ch = name[i];

        if (i >= SESSION_NAME_MAX) {
            return 0;
        }
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '.')) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int session_build_path_local(char *out, uint32_t out_size, const char *name, const char *ext) {
    int len;

    if (!session_name_valid_local(name) || out == NULL || ext == NULL) {
        return 0;
    }
    len = snprintf(out, out_size, "%s/%s.%s", g_session_dir, name, ext);
    return len > 0 && (uint32_t)len < out_size;
}

static int session_ensure_dir_local(const char *path) {
    int fd = opendir(path);

    if (fd >= 0) {
        close((uint32_t)fd);
        return 1;
    }
    return mkdir(path) == 0;
}

static int session_ensure_store_local(void) {
    return session_ensure_dir_local("/SYSTEM") &&
           session_ensure_dir_local("/SYSTEM/SESSION") &&
           session_ensure_dir_local(g_session_dir);
}

static int session_write_text_local(int fd, const char *text) {
    uint32_t len = str_len_local(text);

    return write(fd, text, len) == (int)len;
}

static int session_write_line2_local(int fd, const char *a, const char *b) {
    return session_write_text_local(fd, a) &&
           session_write_text_local(fd, b != NULL ? b : "") &&
           session_write_text_local(fd, "\n");
}

static int session_env_name_valid_local(const char *env) {
    uint32_t i = 0;

    if (env == NULL || env[0] == '\0') {
        return 0;
    }
    while (env[i] != '\0' && env[i] != '=') {
        char ch = env[i];

        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9' && i != 0u) ||
              ch == '_')) {
            return 0;
        }
        i++;
    }
    return i != 0u && env[i] == '=';
}

static int session_write_env_script_local(int fd) {
    uint32_t i;

    for (i = 0; environ != NULL && environ[i] != NULL; i++) {
        if (!session_env_name_valid_local(environ[i])) {
            continue;
        }
        if (str_len_local(environ[i]) + 8u >= 63u) {
            continue;
        }
        if (!session_write_text_local(fd, "export ") ||
            !session_write_text_local(fd, environ[i]) ||
            !session_write_text_local(fd, "\n")) {
            return 0;
        }
    }
    return 1;
}

static int session_write_env_image_local(int fd) {
    uint32_t i;

    for (i = 0; environ != NULL && environ[i] != NULL; i++) {
        if (!session_write_text_local(fd, "env ") ||
            !session_write_text_local(fd, environ[i]) ||
            !session_write_text_local(fd, "\n")) {
            return 0;
        }
    }
    return 1;
}

static int session_write_mounts_image_local(int fd) {
    struct syscall_mount_info info;
    uint32_t i;
    char line[SESSION_LINE_MAX];

    for (i = 0; mount_query(i, &info) > 0; i++) {
        if (info.kind == NEX_MOUNT_INFO_DEVFS || info.kind == NEX_MOUNT_INFO_PROCFS) {
            if (snprintf(line, sizeof(line), "mount /%s virtual\n", info.target) < 0) {
                return 0;
            }
        } else if (info.source_known) {
            if (info.part_index == 0xffffffffu) {
                if (snprintf(line,
                             sizeof(line),
                             "mount /%s %s /dev/disk%u\n",
                             info.target,
                             info.kind == NEX_MOUNT_INFO_FAT32 ? "fat32" : "nxfs",
                             info.disk_index) < 0) {
                    return 0;
                }
            } else {
                if (snprintf(line,
                             sizeof(line),
                             "mount /%s %s /dev/disk%up%u\n",
                             info.target,
                             info.kind == NEX_MOUNT_INFO_FAT32 ? "fat32" : "nxfs",
                             info.disk_index,
                             info.part_index + 1u) < 0) {
                    return 0;
                }
            }
        } else if (snprintf(line,
                            sizeof(line),
                            "mount /%s %s unknown\n",
                            info.target,
                            info.kind == NEX_MOUNT_INFO_FAT32 ? "fat32" : "nxfs") < 0) {
            return 0;
        }
        if (!session_write_text_local(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int session_save_local(const char *name) {
    char cwd[CMD_PATH_MAX];
    char image_path[CMD_PATH_MAX];
    char script_path[CMD_PATH_MAX];
    int image_fd;
    int script_fd;

    if (!session_name_valid_local(name)) {
        write_err_str("session: invalid name\n");
        return 1;
    }
    if (!session_ensure_store_local() ||
        !session_build_path_local(image_path, sizeof(image_path), name, "simg") ||
        !session_build_path_local(script_path, sizeof(script_path), name, "ush")) {
        write_err_str("session: store unavailable\n");
        return 1;
    }
    if (getcwd(cwd, sizeof(cwd)) < 0) {
        copy_line_local(cwd, "/", sizeof(cwd));
    }

    image_fd = open(image_path, O_CREAT | O_TRUNC);
    if (image_fd < 0) {
        write_err_str("session: image write failed\n");
        return 1;
    }
    script_fd = open(script_path, O_CREAT | O_TRUNC);
    if (script_fd < 0) {
        close((uint32_t)image_fd);
        write_err_str("session: script write failed\n");
        return 1;
    }

    if (!session_write_text_local(image_fd, "# NexOS Session Image v1\n") ||
        !session_write_line2_local(image_fd, "name ", name) ||
        !session_write_line2_local(image_fd, "cwd ", cwd) ||
        !session_write_line2_local(image_fd, "restore ", script_path) ||
        !session_write_env_image_local(image_fd) ||
        !session_write_mounts_image_local(image_fd) ||
        !session_write_text_local(script_fd, "# NexOS session restore script\n") ||
        !session_write_line2_local(script_fd, "cd ", cwd) ||
        !session_write_env_script_local(script_fd)) {
        close((uint32_t)script_fd);
        close((uint32_t)image_fd);
        write_err_str("session: write failed\n");
        return 1;
    }
    close((uint32_t)script_fd);
    close((uint32_t)image_fd);
    write_str("saved session ");
    write_str(name);
    write_str("\nimage: ");
    write_str(image_path);
    write_str("\nrestore: ");
    write_str(script_path);
    write_str("\n");
    return 0;
}

static int session_info_file_local(const char *path) {
    char line[SESSION_LINE_MAX];
    int fd = open(path, 0);

    if (fd < 0) {
        write_err_str("session: image open failed\n");
        return 1;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        write_str(line);
        write_str("\n");
    }
    close((uint32_t)fd);
    return 0;
}

static int session_info_local(const char *name) {
    char image_path[CMD_PATH_MAX];

    if (!session_build_path_local(image_path, sizeof(image_path), name, "simg")) {
        write_err_str("session: invalid name\n");
        return 1;
    }
    return session_info_file_local(image_path);
}

static int session_load_local(const char *name) {
    char script_path[CMD_PATH_MAX];
    int fd;

    if (!session_build_path_local(script_path, sizeof(script_path), name, "ush")) {
        write_err_str("session: invalid name\n");
        return 1;
    }
    fd = open(script_path, 0);
    if (fd < 0) {
        write_err_str("session: not found\n");
        return 1;
    }
    close((uint32_t)fd);
    write_str("restore with: source ");
    write_str(script_path);
    write_str("\n");
    return 0;
}

static int session_list_local(void) {
    struct syscall_dirent entry;
    int fd = opendir(g_session_dir);
    int listed = 0;

    if (fd < 0) {
        write_str("<empty>\n");
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        uint32_t len = str_len_local(entry.name);

        if (len > 5u &&
            entry.name[len - 5u] == '.' &&
            (entry.name[len - 4u] == 'S' || entry.name[len - 4u] == 's') &&
            (entry.name[len - 3u] == 'I' || entry.name[len - 3u] == 'i') &&
            (entry.name[len - 2u] == 'M' || entry.name[len - 2u] == 'm') &&
            (entry.name[len - 1u] == 'G' || entry.name[len - 1u] == 'g')) {
            write_str(entry.name);
            write_str("\n");
            listed = 1;
        }
    }
    close((uint32_t)fd);
    if (!listed) {
        write_str("<empty>\n");
    }
    return 0;
}

static int service_name_valid_local(const char *name) {
    uint32_t i = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    while (name[i] != '\0') {
        char ch = name[i];

        if (i >= SERVICE_NAME_MAX) {
            return 0;
        }
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '.')) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int service_ensure_store_local(void) {
    return session_ensure_dir_local("/SYSTEM") &&
           session_ensure_dir_local(g_service_dir);
}

static int service_build_path_local(char *out, uint32_t out_size, const char *name, const char *ext) {
    int len;

    if (!service_name_valid_local(name) || out == NULL || ext == NULL) {
        return 0;
    }
    len = snprintf(out, out_size, "%s/%s.%s", g_service_dir, name, ext);
    return len > 0 && (uint32_t)len < out_size;
}

static int service_entry_has_ext_local(const char *name, const char *ext) {
    uint32_t len = str_len_local(name);
    uint32_t ext_len = str_len_local(ext);
    uint32_t i;

    if (len <= ext_len + 1u || name[len - ext_len - 1u] != '.') {
        return 0;
    }
    for (i = 0; i < ext_len; i++) {
        char a = name[len - ext_len + i];
        char b = ext[i];

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static int service_name_from_entry_local(const char *entry, char *out, uint32_t out_size) {
    uint32_t len = str_len_local(entry);
    uint32_t name_len;
    uint32_t i;

    if (!service_entry_has_ext_local(entry, "svc") || out == NULL || out_size == 0u) {
        return 0;
    }
    name_len = len - 4u;
    if (name_len == 0u || name_len >= out_size) {
        return 0;
    }
    for (i = 0; i < name_len; i++) {
        out[i] = entry[i];
    }
    out[name_len] = '\0';
    return service_name_valid_local(out);
}

static int service_join_command_local(int argc, char **argv, int start, char *out, uint32_t out_size) {
    uint32_t used = 0;
    int i;

    if (out == NULL || out_size == 0u || start >= argc) {
        return 0;
    }
    out[0] = '\0';
    for (i = start; i < argc; i++) {
        uint32_t len = str_len_local(argv[i]);

        if (len == 0u) {
            continue;
        }
        if (used + len + (used != 0u ? 1u : 0u) + 1u >= out_size) {
            return 0;
        }
        if (used != 0u) {
            out[used++] = ' ';
        }
        for (uint32_t j = 0; j < len; j++) {
            out[used++] = argv[i][j];
        }
        out[used] = '\0';
    }
    return used != 0u;
}

static int service_meta_value_local(const char *line, const char *key, char *out, uint32_t out_size) {
    uint32_t key_len = str_len_local(key);
    uint32_t i;

    if (line == NULL || key == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    for (i = 0; i < key_len; i++) {
        if (line[i] != key[i]) {
            return 0;
        }
    }
    if (line[key_len] != ' ') {
        return 0;
    }
    copy_line_local(out, line + key_len + 1u, out_size);
    return 1;
}

static int service_load_def_local(const char *name, char *command, uint32_t command_size, int *enabled) {
    char path[CMD_PATH_MAX];
    char line[SERVICE_LINE_MAX];
    int fd;
    int saw_command = 0;

    if (!service_build_path_local(path, sizeof(path), name, "svc")) {
        return 0;
    }
    fd = open(path, 0);
    if (fd < 0) {
        return 0;
    }
    if (command != NULL && command_size != 0u) {
        command[0] = '\0';
    }
    if (enabled != NULL) {
        *enabled = 0;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        char value[SERVICE_COMMAND_MAX];

        if (service_meta_value_local(line, "command", value, sizeof(value))) {
            if (command != NULL && command_size != 0u) {
                copy_line_local(command, value, command_size);
            }
            saw_command = value[0] != '\0';
        } else if (service_meta_value_local(line, "enabled", value, sizeof(value)) && enabled != NULL) {
            *enabled = streq_local(value, "1") || streq_ignore_case_local(value, "yes") ||
                       streq_ignore_case_local(value, "true") || streq_ignore_case_local(value, "on");
        }
    }
    close((uint32_t)fd);
    return saw_command;
}

static int service_write_def_local(const char *name, const char *command, int enabled) {
    char path[CMD_PATH_MAX];
    int fd;

    if (!service_ensure_store_local() ||
        !service_build_path_local(path, sizeof(path), name, "svc")) {
        write_err_str("service: store unavailable\n");
        return 1;
    }
    fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        write_err_str("service: write failed\n");
        return 1;
    }
    if (!session_write_text_local(fd, "# NexOS Service v1\n") ||
        !session_write_line2_local(fd, "name ", name) ||
        !session_write_line2_local(fd, "enabled ", enabled ? "1" : "0") ||
        !session_write_line2_local(fd, "command ", command)) {
        close((uint32_t)fd);
        write_err_str("service: write failed\n");
        return 1;
    }
    close((uint32_t)fd);
    return 0;
}

static void service_capture_pids_local(uint32_t *out) {
    struct syscall_process_info info;
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        out[i] = 0u;
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0) {
            out[i] = info.pid;
        }
    }
}

static int service_pid_seen_local(const uint32_t *snapshot, uint32_t pid) {
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (snapshot[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static uint32_t service_find_new_pid_local(const uint32_t *snapshot) {
    struct syscall_process_info info;
    uint32_t i;

    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) <= 0) {
            continue;
        }
        if (info.pid != 0u && !service_pid_seen_local(snapshot, info.pid)) {
            return info.pid;
        }
    }
    return 0u;
}

static int service_pid_alive_local(uint32_t pid) {
    struct syscall_process_info info;
    uint32_t i;

    if (pid == 0u) {
        return 0;
    }
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++) {
        if (proc_query(NEX_PROC_QUERY_ALL, i, &info) > 0 &&
            info.pid == pid &&
            info.state != NEX_PROC_STATE_EXITED) {
            return 1;
        }
    }
    return 0;
}

static int service_read_run_pid_local(const char *name, uint32_t *pid_out) {
    char path[CMD_PATH_MAX];
    char line[SERVICE_LINE_MAX];
    int fd;

    if (pid_out == NULL ||
        !service_build_path_local(path, sizeof(path), name, "run")) {
        return 0;
    }
    fd = open(path, 0);
    if (fd < 0) {
        return 0;
    }
    *pid_out = 0u;
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        char value[32];

        if (service_meta_value_local(line, "pid", value, sizeof(value))) {
            (void)parse_u32_local(value, pid_out);
            break;
        }
    }
    close((uint32_t)fd);
    return *pid_out != 0u;
}

static int service_write_run_local(const char *name, uint32_t pid, const char *command) {
    char path[CMD_PATH_MAX];
    char pid_text[16];
    int fd;

    if (!service_ensure_store_local() ||
        !service_build_path_local(path, sizeof(path), name, "run")) {
        return 0;
    }
    fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        return 0;
    }
    if (snprintf(pid_text, sizeof(pid_text), "%u", pid) < 0 ||
        !session_write_text_local(fd, "# NexOS Service Runtime v1\n") ||
        !session_write_line2_local(fd, "name ", name) ||
        !session_write_line2_local(fd, "pid ", pid_text) ||
        !session_write_line2_local(fd, "command ", command)) {
        close((uint32_t)fd);
        return 0;
    }
    close((uint32_t)fd);
    return 1;
}

static void service_remove_run_local(const char *name) {
    char path[CMD_PATH_MAX];

    if (service_build_path_local(path, sizeof(path), name, "run")) {
        (void)remove(path);
    }
}

static int service_define_local(const char *name, int argc, char **argv, int start) {
    char command[SERVICE_COMMAND_MAX];

    if (!service_name_valid_local(name)) {
        write_err_str("service: invalid name\n");
        return 1;
    }
    if (!service_join_command_local(argc, argv, start, command, sizeof(command))) {
        write_err_str("service: command too long or empty\n");
        return 1;
    }
    if (service_write_def_local(name, command, 0) != 0) {
        return 1;
    }
    write_str("defined service ");
    write_str(name);
    write_str("\n");
    return 0;
}

static int service_set_enabled_local(const char *name, int enabled) {
    char command[SERVICE_COMMAND_MAX];
    int old_enabled;

    if (!service_load_def_local(name, command, sizeof(command), &old_enabled)) {
        write_err_str("service: not found\n");
        return 1;
    }
    (void)old_enabled;
    if (service_write_def_local(name, command, enabled) != 0) {
        return 1;
    }
    write_str(enabled ? "enabled " : "disabled ");
    write_str(name);
    write_str("\n");
    return 0;
}

static int service_start_local(const char *name, int quiet) {
    char command[SERVICE_COMMAND_MAX];
    uint32_t before[NEX_PROC_SLOTS_MAX];
    uint32_t pid = 0u;
    uint32_t start_tick;
    int enabled;
    int rc;

    if (!service_load_def_local(name, command, sizeof(command), &enabled)) {
        write_err_str("service: not found\n");
        return 1;
    }
    if (service_read_run_pid_local(name, &pid) && service_pid_alive_local(pid)) {
        if (!quiet) {
            write_str("service ");
            write_str(name);
            write_str(" already running pid=");
            write_dec(pid);
            write_str("\n");
        }
        return 0;
    }
    service_capture_pids_local(before);
    rc = spawn(command, SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND);
    if (rc != 0) {
        write_err_str("service: start failed rc=");
        eprintf("%d\n", rc);
        return 1;
    }
    start_tick = ticks();
    while ((uint32_t)(ticks() - start_tick) < 100u) {
        pid = service_find_new_pid_local(before);
        if (pid != 0u) {
            break;
        }
        yield();
    }
    if (pid == 0u) {
        write_err_str("service: started but pid unknown\n");
        service_remove_run_local(name);
        return 1;
    }
    if (!service_write_run_local(name, pid, command)) {
        write_err_str("service: runtime write failed\n");
        return 1;
    }
    if (!quiet) {
        write_str("started service ");
        write_str(name);
        write_str(" pid=");
        write_dec(pid);
        write_str("\n");
    }
    return 0;
}

static int service_stop_local(const char *name, int quiet) {
    uint32_t pid = 0u;

    if (!service_read_run_pid_local(name, &pid)) {
        if (!quiet) {
            write_str("service ");
            write_str(name);
            write_str(" not running\n");
        }
        return 0;
    }
    if (service_pid_alive_local(pid) && kill(pid) <= 0) {
        write_err_str("service: stop failed\n");
        return 1;
    }
    service_remove_run_local(name);
    if (!quiet) {
        write_str("stopped service ");
        write_str(name);
        write_str("\n");
    }
    return 0;
}

static int service_info_local(const char *name) {
    char command[SERVICE_COMMAND_MAX];
    uint32_t pid = 0u;
    int enabled;

    if (!service_load_def_local(name, command, sizeof(command), &enabled)) {
        write_err_str("service: not found\n");
        return 1;
    }
    write_str("name: ");
    write_str(name);
    write_str("\nenabled: ");
    write_str(enabled ? "1" : "0");
    write_str("\ncommand: ");
    write_str(command);
    write_str("\nstate: ");
    if (service_read_run_pid_local(name, &pid) && service_pid_alive_local(pid)) {
        write_str("running\npid: ");
        write_dec(pid);
        write_str("\n");
    } else {
        service_remove_run_local(name);
        write_str("stopped\n");
    }
    return 0;
}

static int service_list_local(void) {
    struct syscall_dirent entry;
    int fd = opendir(g_service_dir);
    int listed = 0;

    write_str("SERVICE                EN PID    STATE    COMMAND\n");
    if (fd < 0) {
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char name[SERVICE_NAME_MAX + 1u];
        char command[SERVICE_COMMAND_MAX];
        uint32_t pid = 0u;
        int enabled = 0;
        int running;

        if (!service_name_from_entry_local(entry.name, name, sizeof(name))) {
            continue;
        }
        if (!service_load_def_local(name, command, sizeof(command), &enabled)) {
            continue;
        }
        running = service_read_run_pid_local(name, &pid) && service_pid_alive_local(pid);
        if (!running) {
            service_remove_run_local(name);
        }
        write_text_padded(name, 22u);
        write_str(enabled ? "Y  " : "N  ");
        if (running) {
            write_dec(pid);
        } else {
            write_str("-");
        }
        write_str(pid < 10u ? "      " : (pid < 100u ? "     " : (pid < 1000u ? "    " : "   ")));
        write_str(running ? "running  " : "stopped  ");
        write_str(command);
        write_str("\n");
        listed = 1;
    }
    close((uint32_t)fd);
    if (!listed) {
        write_str("<empty>\n");
    }
    return 0;
}

static int service_boot_local(void) {
    struct syscall_dirent entry;
    int fd = opendir(g_service_dir);
    int rc = 0;

    if (fd < 0) {
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char name[SERVICE_NAME_MAX + 1u];
        char command[SERVICE_COMMAND_MAX];
        int enabled = 0;

        if (!service_name_from_entry_local(entry.name, name, sizeof(name))) {
            continue;
        }
        if (!service_load_def_local(name, command, sizeof(command), &enabled) || !enabled) {
            continue;
        }
        if (service_start_local(name, 0) != 0) {
            rc = 1;
        }
    }
    close((uint32_t)fd);
    return rc;
}

int cmd_session(int argc, char **argv) {
    if (argc < 2 || argv[1] == NULL) {
        write_err_usage("session", " <save|load|list|info> [name]\n");
        return 1;
    }
    if (streq_ignore_case_local(argv[1], "save")) {
        if (argc != 3) {
            write_err_usage("session save", " <name>\n");
            return 1;
        }
        return session_save_local(argv[2]);
    }
    if (streq_ignore_case_local(argv[1], "load")) {
        if (argc != 3) {
            write_err_usage("session load", " <name>\n");
            return 1;
        }
        return session_load_local(argv[2]);
    }
    if (streq_ignore_case_local(argv[1], "info")) {
        if (argc != 3) {
            write_err_usage("session info", " <name>\n");
            return 1;
        }
        return session_info_local(argv[2]);
    }
    if (streq_ignore_case_local(argv[1], "list")) {
        if (argc != 2) {
            write_err_usage("session list", "\n");
            return 1;
        }
        return session_list_local();
    }
    write_err_usage("session", " <save|load|list|info> [name]\n");
    return 1;
}

int cmd_service(int argc, char **argv) {
    if (argc < 2 || argv[1] == NULL) {
        write_err_usage("service", " <define|list|info|start|stop|restart|enable|disable|boot> ...\n");
        return 1;
    }
    if (streq_ignore_case_local(argv[1], "define")) {
        if (argc < 4) {
            write_err_usage("service define", " <name> <command> [args...]\n");
            return 1;
        }
        return service_define_local(argv[2], argc, argv, 3);
    }
    if (streq_ignore_case_local(argv[1], "list")) {
        if (argc != 2) {
            write_err_usage("service list", "\n");
            return 1;
        }
        return service_list_local();
    }
    if (streq_ignore_case_local(argv[1], "info") ||
        streq_ignore_case_local(argv[1], "status")) {
        if (argc != 3) {
            write_err_usage("service info", " <name>\n");
            return 1;
        }
        return service_info_local(argv[2]);
    }
    if (streq_ignore_case_local(argv[1], "start")) {
        if (argc != 3) {
            write_err_usage("service start", " <name>\n");
            return 1;
        }
        return service_start_local(argv[2], 0);
    }
    if (streq_ignore_case_local(argv[1], "stop")) {
        if (argc != 3) {
            write_err_usage("service stop", " <name>\n");
            return 1;
        }
        return service_stop_local(argv[2], 0);
    }
    if (streq_ignore_case_local(argv[1], "restart")) {
        if (argc != 3) {
            write_err_usage("service restart", " <name>\n");
            return 1;
        }
        if (service_stop_local(argv[2], 1) != 0) {
            return 1;
        }
        return service_start_local(argv[2], 0);
    }
    if (streq_ignore_case_local(argv[1], "enable")) {
        if (argc != 3) {
            write_err_usage("service enable", " <name>\n");
            return 1;
        }
        return service_set_enabled_local(argv[2], 1);
    }
    if (streq_ignore_case_local(argv[1], "disable")) {
        if (argc != 3) {
            write_err_usage("service disable", " <name>\n");
            return 1;
        }
        return service_set_enabled_local(argv[2], 0);
    }
    if (streq_ignore_case_local(argv[1], "boot")) {
        if (argc != 2) {
            write_err_usage("service boot", "\n");
            return 1;
        }
        return service_boot_local();
    }
    write_err_usage("service", " <define|list|info|start|stop|restart|enable|disable|boot> ...\n");
    return 1;
}
