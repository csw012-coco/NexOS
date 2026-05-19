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

static int service_parse_interval_local(const char *text, uint32_t *ticks_out) {
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

static int service_reconcile_local(int quiet) {
    struct syscall_dirent entry;
    int fd = opendir(g_service_dir);
    int rc = 0;

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
        if (!service_load_def_local(name, command, sizeof(command), &enabled) || !enabled) {
            continue;
        }
        running = service_read_run_pid_local(name, &pid) && service_pid_alive_local(pid);
        if (!running) {
            service_remove_run_local(name);
            if (!quiet) {
                write_str("supervisor: starting ");
                write_str(name);
                write_str("\n");
            }
            if (service_start_local(name, quiet) != 0) {
                rc = 1;
            }
        }
    }
    close((uint32_t)fd);
    return rc;
}

static int service_supervise_local(uint32_t interval_ticks) {
    if (interval_ticks == 0u) {
        interval_ticks = 1000u;
    }
    write_str("service supervisor interval=");
    write_dec(interval_ticks);
    write_str("ms\n");
    for (;;) {
        (void)service_reconcile_local(1);
        sleep(interval_ticks);
    }
    return 0;
}

enum {
    CONFIG_KEY_MAX = 48u,
    CONFIG_VALUE_MAX = 96u,
    CONFIG_LINE_MAX = 160u,
    CONFIG_LINE_STORE_MAX = 40u,
    CONFIG_LIST_MAX = 32u
};

struct config_schema_entry {
    const char *key;
    const char *type;
    const char *detail;
};

struct config_entry_local {
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VALUE_MAX];
    const char *source;
    const char *type;
};

static const char *g_config_system_path = "/SYSTEM/CONFIG/NOS.CFG";
static const char *g_config_user_path = "/HOME/CONFIG.CFG";
static const char *g_config_runtime_path = "/SYSTEM/CONFIG/RUNTIME.CFG";

static char g_config_lines[CONFIG_LINE_STORE_MAX][CONFIG_LINE_MAX];
static uint32_t g_config_line_count;
static struct config_entry_local g_config_entries[CONFIG_LIST_MAX];
static uint32_t g_config_entry_count;

static const struct config_schema_entry g_config_schema[] = {
    {"init", "path", "boot init script path"},
    {"ring3_smoke", "bool", "run ring3 smoke test at boot"},
    {"mouse.cursor", "bool", "show framebuffer mouse cursor on console"},
    {"function_recursion_limit", "bool", "limit ush function recursion"},
    {"shell.prompt", "string", "interactive shell prompt"},
    {"shell.history_size", "int", "interactive shell history slots"},
    {"proc.max_jobs", "int", "background job limit"},
    {"service.autostart", "bool", "start enabled services at boot"},
    {"log.level", "enum", "debug|info|warn|error"}
};

static void config_trim_local(char *text) {
    uint32_t start = 0;
    uint32_t end = str_len_local(text);
    uint32_t out = 0;

    while (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n') {
        start++;
    }
    while (end > start &&
           (text[end - 1u] == ' ' || text[end - 1u] == '\t' ||
            text[end - 1u] == '\r' || text[end - 1u] == '\n')) {
        end--;
    }
    while (start < end) {
        text[out++] = text[start++];
    }
    text[out] = '\0';
}

static int config_key_valid_local(const char *key) {
    uint32_t i = 0;

    if (key == NULL || key[0] == '\0') {
        return 0;
    }
    while (key[i] != '\0') {
        char ch = key[i];

        if (i >= CONFIG_KEY_MAX - 1u) {
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

static int config_parse_line_local(const char *line,
                                   char *key,
                                   uint32_t key_size,
                                   char *value,
                                   uint32_t value_size) {
    uint32_t pos = 0;
    uint32_t key_pos = 0;
    uint32_t value_pos = 0;

    if (line == NULL || key == NULL || value == NULL || key_size == 0u || value_size == 0u) {
        return 0;
    }
    key[0] = '\0';
    value[0] = '\0';
    while (line[pos] == ' ' || line[pos] == '\t') {
        pos++;
    }
    if (line[pos] == '\0' || line[pos] == '#') {
        return 0;
    }
    while (line[pos] != '\0' && line[pos] != '=') {
        if (key_pos + 1u < key_size) {
            key[key_pos++] = line[pos];
        }
        pos++;
    }
    if (line[pos] != '=') {
        return 0;
    }
    key[key_pos] = '\0';
    pos++;
    while (line[pos] != '\0') {
        if (value_pos + 1u < value_size) {
            value[value_pos++] = line[pos];
        }
        pos++;
    }
    value[value_pos] = '\0';
    config_trim_local(key);
    config_trim_local(value);
    return config_key_valid_local(key);
}

static const struct config_schema_entry *config_schema_find_local(const char *key) {
    uint32_t i;

    for (i = 0; i < sizeof(g_config_schema) / sizeof(g_config_schema[0]); i++) {
        if (streq_local(g_config_schema[i].key, key)) {
            return &g_config_schema[i];
        }
    }
    return NULL;
}

static const char *config_type_for_key_local(const char *key) {
    const struct config_schema_entry *schema = config_schema_find_local(key);

    return schema != NULL ? schema->type : "string";
}

static int config_value_bool_local(const char *value) {
    return streq_local(value, "0") || streq_local(value, "1") ||
           streq_ignore_case_local(value, "true") || streq_ignore_case_local(value, "false") ||
           streq_ignore_case_local(value, "yes") || streq_ignore_case_local(value, "no") ||
           streq_ignore_case_local(value, "on") || streq_ignore_case_local(value, "off");
}

static int config_value_int_local(const char *value) {
    char *end = NULL;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    (void)strtoul(value, &end, 10);
    return end != value && end != NULL && *end == '\0';
}

static int config_value_valid_local(const char *key, const char *value) {
    const char *type = config_type_for_key_local(key);

    if (value == NULL) {
        return 0;
    }
    if (streq_local(type, "bool")) {
        return config_value_bool_local(value);
    }
    if (streq_local(type, "int")) {
        return config_value_int_local(value);
    }
    if (streq_local(type, "path")) {
        return value[0] != '\0';
    }
    if (streq_local(type, "enum") && streq_local(key, "log.level")) {
        return streq_ignore_case_local(value, "debug") ||
               streq_ignore_case_local(value, "info") ||
               streq_ignore_case_local(value, "warn") ||
               streq_ignore_case_local(value, "error");
    }
    return value[0] != '\0';
}

static const char *config_layer_path_local(const char *layer) {
    if (layer != NULL && streq_ignore_case_local(layer, "--runtime")) {
        return g_config_runtime_path;
    }
    if (layer != NULL && streq_ignore_case_local(layer, "--system")) {
        return g_config_system_path;
    }
    return g_config_user_path;
}

static const char *config_layer_name_local(const char *path) {
    if (streq_local(path, g_config_runtime_path)) {
        return "runtime";
    }
    if (streq_local(path, g_config_user_path)) {
        return "user";
    }
    return "system";
}

static int config_ensure_store_for_path_local(const char *path) {
    if (streq_local(path, g_config_user_path)) {
        (void)mkdir("/HOME");
        return 1;
    }
    (void)mkdir("/SYSTEM");
    return session_ensure_dir_local("/SYSTEM/CONFIG");
}

static int config_lookup_in_file_local(const char *path,
                                       const char *wanted_key,
                                       char *value,
                                       uint32_t value_size) {
    char line[CONFIG_LINE_MAX];
    char key[CONFIG_KEY_MAX];
    int fd = open(path, 0);

    if (fd < 0) {
        return 0;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        if (config_parse_line_local(line, key, sizeof(key), value, value_size) &&
            streq_local(key, wanted_key)) {
            close((uint32_t)fd);
            return 1;
        }
    }
    close((uint32_t)fd);
    return 0;
}

static const char *config_lookup_effective_local(const char *key, char *value, uint32_t value_size) {
    if (config_lookup_in_file_local(g_config_runtime_path, key, value, value_size)) {
        return g_config_runtime_path;
    }
    if (config_lookup_in_file_local(g_config_user_path, key, value, value_size)) {
        return g_config_user_path;
    }
    if (config_lookup_in_file_local(g_config_system_path, key, value, value_size)) {
        return g_config_system_path;
    }
    return NULL;
}

static int config_load_lines_local(const char *path) {
    int fd = open(path, 0);

    g_config_line_count = 0;
    if (fd < 0) {
        return 1;
    }
    while (g_config_line_count < CONFIG_LINE_STORE_MAX &&
           read_line((uint32_t)fd,
                     g_config_lines[g_config_line_count],
                     sizeof(g_config_lines[g_config_line_count])) != 0u) {
        g_config_line_count++;
    }
    close((uint32_t)fd);
    return 1;
}

static int config_write_key_local(const char *path, const char *key, const char *value, int unset) {
    int fd;
    int replaced = 0;
    uint32_t i;

    if (!config_ensure_store_for_path_local(path) || !config_load_lines_local(path)) {
        write_err_str("config: store unavailable\n");
        return 1;
    }
    fd = open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        write_err_str("config: write failed\n");
        return 1;
    }
    for (i = 0; i < g_config_line_count; i++) {
        char parsed_key[CONFIG_KEY_MAX];
        char parsed_value[CONFIG_VALUE_MAX];

        if (config_parse_line_local(g_config_lines[i],
                                    parsed_key,
                                    sizeof(parsed_key),
                                    parsed_value,
                                    sizeof(parsed_value)) &&
            streq_local(parsed_key, key)) {
            replaced = 1;
            if (unset) {
                continue;
            }
            if (!session_write_text_local(fd, key) ||
                !session_write_text_local(fd, "=") ||
                !session_write_text_local(fd, value != NULL ? value : "")) {
                close((uint32_t)fd);
                write_err_str("config: write failed\n");
                return 1;
            }
            if (!session_write_text_local(fd, "\n")) {
                close((uint32_t)fd);
                write_err_str("config: write failed\n");
                return 1;
            }
            continue;
        }
        if (!session_write_line2_local(fd, g_config_lines[i], "")) {
            close((uint32_t)fd);
            write_err_str("config: write failed\n");
            return 1;
        }
    }
    if (!unset && !replaced) {
        if (!session_write_text_local(fd, key) ||
            !session_write_text_local(fd, "=") ||
            !session_write_text_local(fd, value) ||
            !session_write_text_local(fd, "\n")) {
            close((uint32_t)fd);
            write_err_str("config: write failed\n");
            return 1;
        }
    }
    close((uint32_t)fd);
    return 0;
}

static int config_join_value_local(int argc, char **argv, int start, char *out, uint32_t out_size) {
    uint32_t used = 0;
    int i;

    if (out == NULL || out_size == 0u || start >= argc) {
        return 0;
    }
    out[0] = '\0';
    for (i = start; i < argc; i++) {
        uint32_t len = str_len_local(argv[i]);
        uint32_t j;

        if (used + len + (used != 0u ? 1u : 0u) + 1u >= out_size) {
            return 0;
        }
        if (used != 0u) {
            out[used++] = ' ';
        }
        for (j = 0; j < len; j++) {
            out[used++] = argv[i][j];
        }
        out[used] = '\0';
    }
    return used != 0u;
}

static void config_list_add_local(const char *key, const char *value, const char *source) {
    uint32_t i;

    for (i = 0; i < g_config_entry_count; i++) {
        if (streq_local(g_config_entries[i].key, key)) {
            copy_line_local(g_config_entries[i].value, value, sizeof(g_config_entries[i].value));
            g_config_entries[i].source = source;
            g_config_entries[i].type = config_type_for_key_local(key);
            return;
        }
    }
    if (g_config_entry_count >= CONFIG_LIST_MAX) {
        return;
    }
    copy_line_local(g_config_entries[g_config_entry_count].key, key, sizeof(g_config_entries[g_config_entry_count].key));
    copy_line_local(g_config_entries[g_config_entry_count].value, value, sizeof(g_config_entries[g_config_entry_count].value));
    g_config_entries[g_config_entry_count].source = source;
    g_config_entries[g_config_entry_count].type = config_type_for_key_local(key);
    g_config_entry_count++;
}

static void config_list_load_file_local(const char *path, const char *source) {
    char line[CONFIG_LINE_MAX];
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VALUE_MAX];
    int fd = open(path, 0);

    if (fd < 0) {
        return;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        if (config_parse_line_local(line, key, sizeof(key), value, sizeof(value))) {
            config_list_add_local(key, value, source);
        }
    }
    close((uint32_t)fd);
}

static int config_list_local(void) {
    uint32_t i;

    g_config_entry_count = 0;
    config_list_load_file_local(g_config_system_path, "system");
    config_list_load_file_local(g_config_user_path, "user");
    config_list_load_file_local(g_config_runtime_path, "runtime");

    write_str("KEY                         SOURCE   TYPE    VALUE\n");
    for (i = 0; i < g_config_entry_count; i++) {
        write_text_padded(g_config_entries[i].key, 28u);
        write_text_padded(g_config_entries[i].source, 9u);
        write_text_padded(g_config_entries[i].type, 8u);
        write_str(g_config_entries[i].value);
        write_str("\n");
    }
    if (g_config_entry_count == 0u) {
        write_str("<empty>\n");
    }
    return 0;
}

static int config_schema_local(int argc, char **argv) {
    uint32_t i;

    if (argc == 3) {
        const struct config_schema_entry *schema = config_schema_find_local(argv[2]);

        if (schema == NULL) {
            write_str(argv[2]);
            write_str(" type=string detail=unspecified\n");
            return 0;
        }
        write_str(schema->key);
        write_str(" type=");
        write_str(schema->type);
        write_str(" detail=");
        write_str(schema->detail);
        write_str("\n");
        return 0;
    }
    write_str("KEY                         TYPE    DETAIL\n");
    for (i = 0; i < sizeof(g_config_schema) / sizeof(g_config_schema[0]); i++) {
        write_text_padded(g_config_schema[i].key, 28u);
        write_text_padded(g_config_schema[i].type, 8u);
        write_str(g_config_schema[i].detail);
        write_str("\n");
    }
    return 0;
}

static int config_validate_file_local(const char *path, const char *source) {
    char line[CONFIG_LINE_MAX];
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VALUE_MAX];
    uint32_t line_no = 0;
    int fd = open(path, 0);
    int ok = 1;

    if (fd < 0) {
        return 1;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        line_no++;
        if (!config_parse_line_local(line, key, sizeof(key), value, sizeof(value))) {
            continue;
        }
        if (!config_value_valid_local(key, value)) {
            write_str(source);
            write_str(":");
            write_dec(line_no);
            write_str(": invalid ");
            write_str(key);
            write_str("=");
            write_str(value);
            write_str(" type=");
            write_str(config_type_for_key_local(key));
            write_str("\n");
            ok = 0;
        }
    }
    close((uint32_t)fd);
    return ok;
}

static int config_validate_local(void) {
    int ok = 1;

    ok = config_validate_file_local(g_config_system_path, "system") && ok;
    ok = config_validate_file_local(g_config_user_path, "user") && ok;
    ok = config_validate_file_local(g_config_runtime_path, "runtime") && ok;
    write_str(ok ? "config: valid\n" : "config: invalid\n");
    return ok ? 0 : 1;
}

int cmd_config(int argc, char **argv) {
    const char *path;
    int argi = 2;

    if (argc < 2 || argv[1] == NULL) {
        write_err_usage("config", " <get|set|unset|list|source|schema|validate> ...\n");
        return 1;
    }
    if (streq_ignore_case_local(argv[1], "list")) {
        if (argc != 2) {
            write_err_usage("config list", "\n");
            return 1;
        }
        return config_list_local();
    }
    if (streq_ignore_case_local(argv[1], "schema")) {
        if (argc > 3) {
            write_err_usage("config schema", " [key]\n");
            return 1;
        }
        return config_schema_local(argc, argv);
    }
    if (streq_ignore_case_local(argv[1], "validate")) {
        if (argc != 2) {
            write_err_usage("config validate", "\n");
            return 1;
        }
        return config_validate_local();
    }
    if (streq_ignore_case_local(argv[1], "get") || streq_ignore_case_local(argv[1], "source")) {
        char value[CONFIG_VALUE_MAX];
        const char *source;

        if (argc != 3 || !config_key_valid_local(argv[2])) {
            write_err_usage(argv[1], " <key>\n");
            return 1;
        }
        source = config_lookup_effective_local(argv[2], value, sizeof(value));
        if (source == NULL) {
            write_err_str("config: not found\n");
            return 1;
        }
        if (streq_ignore_case_local(argv[1], "source")) {
            write_str(config_layer_name_local(source));
            write_str(" ");
            write_str(source);
            write_str("\n");
            return 0;
        }
        write_str(value);
        write_str("\n");
        return 0;
    }
    if (streq_ignore_case_local(argv[1], "set") || streq_ignore_case_local(argv[1], "unset")) {
        char value[CONFIG_VALUE_MAX];
        int unset = streq_ignore_case_local(argv[1], "unset");

        if (argc > 2 && (streq_ignore_case_local(argv[2], "--user") ||
                         streq_ignore_case_local(argv[2], "--system") ||
                         streq_ignore_case_local(argv[2], "--runtime"))) {
            path = config_layer_path_local(argv[2]);
            argi = 3;
        } else {
            path = config_layer_path_local("--user");
        }
        if (argc <= argi || !config_key_valid_local(argv[argi])) {
            write_err_usage(argv[1], unset ? " [--user|--system|--runtime] <key>\n" :
                                            " [--user|--system|--runtime] <key> <value>\n");
            return 1;
        }
        if (unset) {
            if (argc != argi + 1) {
                write_err_usage("config unset", " [--user|--system|--runtime] <key>\n");
                return 1;
            }
            if (config_write_key_local(path, argv[argi], NULL, 1) != 0) {
                return 1;
            }
            write_str("unset ");
            write_str(config_layer_name_local(path));
            write_str(" ");
            write_str(argv[argi]);
            write_str("\n");
            return 0;
        }
        if (!config_join_value_local(argc, argv, argi + 1, value, sizeof(value))) {
            write_err_usage("config set", " [--user|--system|--runtime] <key> <value>\n");
            return 1;
        }
        if (!config_value_valid_local(argv[argi], value)) {
            write_err_str("config: invalid value for type ");
            write_err_str(config_type_for_key_local(argv[argi]));
            write_err_str("\n");
            return 1;
        }
        if (config_write_key_local(path, argv[argi], value, 0) != 0) {
            return 1;
        }
        write_str("set ");
        write_str(config_layer_name_local(path));
        write_str(" ");
        write_str(argv[argi]);
        write_str("\n");
        return 0;
    }
    write_err_usage("config", " <get|set|unset|list|source|schema|validate> ...\n");
    return 1;
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
        write_err_usage("service", " <define|list|info|start|stop|restart|enable|disable|boot|reconcile|supervise> ...\n");
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
    if (streq_ignore_case_local(argv[1], "reconcile")) {
        if (argc != 2) {
            write_err_usage("service reconcile", "\n");
            return 1;
        }
        return service_reconcile_local(0);
    }
    if (streq_ignore_case_local(argv[1], "supervise") ||
        streq_ignore_case_local(argv[1], "daemon")) {
        uint32_t interval_ticks = 1000u;

        if (argc > 3) {
            write_err_usage("service supervise", " [seconds|ms|ticks]\n");
            return 1;
        }
        if (argc == 3 && !service_parse_interval_local(argv[2], &interval_ticks)) {
            write_err_usage("service supervise", " [seconds|ms|ticks]\n");
            return 1;
        }
        return service_supervise_local(interval_ticks);
    }
    write_err_usage("service", " <define|list|info|start|stop|restart|enable|disable|boot|reconcile|supervise> ...\n");
    return 1;
}
