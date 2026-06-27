#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"
#include "user/apps/elf/nexbox/applets/system/service_policy.h"

enum {
    SVC_NAME_MAX = 24,
    SVC_VALUE_MAX = 160,
    SVC_DEP_MAX = 96,
    SVC_DEPTH_MAX = 16
};

struct service_def {
    char name[SVC_NAME_MAX + 1];
    char command[SVC_VALUE_MAX];
    char after[SVC_DEP_MAX];
    char requires[SVC_DEP_MAX];
    char stdout_path[CMD_PATH_MAX];
    char stderr_path[CMD_PATH_MAX];
    enum service_restart_policy restart;
    uint32_t backoff_ms;
    uint32_t max_retries;
    int enabled;
};

struct service_run {
    uint32_t pid;
    uint32_t start_tick;
    uint32_t exit_tick;
    uint32_t restart_count;
    int32_t exit_code;
    int stopping;
};

static const char *service_dir = "/system/service";

static int valid_name(const char *name) {
    uint32_t i = 0;
    if (name == 0 || name[0] == '\0') return 0;
    while (name[i] != '\0') {
        char ch = name[i];
        if (i >= SVC_NAME_MAX ||
            !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return 0;
        i++;
    }
    return 1;
}

static int path_for(char *out, uint32_t size, const char *name, const char *ext) {
    int n;
    if (!valid_name(name)) return 0;
    n = snprintf(out, size, "%s/%s.%s", service_dir, name, ext);
    return n > 0 && (uint32_t)n < size;
}

static int write_text(int fd, const char *text) {
    uint32_t len = str_len_local(text);
    return write(fd, text, len) == (int)len;
}

static int write_pair(int fd, const char *key, const char *value) {
    return write_text(fd, key) && write_text(fd, value != 0 ? value : "") && write_text(fd, "\n");
}

static int value_of(const char *line, const char *key, char *out, uint32_t size) {
    uint32_t n = str_len_local(key);
    uint32_t i;
    for (i = 0; i < n && line[i] == key[i]; i++) {}
    if (i != n || line[i] != ' ') return 0;
    copy_line_local(out, line + i + 1u, size);
    return 1;
}

static void def_defaults(struct service_def *def, const char *name) {
    memset(def, 0, sizeof(*def));
    copy_line_local(def->name, name, sizeof(def->name));
    def->restart = SERVICE_RESTART_NEVER;
    def->backoff_ms = 1000u;
}

static int load_def(const char *name, struct service_def *def) {
    char path[CMD_PATH_MAX], line[SVC_VALUE_MAX], value[SVC_VALUE_MAX];
    int fd;
    def_defaults(def, name);
    if (!path_for(path, sizeof(path), name, "svc") || (fd = open(path, O_RDONLY)) < 0) return 0;
    while (read_line(fd, line, sizeof(line)) != 0u) {
        uint32_t number;
        if (value_of(line, "command", value, sizeof(value))) copy_line_local(def->command, value, sizeof(def->command));
        else if (value_of(line, "enabled", value, sizeof(value))) def->enabled = streq_local(value, "1");
        else if (value_of(line, "restart", value, sizeof(value))) (void)service_policy_parse_restart(value, &def->restart);
        else if (value_of(line, "backoff_ms", value, sizeof(value)) && parse_u32_local(value, &number)) def->backoff_ms = number;
        else if (value_of(line, "max_retries", value, sizeof(value)) && parse_u32_local(value, &number)) def->max_retries = number;
        else if (value_of(line, "after", value, sizeof(value))) copy_line_local(def->after, value, sizeof(def->after));
        else if (value_of(line, "requires", value, sizeof(value))) copy_line_local(def->requires, value, sizeof(def->requires));
        else if (value_of(line, "stdout", value, sizeof(value))) copy_line_local(def->stdout_path, value, sizeof(def->stdout_path));
        else if (value_of(line, "stderr", value, sizeof(value))) copy_line_local(def->stderr_path, value, sizeof(def->stderr_path));
    }
    close(fd);
    return def->command[0] != '\0';
}

static int save_def(const struct service_def *def) {
    char path[CMD_PATH_MAX], number[16];
    int fd;
    (void)mkdir("/system");
    (void)mkdir(service_dir);
    if (!path_for(path, sizeof(path), def->name, "svc") || (fd = open(path, O_CREAT | O_TRUNC)) < 0) return 0;
    snprintf(number, sizeof(number), "%u", def->backoff_ms);
    if (!write_text(fd, "# NexOS Service v2\n") ||
        !write_pair(fd, "name ", def->name) ||
        !write_pair(fd, "enabled ", def->enabled ? "1" : "0") ||
        !write_pair(fd, "command ", def->command) ||
        !write_pair(fd, "restart ", service_policy_restart_name(def->restart)) ||
        !write_pair(fd, "backoff_ms ", number)) goto fail;
    snprintf(number, sizeof(number), "%u", def->max_retries);
    if (!write_pair(fd, "max_retries ", number) || !write_pair(fd, "after ", def->after) ||
        !write_pair(fd, "requires ", def->requires) || !write_pair(fd, "stdout ", def->stdout_path) ||
        !write_pair(fd, "stderr ", def->stderr_path)) goto fail;
    close(fd);
    return 1;
fail:
    close(fd);
    return 0;
}

static int load_run(const char *name, struct service_run *run) {
    char path[CMD_PATH_MAX], line[SVC_VALUE_MAX], value[32];
    int fd;
    memset(run, 0, sizeof(*run));
    if (!path_for(path, sizeof(path), name, "run") || (fd = open(path, O_RDONLY)) < 0) return 0;
    while (read_line(fd, line, sizeof(line)) != 0u) {
        uint32_t n;
        if (value_of(line, "pid", value, sizeof(value)) && parse_u32_local(value, &n)) run->pid = n;
        else if (value_of(line, "start_tick", value, sizeof(value)) && parse_u32_local(value, &n)) run->start_tick = n;
        else if (value_of(line, "exit_tick", value, sizeof(value)) && parse_u32_local(value, &n)) run->exit_tick = n;
        else if (value_of(line, "restart_count", value, sizeof(value)) && parse_u32_local(value, &n)) run->restart_count = n;
        else if (value_of(line, "exit_code", value, sizeof(value))) run->exit_code = (int32_t)strtoul(value, 0, 10);
        else if (value_of(line, "stopping", value, sizeof(value))) run->stopping = streq_local(value, "1");
    }
    close(fd);
    return 1;
}

static int save_run(const char *name, const struct service_run *run) {
    char path[CMD_PATH_MAX], n[24];
    int fd;
    if (!path_for(path, sizeof(path), name, "run") || (fd = open(path, O_CREAT | O_TRUNC)) < 0) return 0;
#define WRITE_NUM(key, value) do { snprintf(n, sizeof(n), "%d", (int)(value)); if (!write_pair(fd, key, n)) goto fail; } while (0)
    WRITE_NUM("pid ", run->pid);
    WRITE_NUM("start_tick ", run->start_tick);
    WRITE_NUM("exit_tick ", run->exit_tick);
    WRITE_NUM("exit_code ", run->exit_code);
    WRITE_NUM("restart_count ", run->restart_count);
    WRITE_NUM("stopping ", run->stopping);
#undef WRITE_NUM
    close(fd);
    return 1;
fail:
    close(fd);
    return 0;
}

static int process_info(uint32_t pid, struct syscall_process_info *out) {
    uint32_t i;
    for (i = 0; i < NEX_PROC_SLOTS_MAX; i++)
        if (proc_query(NEX_PROC_QUERY_ALL, i, out) > 0 && out->pid == pid) return 1;
    return 0;
}

static int redirect_open(const char *path, int target, int *saved) {
    int fd;
    int save_fd = 8 + target;
    *saved = -1;
    if (path[0] == '\0') return 1;
    fd = open(path, O_CREAT | O_APPEND);
    if (fd < 0) return 0;
    if (dup2(target, save_fd) < 0 || dup2(fd, target) < 0) { close(fd); return 0; }
    *saved = save_fd;
    close(fd);
    return 1;
}

static void redirect_restore(int target, int saved) {
    if (saved >= 0) { (void)dup2(saved, target); close(saved); }
}

static int start_recursive(const char *name, int quiet, uint32_t depth);

static int start_list(const char *list, int required, uint32_t depth) {
    char item[SVC_NAME_MAX + 1];
    uint32_t i = 0, used = 0;
    for (;;) {
        char ch = list[i++];
        if (ch == ',' || ch == '\0') {
            item[used] = '\0';
            if (used && start_recursive(item, 1, depth + 1u) != 0 && required) return 0;
            used = 0;
            if (ch == '\0') break;
        } else if (ch != ' ' && used < SVC_NAME_MAX) item[used++] = ch;
    }
    return 1;
}

static int start_recursive(const char *name, int quiet, uint32_t depth) {
    struct service_def def;
    struct service_run run;
    struct syscall_process_info info;
    int out_saved, err_saved, rc;
    if (depth >= SVC_DEPTH_MAX || !load_def(name, &def)) return 1;
    if (load_run(name, &run) && run.pid && process_info(run.pid, &info) && info.state != NEX_PROC_STATE_EXITED) return 0;
    if (!start_list(def.requires, 1, depth) || !start_list(def.after, 0, depth)) return 1;
    if (!redirect_open(def.stdout_path, STDOUT_FILENO, &out_saved) ||
        !redirect_open(def.stderr_path, STDERR_FILENO, &err_saved)) return 1;
    rc = spawn(def.command, SYS_SPAWN_ELF, SYS_SPAWN_BACKGROUND);
    redirect_restore(STDERR_FILENO, err_saved);
    redirect_restore(STDOUT_FILENO, out_saved);
    if (rc < 0) return 1;
    if (!load_run(name, &run)) memset(&run, 0, sizeof(run));
    run.pid = (uint32_t)rc;
    run.start_tick = ticks();
    run.exit_tick = 0;
    run.exit_code = 0;
    run.stopping = 0;
    (void)save_run(name, &run);
    if (!quiet) dprintf(STDOUT_FILENO, "started service %s pid=%u\n", name, run.pid);
    return 0;
}

static int stop_service(const char *name, int quiet) {
    struct service_run run;
    struct syscall_process_info info;
    if (!load_run(name, &run)) return 0;
    run.stopping = 1;
    (void)save_run(name, &run);
    if (run.pid && process_info(run.pid, &info) && info.state != NEX_PROC_STATE_EXITED && kill(run.pid) <= 0) return 1;
    if (!quiet) dprintf(STDOUT_FILENO, "stopped service %s\n", name);
    return 0;
}

static int reconcile_one(const char *name, int quiet) {
    struct service_def def;
    struct service_run run;
    struct syscall_process_info info;
    uint32_t delay;
    if (!load_def(name, &def) || !def.enabled) return 0;
    if (!load_run(name, &run)) return start_recursive(name, quiet, 0);
    if (run.pid && process_info(run.pid, &info) && info.state != NEX_PROC_STATE_EXITED) return 0;
    if (run.pid && process_info(run.pid, &info)) {
        run.exit_code = info.exit_code;
        run.exit_tick = ticks();
        run.pid = 0;
        (void)save_run(name, &run);
    }
    if (run.stopping || !service_policy_should_restart(def.restart, run.exit_code) ||
        !service_policy_retry_allowed(run.restart_count, def.max_retries)) return 0;
    delay = service_policy_backoff(def.backoff_ms, run.restart_count);
    if ((uint32_t)(ticks() - run.exit_tick) < delay) return 0;
    run.restart_count++;
    (void)save_run(name, &run);
    return start_recursive(name, quiet, 0);
}

static int each_service(int reconcile) {
    struct syscall_dirent ent;
    int fd = opendir(service_dir), rc = 0;
    if (fd < 0) return 0;
    while (readdir(fd, &ent) > 0) {
        uint32_t len = str_len_local(ent.name);
        char name[SVC_NAME_MAX + 1];
        if (len <= 4 || !streq_ignore_case_local(ent.name + len - 4, ".svc") || len - 4 > SVC_NAME_MAX) continue;
        memcpy(name, ent.name, len - 4); name[len - 4] = '\0';
        if (reconcile_one(name, reconcile) != 0) rc = 1;
    }
    close(fd);
    return rc;
}

static int set_field(const char *name, const char *key, const char *value) {
    struct service_def def;
    uint32_t number;
    if (!load_def(name, &def)) return 1;
    if (streq_local(key, "restart")) {
        if (!service_policy_parse_restart(value, &def.restart)) return 1;
    } else if (streq_local(key, "backoff_ms")) {
        if (!parse_u32_local(value, &number)) return 1;
        def.backoff_ms = number;
    } else if (streq_local(key, "max_retries")) {
        if (!parse_u32_local(value, &number)) return 1;
        def.max_retries = number;
    } else if (streq_local(key, "after")) copy_line_local(def.after, value, sizeof(def.after));
    else if (streq_local(key, "requires")) copy_line_local(def.requires, value, sizeof(def.requires));
    else if (streq_local(key, "stdout")) copy_line_local(def.stdout_path, value, sizeof(def.stdout_path));
    else if (streq_local(key, "stderr")) copy_line_local(def.stderr_path, value, sizeof(def.stderr_path));
    else return 1;
    return save_def(&def) ? 0 : 1;
}

int cmd_service_legacy(int argc, char **argv);

int cmd_service(int argc, char **argv) {
    struct service_def def;
    struct service_run run;
    struct syscall_process_info info;
    int now = argc == 4 && streq_local(argv[3], "--now");
    if (argc < 2) return cmd_service_legacy(argc, argv);
    if (streq_ignore_case_local(argv[1], "set") && argc == 5) return set_field(argv[2], argv[3], argv[4]);
    if (streq_ignore_case_local(argv[1], "start") && argc == 3) return start_recursive(argv[2], 0, 0);
    if (streq_ignore_case_local(argv[1], "stop") && argc == 3) return stop_service(argv[2], 0);
    if (streq_ignore_case_local(argv[1], "restart") && argc == 3) {
        if (stop_service(argv[2], 1)) return 1;
        return start_recursive(argv[2], 0, 0);
    }
    if ((streq_ignore_case_local(argv[1], "enable") || streq_ignore_case_local(argv[1], "disable")) &&
        (argc == 3 || now)) {
        if (!load_def(argv[2], &def)) return 1;
        def.enabled = streq_ignore_case_local(argv[1], "enable");
        if (!save_def(&def)) return 1;
        return now ? (def.enabled ? start_recursive(argv[2], 0, 0) : stop_service(argv[2], 0)) : 0;
    }
    if (streq_ignore_case_local(argv[1], "reconcile") && argc == 2) return each_service(0);
    if (streq_ignore_case_local(argv[1], "boot") && argc == 2) return each_service(0);
    if ((streq_ignore_case_local(argv[1], "info") || streq_ignore_case_local(argv[1], "status")) && argc == 3) {
        if (!load_def(argv[2], &def)) return 1;
        memset(&run, 0, sizeof(run)); (void)load_run(argv[2], &run);
        dprintf(STDOUT_FILENO, "name: %s\nenabled: %d\ncommand: %s\nrestart: %s\nbackoff_ms: %u\nmax_retries: %u\nafter: %s\nrequires: %s\nstdout: %s\nstderr: %s\n",
                def.name, def.enabled, def.command, service_policy_restart_name(def.restart),
                def.backoff_ms, def.max_retries, def.after, def.requires, def.stdout_path, def.stderr_path);
        if (run.pid && process_info(run.pid, &info) && info.state != NEX_PROC_STATE_EXITED)
            dprintf(STDOUT_FILENO, "state: running\npid: %u\nstart_tick: %u\nrestart_count: %u\n", run.pid, run.start_tick, run.restart_count);
        else dprintf(STDOUT_FILENO, "state: stopped\nexit_code: %d\nexit_tick: %u\nrestart_count: %u\n", run.exit_code, run.exit_tick, run.restart_count);
        return 0;
    }
    if (streq_ignore_case_local(argv[1], "supervise")) {
        uint32_t interval = 1000u;
        if (argc == 3 && !parse_u32_local(argv[2], &interval)) return 1;
        for (;;) { (void)each_service(1); sleep(interval); }
    }
    return cmd_service_legacy(argc, argv);
}
