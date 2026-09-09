#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"
#ifdef __i386__
#include "user/libc32/include/nexos/gui.h"
#else
#include "user/libc/include/nexos/gui.h"
#endif

static const char *g_nexctl_service_dir = "/system/service";
static const char *g_nexctl_passwd_path = "/system/config/passwd.scf";

enum {
    NEXCTL_CAP_POLICY_FILE_MAX = 4096u,
    NEXCTL_CAP_POLICY_TOKEN_MAX = 96u,
    NEXCTL_CAP_POLICY_MATCH_NONE = 0u,
    NEXCTL_CAP_POLICY_MATCH_DEFAULT = 1u,
    NEXCTL_CAP_POLICY_MATCH_BASENAME = 2u,
    NEXCTL_CAP_POLICY_MATCH_EXACT = 3u
};

static const char *const g_nexctl_cap_policy_paths[] = {
    "/system/cap.policy",
    "/SYSTEM/CAP.POLICY",
    "/CAP.POLICY"
};

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
    struct syscall_dirent entry;
    uint32_t count = 0u;
    int fd = opendir("/cmd");

    if (fd < 0) {
        return 0u;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (entry.name[0] == '\0' || streq_local(entry.name, ".") ||
            streq_local(entry.name, "..")) {
            continue;
        }
        count++;
    }
    (void)close((uint32_t)fd);
    return count;
}

static void nexctl_cap_write_names_local(uint32_t caps) {
    int first = 1;

    if (caps == 0u) {
        write_str("none");
        return;
    }
#define NEXCTL_CAP_NAME(bit, name) \
    do { \
        if ((caps & (bit)) != 0u) { \
            if (!first) { \
                write_str(","); \
            } \
            write_str(name); \
            first = 0; \
        } \
    } while (0)
    NEXCTL_CAP_NAME(SYS_PROC_CAP_POWER, "power");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_RAW_BLOCK, "raw-block");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_MOUNT, "mount");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_SIGNAL, "signal");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_GRANT, "grant");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_AUDIO, "audio");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_NET_RAW, "net-raw");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_DISPLAY, "display");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_INPUT, "input");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_CLIPBOARD, "clipboard");
    NEXCTL_CAP_NAME(SYS_PROC_CAP_DEBUG, "debug");
#undef NEXCTL_CAP_NAME
}

static int nexctl_cap_parse_local(const char *arg, uint32_t *mask) {
    uint32_t numeric;

    if (arg == NULL || mask == NULL) {
        return 0;
    }
    if (streq_ignore_case_local(arg, "all")) {
        *mask = SYS_PROC_CAP_ALL;
        return 1;
    }
    if (streq_ignore_case_local(arg, "power")) {
        *mask = SYS_PROC_CAP_POWER;
        return 1;
    }
    if (streq_ignore_case_local(arg, "raw-block") ||
        streq_ignore_case_local(arg, "raw_block") ||
        streq_ignore_case_local(arg, "block")) {
        *mask = SYS_PROC_CAP_RAW_BLOCK;
        return 1;
    }
    if (streq_ignore_case_local(arg, "mount")) {
        *mask = SYS_PROC_CAP_MOUNT;
        return 1;
    }
    if (streq_ignore_case_local(arg, "signal") ||
        streq_ignore_case_local(arg, "kill")) {
        *mask = SYS_PROC_CAP_SIGNAL;
        return 1;
    }
    if (streq_ignore_case_local(arg, "grant")) {
        *mask = SYS_PROC_CAP_GRANT;
        return 1;
    }
    if (streq_ignore_case_local(arg, "audio")) {
        *mask = SYS_PROC_CAP_AUDIO;
        return 1;
    }
    if (streq_ignore_case_local(arg, "net-raw") ||
        streq_ignore_case_local(arg, "net_raw") ||
        streq_ignore_case_local(arg, "network") ||
        streq_ignore_case_local(arg, "rtl8139")) {
        *mask = SYS_PROC_CAP_NET_RAW;
        return 1;
    }
    if (streq_ignore_case_local(arg, "display") ||
        streq_ignore_case_local(arg, "gfx")) {
        *mask = SYS_PROC_CAP_DISPLAY;
        return 1;
    }
    if (streq_ignore_case_local(arg, "input")) {
        *mask = SYS_PROC_CAP_INPUT;
        return 1;
    }
    if (streq_ignore_case_local(arg, "clipboard")) {
        *mask = SYS_PROC_CAP_CLIPBOARD;
        return 1;
    }
    if (streq_ignore_case_local(arg, "debug")) {
        *mask = SYS_PROC_CAP_DEBUG;
        return 1;
    }
    if (parse_u32_local(arg, &numeric) && (numeric & ~SYS_PROC_CAP_ALL) == 0u) {
        *mask = numeric;
        return 1;
    }
    return 0;
}

static int nexctl_cap_mask_from_args_local(int argc, char **argv, uint32_t *mask) {
    int i;

    if (argc < 4 || mask == NULL) {
        return 0;
    }
    *mask = 0u;
    for (i = 3; i < argc; i++) {
        uint32_t part;

        if (!nexctl_cap_parse_local(argv[i], &part)) {
            return 0;
        }
        *mask |= part;
    }
    return 1;
}

static int nexctl_cap_mask_from_range_local(char **argv,
                                            int first_arg,
                                            int last_arg,
                                            uint32_t *mask) {
    int i;

    if (argv == NULL || first_arg >= last_arg || mask == NULL) {
        return 0;
    }
    *mask = 0u;
    for (i = first_arg; i < last_arg; i++) {
        uint32_t part;

        if (!nexctl_cap_parse_local(argv[i], &part)) {
            return 0;
        }
        *mask |= part;
    }
    return 1;
}

static int nexctl_cap_print_current_local(void) {
    uint32_t caps = 0u;
    int rc = capability_get(&caps);

    if (rc < 0) {
        write_err_str("nexctl cap: get failed rc=");
        write_sdec(rc);
        write_err_str("\n");
        return 1;
    }
    write_str("caps: ");
    write_hex_u32(caps);
    write_str(" ");
    nexctl_cap_write_names_local(caps);
    write_str("\n");
    return 0;
}

static int nexctl_cap_print_spawn_local(void) {
    uint32_t caps = 0u;
    int rc = capability_spawn_get(&caps);

    if (rc < 0) {
        write_err_str("nexctl cap: spawn get failed rc=");
        write_sdec(rc);
        write_err_str("\n");
        return 1;
    }
    write_str("spawn caps: ");
    write_hex_u32(caps);
    write_str(" ");
    nexctl_cap_write_names_local(caps);
    write_str("\n");
    return 0;
}

static int nexctl_cap_run_local(int argc, char **argv) {
    char command[CMD_PATH_MAX];
    uint32_t mask;
    int sep = 0;
    int i;
    int rc;

    for (i = 3; i < argc; i++) {
        if (streq_local(argv[i], "--")) {
            sep = i;
            break;
        }
    }
    if (sep == 0 ||
        !nexctl_cap_mask_from_range_local(argv, 3, sep, &mask) ||
        sep + 1 >= argc) {
        write_err_usage("nexctl cap run", " <caps...> -- <command> [args]\n");
        return 1;
    }
    if (!cmd_build_program_command(
            argc, argv, sep + 1, "nexctl cap run", 0, command, sizeof(command))) {
        return 1;
    }
    rc = capability_spawn_set(mask);
    if (rc < 0) {
        write_err_str("nexctl cap: spawn set failed rc=");
        write_sdec(rc);
        write_err_str("\n");
        return 1;
    }
    rc = spawn(command, SYS_SPAWN_ELF, 0u);
    if (rc < 0) {
        (void)capability_spawn_clear();
        write_err_str("nexctl cap run: spawn failed rc=");
        write_sdec(rc);
        write_err_str("\n");
        return 1;
    }
    if (rc == 0 || fg((uint32_t)rc) <= 0) {
        write_err_str("nexctl cap run: foreground failed\n");
        return 1;
    }
    return 0;
}

static int nexctl_cap_auth_grant_local(int argc, char **argv) {
    uint32_t mask;
    int sep = 0;
    int i;
    int rc;

    for (i = 3; i < argc; i++) {
        if (streq_local(argv[i], "--")) {
            sep = i;
            break;
        }
    }
    if (sep == 0 ||
        !nexctl_cap_mask_from_range_local(argv, 3, sep, &mask) ||
        sep + 1 >= argc ||
        sep + 2 != argc) {
        write_err_usage("nexctl cap auth-grant", " <caps...> -- <root-token>\n");
        return 1;
    }
    rc = capability_auth_grant(mask, argv[sep + 1]);
    if (rc < 0) {
        write_err_str("nexctl cap: auth grant failed rc=");
        write_sdec(rc);
        write_err_str("\n");
        return 1;
    }
    write_str("caps: ");
    write_hex_u32((uint32_t)rc);
    write_str(" ");
    nexctl_cap_write_names_local((uint32_t)rc);
    write_str("\n");
    return 0;
}

static int nexctl_cap_policy_local(void) {
    char buffer[256];
    int fd = open(g_nexctl_cap_policy_paths[0], O_RDONLY);

    if (fd < 0) {
        fd = open(g_nexctl_cap_policy_paths[1], O_RDONLY);
    }
    if (fd < 0) {
        fd = open(g_nexctl_cap_policy_paths[2], O_RDONLY);
    }
    if (fd < 0) {
        write_err_str("nexctl cap policy: not found\n");
        return 1;
    }
    for (;;) {
        int got = read((uint32_t)fd, buffer, sizeof(buffer));

        if (got < 0) {
            (void)close((uint32_t)fd);
            write_err_str("nexctl cap policy: read failed\n");
            return 1;
        }
        if (got == 0) {
            break;
        }
        write_stdout(buffer, (uint32_t)got);
    }
    (void)close((uint32_t)fd);
    return 0;
}

static const char *nexctl_cap_policy_basename_local(const char *path) {
    const char *base = path;
    uint32_t i = 0u;

    while (path != NULL && path[i] != '\0') {
        if (path[i] == '/') {
            base = path + i + 1u;
        }
        i++;
    }
    return base != NULL ? base : "";
}

static int nexctl_cap_policy_has_path_local(const char *pattern) {
    uint32_t i = 0u;

    while (pattern != NULL && pattern[i] != '\0') {
        if (pattern[i] == '/') {
            return 1;
        }
        i++;
    }
    return 0;
}

static const char *nexctl_cap_policy_skip_inline_spaces_local(
    const char *cursor,
    const char *line_end) {
    while (cursor < line_end && (cursor[0] == ' ' || cursor[0] == '\t')) {
        cursor++;
    }
    return cursor;
}

static int nexctl_cap_policy_read_token_local(const char **cursor_io,
                                              const char *line_end,
                                              char *out,
                                              uint32_t out_size) {
    const char *cursor;
    uint32_t len = 0u;

    if (cursor_io == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    cursor = nexctl_cap_policy_skip_inline_spaces_local(*cursor_io, line_end);
    if (cursor >= line_end || cursor[0] == '#') {
        out[0] = '\0';
        *cursor_io = cursor;
        return 0;
    }
    while (cursor < line_end &&
           cursor[0] != ' ' && cursor[0] != '\t' &&
           cursor[0] != '#') {
        if (len + 1u >= out_size) {
            return 0;
        }
        out[len++] = *cursor++;
    }
    out[len] = '\0';
    *cursor_io = cursor;
    return len != 0u;
}

static uint32_t nexctl_cap_policy_match_score_local(const char *pattern,
                                                    const char *image_name) {
    const char *base;

    if (pattern == NULL || image_name == NULL || pattern[0] == '\0') {
        return NEXCTL_CAP_POLICY_MATCH_NONE;
    }
    if (streq_local(pattern, "*") || streq_local(pattern, "default")) {
        return NEXCTL_CAP_POLICY_MATCH_DEFAULT;
    }
    if (nexctl_cap_policy_has_path_local(pattern)) {
        return streq_local(pattern, image_name)
            ? NEXCTL_CAP_POLICY_MATCH_EXACT
            : NEXCTL_CAP_POLICY_MATCH_NONE;
    }
    base = nexctl_cap_policy_basename_local(image_name);
    return streq_local(pattern, base)
        ? NEXCTL_CAP_POLICY_MATCH_BASENAME
        : NEXCTL_CAP_POLICY_MATCH_NONE;
}

static int nexctl_cap_policy_parse_caps_local(const char *cursor,
                                              const char *line_end,
                                              uint32_t *mask_out) {
    char token[NEXCTL_CAP_POLICY_TOKEN_MAX];
    uint32_t mask = 0u;
    int saw_cap = 0;

    cursor = nexctl_cap_policy_skip_inline_spaces_local(cursor, line_end);
    while (cursor < line_end && cursor[0] != '#') {
        uint32_t cap;

        if (!nexctl_cap_policy_read_token_local(
                &cursor, line_end, token, sizeof(token)) ||
            !nexctl_cap_parse_local(token, &cap)) {
            return 0;
        }
        mask |= cap;
        saw_cap = 1;
        cursor = nexctl_cap_policy_skip_inline_spaces_local(cursor, line_end);
    }
    if (!saw_cap || mask_out == NULL) {
        return 0;
    }
    *mask_out = mask & SYS_PROC_CAP_ALL;
    return 1;
}

static void nexctl_cap_policy_apply_line_local(const char *line,
                                               const char *image_name,
                                               uint32_t *best_score_io,
                                               uint32_t *policy_mask_io) {
    char pattern[NEXCTL_CAP_POLICY_TOKEN_MAX];
    const char *cursor = line;
    const char *line_end = line;
    uint32_t score;
    uint32_t mask;

    while (line_end[0] != '\0' && line_end[0] != '\n' && line_end[0] != '\r') {
        line_end++;
    }
    if (!nexctl_cap_policy_read_token_local(
            &cursor, line_end, pattern, sizeof(pattern))) {
        return;
    }
    score = nexctl_cap_policy_match_score_local(pattern, image_name);
    if (score == NEXCTL_CAP_POLICY_MATCH_NONE ||
        score < *best_score_io ||
        !nexctl_cap_policy_parse_caps_local(cursor, line_end, &mask)) {
        return;
    }
    *best_score_io = score;
    *policy_mask_io = mask;
}

static int nexctl_cap_policy_load_local(char *buffer, uint32_t size) {
    uint32_t i;

    if (buffer == NULL || size == 0u) {
        return 0;
    }
    for (i = 0u;
         i < sizeof(g_nexctl_cap_policy_paths) / sizeof(g_nexctl_cap_policy_paths[0]);
         i++) {
        int fd = open(g_nexctl_cap_policy_paths[i], O_RDONLY);
        int got;

        if (fd < 0) {
            continue;
        }
        got = read((uint32_t)fd, buffer, size - 1u);
        close((uint32_t)fd);
        if (got <= 0) {
            return 0;
        }
        buffer[(uint32_t)got] = '\0';
        return 1;
    }
    return 0;
}

static int nexctl_cap_policy_match_local(const char *policy,
                                         const char *image_name,
                                         uint32_t *mask_out) {
    uint32_t pos = 0u;
    uint32_t best_score = NEXCTL_CAP_POLICY_MATCH_NONE;
    uint32_t mask = SYS_PROC_CAP_ALL;

    if (policy == NULL || image_name == NULL || mask_out == NULL) {
        return 0;
    }
    while (policy[pos] != '\0') {
        nexctl_cap_policy_apply_line_local(
            policy + pos, image_name, &best_score, &mask);
        while (policy[pos] != '\0' && policy[pos] != '\n') {
            pos++;
        }
        if (policy[pos] == '\n') {
            pos++;
        }
    }
    if (best_score == NEXCTL_CAP_POLICY_MATCH_NONE) {
        return 0;
    }
    *mask_out = mask & SYS_PROC_CAP_ALL;
    return 1;
}

static int nexctl_cap_cmd_path_local(const char *name,
                                     char *out,
                                     uint32_t out_size) {
    uint32_t pos = 0u;
    uint32_t i = 0u;
    static const char prefix[] = "/cmd/";

    if (name == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    while (prefix[pos] != '\0' && pos + 1u < out_size) {
        out[pos] = prefix[pos];
        pos++;
    }
    while (name[i] != '\0' && pos + 1u < out_size) {
        out[pos++] = name[i++];
    }
    if (name[i] != '\0') {
        return 0;
    }
    out[pos] = '\0';
    return 1;
}

static int nexctl_cap_apply_policy_local(void) {
    static char policy[NEXCTL_CAP_POLICY_FILE_MAX];
    struct syscall_dirent entry;
    uint32_t applied = 0u;
    uint32_t skipped = 0u;
    int fd;

    if (!nexctl_cap_policy_load_local(policy, sizeof(policy))) {
        write_err_str("nexctl cap apply: policy not found\n");
        return 1;
    }
    fd = opendir("/cmd");
    if (fd < 0) {
        write_err_str("nexctl cap apply: /cmd not found\n");
        return 1;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char path[CMD_PATH_MAX];
        uint32_t mask;

        if (entry.name[0] == '\0' || streq_local(entry.name, ".") ||
            streq_local(entry.name, "..") ||
            !nexctl_cap_cmd_path_local(entry.name, path, sizeof(path))) {
            skipped++;
            continue;
        }
        if (!nexctl_cap_policy_match_local(policy, path, &mask)) {
            skipped++;
            continue;
        }
        if (setcap(path, mask) < 0) {
            write_err_str("nexctl cap apply: setcap failed ");
            write_err_str(path);
            write_err_str("\n");
            close((uint32_t)fd);
            return 1;
        }
        applied++;
    }
    close((uint32_t)fd);
    write_str("cap policy applied files=");
    write_dec(applied);
    write_str(" skipped=");
    write_dec(skipped);
    write_str("\n");
    return 0;
}

static int nexctl_cap_ps_local(void) {
    char *ps_argv[2];

    ps_argv[0] = "ps";
    ps_argv[1] = "--caps";
    return cmd_ps(2, ps_argv);
}

typedef int (*nexctl_cap_probe_fn_local)(void);

struct nexctl_cap_check_case_local {
    const char *name;
    uint32_t cap;
    nexctl_cap_probe_fn_local probe;
    int unsafe;
};

static int nexctl_cap_restore_local(uint32_t caps) {
    int rc = capability_grant(caps & SYS_PROC_CAP_ALL);

    if (rc < 0) {
        return rc;
    }
    return capability_drop((~caps) & SYS_PROC_CAP_ALL);
}

static int nexctl_cap_probe_power_local(void) {
    return reboot();
}

static int nexctl_cap_probe_signal_local(void) {
    return kill(0xffffffffu);
}

static int nexctl_cap_probe_mount_local(void) {
    return umount("/__nexos_cap_check_missing__");
}

static int nexctl_cap_probe_raw_block_local(void) {
    return block_flush(0u);
}

static int nexctl_cap_probe_audio_local(void) {
    return audio_tone(0u, 440u, 1u);
}

static int nexctl_cap_probe_clipboard_local(void) {
    return clipboard_size();
}

static int nexctl_cap_probe_display_local(void) {
    return gfx_clear(0u);
}

static int nexctl_cap_probe_input_local(void) {
    int rc = gui_input_grab();

    if (rc > 0) {
        (void)gui_input_release();
    }
    return rc;
}

static int nexctl_cap_probe_net_raw_local(void) {
    return rtl8139_tx_test();
}

static int nexctl_cap_probe_open_path_local(const char *path,
                                            uint32_t flags) {
    int fd = open(path, flags);

    if (fd >= 0) {
        (void)close((uint32_t)fd);
    }
    return fd;
}

static int nexctl_cap_probe_raw_block_event_open_local(void) {
    return nexctl_cap_probe_open_path_local("/event/block/change", O_RDONLY);
}

static int nexctl_cap_probe_raw_block_proc_open_local(void) {
    return nexctl_cap_probe_open_path_local("/proc/block", O_RDONLY);
}

static int nexctl_cap_probe_audio_dev_open_local(void) {
    return nexctl_cap_probe_open_path_local("/dev/audio", O_WRONLY);
}

static int nexctl_cap_probe_speaker_dev_open_local(void) {
    return nexctl_cap_probe_open_path_local("/dev/speaker", O_WRONLY);
}

static int nexctl_cap_probe_display_dev_open_local(void) {
    return nexctl_cap_probe_open_path_local("/dev/fb", O_RDWR);
}

static int nexctl_cap_probe_display_proc_open_local(void) {
    return nexctl_cap_probe_open_path_local("/proc/fb", O_RDONLY);
}

static int nexctl_cap_probe_input_event_open_local(void) {
    return nexctl_cap_probe_open_path_local("/event/input/keyboard", O_RDONLY);
}

static int nexctl_cap_probe_net_event_open_local(void) {
    return nexctl_cap_probe_open_path_local("/event/net/status", O_RDONLY);
}

static int nexctl_cap_probe_debug_profile_local(void) {
    struct syscall_profile_info info;

    return profile_query(0u, 0u, &info);
}

static int nexctl_cap_probe_debug_profile_reset_local(void) {
    struct syscall_profile_info info;

    return profile_query(0u, SYS_PROFILE_QUERY_RESET, &info);
}

static int nexctl_cap_probe_debug_kmsg_local(void) {
    struct syscall_kmsg_info info;

    return kmsg_query(0u, &info);
}

static int nexctl_cap_probe_debug_memmap_local(void) {
    struct syscall_memmap_info info;

    return memmap_query(0u, &info);
}

static int nexctl_cap_probe_debug_pmm_local(void) {
    struct syscall_pmm_info info;

    return pmm_query(&info);
}

static int nexctl_cap_probe_debug_vm_local(void) {
    struct syscall_vm_info info;

    return vm_query(&info);
}

static int nexctl_cap_probe_debug_pci_local(void) {
    struct syscall_pci_info info;

    return pci_query_at(0u, &info);
}

static int nexctl_cap_probe_debug_proc_open_local(void) {
    return nexctl_cap_probe_open_path_local("/proc/kmsg", O_RDONLY);
}

static int nexctl_cap_probe_debug_event_open_local(void) {
    return nexctl_cap_probe_open_path_local("/event/security/capability", O_RDONLY);
}

static int nexctl_cap_check_one_local(const struct nexctl_cap_check_case_local *test,
                                      uint32_t saved_caps) {
    uint32_t now_caps = 0u;
    int rc;
    int restore_rc;

    if (test == NULL) {
        return 1;
    }
    rc = capability_drop(test->cap);
    if (rc < 0 || capability_get(&now_caps) < 0 || (now_caps & test->cap) != 0u) {
        write_err_str("cap check: ");
        write_err_str(test->name);
        write_err_str(" drop failed rc=");
        write_sdec(rc);
        write_err_str("\n");
        (void)nexctl_cap_restore_local(saved_caps);
        return 1;
    }
    rc = test->probe();
    restore_rc = nexctl_cap_restore_local(saved_caps);
    if (restore_rc < 0) {
        write_err_str("cap check: restore failed rc=");
        write_sdec(restore_rc);
        write_err_str("\n");
        return 1;
    }
    if (rc == -NEX_ERR_ACCES) {
        write_str("cap check: ");
        write_str(test->name);
        write_str(" OK\n");
        return 0;
    }
    write_err_str("cap check: ");
    write_err_str(test->name);
    write_err_str(" FAIL expected ");
    write_sdec(-NEX_ERR_ACCES);
    write_err_str(" got ");
    write_sdec(rc);
    write_err_str("\n");
    return 1;
}

static int nexctl_cap_check_local(int argc, char **argv) {
    static const struct nexctl_cap_check_case_local tests[] = {
        {"signal", SYS_PROC_CAP_SIGNAL, nexctl_cap_probe_signal_local, 0},
        {"mount", SYS_PROC_CAP_MOUNT, nexctl_cap_probe_mount_local, 0},
        {"raw-block", SYS_PROC_CAP_RAW_BLOCK, nexctl_cap_probe_raw_block_local, 0},
        {"raw-block-event-open", SYS_PROC_CAP_RAW_BLOCK, nexctl_cap_probe_raw_block_event_open_local, 0},
        {"raw-block-proc-open", SYS_PROC_CAP_RAW_BLOCK, nexctl_cap_probe_raw_block_proc_open_local, 0},
        {"audio", SYS_PROC_CAP_AUDIO, nexctl_cap_probe_audio_local, 0},
        {"audio-dev-open", SYS_PROC_CAP_AUDIO, nexctl_cap_probe_audio_dev_open_local, 0},
        {"speaker-dev-open", SYS_PROC_CAP_AUDIO, nexctl_cap_probe_speaker_dev_open_local, 0},
        {"clipboard", SYS_PROC_CAP_CLIPBOARD, nexctl_cap_probe_clipboard_local, 0},
        {"display", SYS_PROC_CAP_DISPLAY, nexctl_cap_probe_display_local, 0},
        {"display-dev-open", SYS_PROC_CAP_DISPLAY, nexctl_cap_probe_display_dev_open_local, 0},
        {"display-proc-open", SYS_PROC_CAP_DISPLAY, nexctl_cap_probe_display_proc_open_local, 0},
        {"input", SYS_PROC_CAP_INPUT, nexctl_cap_probe_input_local, 0},
        {"input-event-open", SYS_PROC_CAP_INPUT, nexctl_cap_probe_input_event_open_local, 0},
        {"net-raw", SYS_PROC_CAP_NET_RAW, nexctl_cap_probe_net_raw_local, 0},
        {"net-event-open", SYS_PROC_CAP_NET_RAW, nexctl_cap_probe_net_event_open_local, 0},
        {"debug-profile", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_profile_local, 0},
        {"debug-profile-reset", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_profile_reset_local, 0},
        {"debug-kmsg", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_kmsg_local, 0},
        {"debug-memmap", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_memmap_local, 0},
        {"debug-pmm", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_pmm_local, 0},
        {"debug-vm", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_vm_local, 0},
        {"debug-pci", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_pci_local, 0},
        {"debug-proc-open", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_proc_open_local, 0},
        {"debug-event-open", SYS_PROC_CAP_DEBUG, nexctl_cap_probe_debug_event_open_local, 0},
        {"power", SYS_PROC_CAP_POWER, nexctl_cap_probe_power_local, 1}
    };
    uint32_t saved_caps = 0u;
    int include_unsafe = 0;
    int failures = 0;
    uint32_t i;

    if (argc > 4 ||
        (argc == 4 && !streq_ignore_case_local(argv[3], "unsafe"))) {
        write_err_usage("nexctl cap check", " [unsafe]\n");
        return 1;
    }
    include_unsafe = argc == 4;
    if (capability_get(&saved_caps) < 0) {
        write_err_str("nexctl cap check: get failed\n");
        return 1;
    }
    if ((saved_caps & SYS_PROC_CAP_GRANT) == 0u) {
        write_err_str("nexctl cap check: grant capability required\n");
        return 1;
    }
    for (i = 0u; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (tests[i].unsafe && !include_unsafe) {
            write_str("cap check: ");
            write_str(tests[i].name);
            write_str(" SKIP unsafe\n");
            continue;
        }
        failures += nexctl_cap_check_one_local(&tests[i], saved_caps);
    }
    if (nexctl_cap_restore_local(saved_caps) < 0) {
        write_err_str("nexctl cap check: final restore failed\n");
        return 1;
    }
    if (failures == 0) {
        write_str("cap check: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}

static int nexctl_cap_local(int argc, char **argv) {
    uint32_t mask;
    int rc;

    if (argc < 3 ||
        streq_ignore_case_local(argv[2], "get") ||
        streq_ignore_case_local(argv[2], "show")) {
        return nexctl_cap_print_current_local();
    }
    if (streq_ignore_case_local(argv[2], "drop")) {
        if (!nexctl_cap_mask_from_args_local(argc, argv, &mask)) {
            write_err_usage("nexctl cap drop", " <cap|all|mask> ...\n");
            return 1;
        }
        rc = capability_drop(mask);
    } else if (streq_ignore_case_local(argv[2], "grant")) {
        if (!nexctl_cap_mask_from_args_local(argc, argv, &mask)) {
            write_err_usage("nexctl cap grant", " <cap|all|mask> ...\n");
            return 1;
        }
        rc = capability_grant(mask);
    } else if (streq_ignore_case_local(argv[2], "spawn")) {
        if (argc == 3 ||
            streq_ignore_case_local(argv[3], "get") ||
            streq_ignore_case_local(argv[3], "show")) {
            return nexctl_cap_print_spawn_local();
        }
        if (streq_ignore_case_local(argv[3], "clear")) {
            rc = capability_spawn_clear();
        } else if (!nexctl_cap_mask_from_range_local(argv, 3, argc, &mask)) {
            write_err_usage("nexctl cap spawn", " <cap|all|mask> ... | clear\n");
            return 1;
        } else {
            rc = capability_spawn_set(mask);
        }
    } else if (streq_ignore_case_local(argv[2], "run")) {
        return nexctl_cap_run_local(argc, argv);
    } else if (streq_ignore_case_local(argv[2], "auth-grant") ||
               streq_ignore_case_local(argv[2], "request")) {
        return nexctl_cap_auth_grant_local(argc, argv);
    } else if (streq_ignore_case_local(argv[2], "check")) {
        return nexctl_cap_check_local(argc, argv);
    } else if (streq_ignore_case_local(argv[2], "policy")) {
        return nexctl_cap_policy_local();
    } else if (streq_ignore_case_local(argv[2], "apply")) {
        if (argc != 3) {
            write_err_usage("nexctl cap apply", "\n");
            return 1;
        }
        return nexctl_cap_apply_policy_local();
    } else if (streq_ignore_case_local(argv[2], "ps")) {
        if (argc != 3) {
            write_err_usage("nexctl cap ps", "\n");
            return 1;
        }
        return nexctl_cap_ps_local();
    } else {
        write_err_usage("nexctl cap", " [get|drop|grant|auth-grant|spawn|run|check|policy|apply|ps] ...\n");
        return 1;
    }
    if (rc < 0) {
        write_err_str("nexctl cap: operation failed rc=");
        write_sdec(rc);
        write_err_str("\n");
        return 1;
    }
    write_str("caps: ");
    write_hex_u32((uint32_t)rc);
    write_str(" ");
    nexctl_cap_write_names_local((uint32_t)rc);
    write_str("\n");
    return 0;
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

static const char *nexctl_user_name_local(uint32_t uid) {
    if (uid == 0u) {
        return "root";
    }
    if (uid == 1000u) {
        return "user";
    }
    return "unknown";
}

static int nexctl_user_id_local(const char *name, uint32_t *uid_out) {
    uint32_t uid;

    if (name == NULL || uid_out == NULL) {
        return 0;
    }
    if (streq_ignore_case_local(name, "root")) {
        *uid_out = 0u;
        return 1;
    }
    if (streq_ignore_case_local(name, "user")) {
        *uid_out = 1000u;
        return 1;
    }
    if (parse_u32_local(name, &uid)) {
        *uid_out = uid;
        return 1;
    }
    return 0;
}

static char *nexctl_find_char_local(char *text, char needle) {
    if (text == NULL) {
        return NULL;
    }
    while (*text != '\0') {
        if (*text == needle) {
            return text;
        }
        text++;
    }
    return NULL;
}

static int nexctl_passwd_auth_local(const char *name,
                                    const char *password,
                                    uint32_t *uid_out) {
    char buffer[512];
    uint32_t used = 0u;
    char *line;
    int fd;

    if (name == NULL || password == NULL || uid_out == NULL) {
        return 0;
    }
    fd = open(g_nexctl_passwd_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    while (used + 1u < sizeof(buffer)) {
        int got = read((uint32_t)fd, buffer + used, sizeof(buffer) - used - 1u);

        if (got < 0) {
            (void)close((uint32_t)fd);
            return -1;
        }
        if (got == 0) {
            break;
        }
        used += (uint32_t)got;
    }
    (void)close((uint32_t)fd);
    buffer[used] = '\0';
    line = buffer;
    while (*line != '\0') {
        char *next = line;
        char *name_end;
        char *uid_end;
        char *gid_end;
        char *pass;
        uint32_t uid;

        while (*next != '\0' && *next != '\n' && *next != '\r') {
            next++;
        }
        if (*next != '\0') {
            *next++ = '\0';
            if (*(next - 1) == '\r' && *next == '\n') {
                next++;
            }
        }
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (*line != '\0' && *line != '#') {
            name_end = nexctl_find_char_local(line, ':');
            if (name_end != NULL) {
                *name_end = '\0';
                uid_end = nexctl_find_char_local(name_end + 1, ':');
                if (uid_end != NULL) {
                    *uid_end = '\0';
                    gid_end = nexctl_find_char_local(uid_end + 1, ':');
                    if (gid_end != NULL) {
                        *gid_end = '\0';
                        pass = gid_end + 1;
                        if (streq_local(line, name)) {
                            if (!parse_u32_local(name_end + 1, &uid)) {
                                return 0;
                            }
                            if (!streq_local(pass, password)) {
                                return 0;
                            }
                            *uid_out = uid;
                            return 1;
                        }
                    }
                }
            }
        }
        line = next;
    }
    return 0;
}

static int nexctl_auth_root_local(const char *password) {
    struct syscall_identity_info identity;
    uint32_t uid = 0u;
    int rc = nexctl_passwd_auth_local("root", password, &uid);

    if (rc <= 0 || uid != 0u) {
        return -1;
    }
    if (identity_get(&identity) < 0 || identity.uid != 0u) {
        return -1;
    }
    return 1;
}

static int nexctl_user_status_local(void) {
    struct syscall_identity_info info;
    int rc = identity_get(&info);

    if (rc < 0) {
        write_err_str("nexctl user status failed rc=");
        write_sdec((int32_t)rc);
        write_err_str("\n");
        return 1;
    }
    dprintf(STDOUT_FILENO,
            "uid=%u(%s) gid=%u\n",
            info.uid,
            nexctl_user_name_local(info.uid),
            info.gid);
    return 0;
}

static int run_foreground_command_local(const char *verb, const char *command) {
    struct syscall_process_info info;
    int fg_rc;
    int wait_rc;
    int rc = spawn(command, SYS_SPAWN_AUTO, 0u);

    if (rc < 0) {
        write_err_str(verb);
        write_err_str(": spawn failed rc=");
        write_sdec((int32_t)rc);
        write_err_str("\n");
        return 1;
    }
    if (rc == 0) {
        write_err_str(verb);
        write_err_str(": foreground failed\n");
        return 1;
    }
    fg_rc = fg((uint32_t)rc);
    wait_rc = wait((uint32_t)rc, &info);
    if (fg_rc <= 0 || wait_rc <= 0) {
        write_err_str(verb);
        write_err_str(fg_rc <= 0 ? ": foreground failed rc=" : ": wait failed rc=");
        write_sdec((int32_t)(fg_rc <= 0 ? fg_rc : wait_rc));
        write_err_str("\n");
        return 1;
    }
    return info.exit_code == 0 ? 0 : 1;
}

static int exec_command_local(const char *verb, const char *command) {
    int rc = exec(command);

    if (rc < 0) {
        write_err_str(verb);
        write_err_str(": exec failed rc=");
        write_sdec((int32_t)rc);
        write_err_str("\n");
        return 1;
    }
    return 0;
}

static int switch_identity_and_exec_local(int argc,
                                          char **argv,
                                          const char *verb,
                                          uint32_t default_uid) {
    struct syscall_identity_info identity;
    char command[CMD_PATH_MAX];
    char token[64];
    const char *token_arg = NULL;
    uint32_t target_uid = default_uid;
    int sep = 0;
    int command_start;
    int arg = 1;
    int rc;

    token[0] = '\0';
    if (argc >= 2 && !streq_local(argv[1], "--")) {
        if (nexctl_user_id_local(argv[1], &target_uid)) {
            arg = 2;
        } else {
            token_arg = argv[1];
            arg = 2;
        }
    }

    for (int i = arg; i < argc; i++) {
        if (streq_local(argv[i], "--")) {
            sep = i;
            break;
        }
    }
    if (token_arg == NULL && target_uid == 0u && arg < argc &&
        (sep == 0 || arg < sep)) {
        token_arg = argv[arg++];
    }
    if (sep != 0 && arg != sep) {
        write_err_usage(verb, " [USER] [TOKEN] [-- <command> [args]]\n");
        return 1;
    }
    if (sep == 0 && arg < argc) {
        write_err_usage(verb, " [USER] [TOKEN] [-- <command> [args]]\n");
        return 1;
    }
    if (sep != 0 && sep + 1 >= argc) {
        write_err_usage(verb, " [USER] [TOKEN] [-- <command> [args]]\n");
        return 1;
    }

    command_start = sep != 0 ? sep + 1 : argc;
    if (command_start >= argc) {
        copy_line_local(command, "/cmd/ush", sizeof(command));
    } else if (!cmd_build_program_command(argc,
                                          argv,
                                          command_start,
                                          verb,
                                          0,
                                          command,
                                          sizeof(command))) {
        return 1;
    }

    if (identity_get(&identity) < 0) {
        write_err_str(verb);
        write_err_str(": identity query failed\n");
        return 1;
    }
    if (identity.uid != target_uid) {
        rc = identity_push();
        if (rc < 0) {
            write_err_str(verb);
            write_err_str(": identity context push failed rc=");
            write_sdec((int32_t)rc);
            write_err_str("\n");
            return 1;
        }
        if (target_uid == 0u) {
            if (token_arg == NULL) {
                write_str("root password: ");
                if (read_line(STDIN_FILENO, token, sizeof(token)) == 0u ||
                    token[0] == '\0') {
                    write_str("\n");
                    (void)identity_pop();
                    write_err_str(verb);
                    write_err_str(": password required\n");
                    return 1;
                }
                write_str("\n");
                token_arg = token;
            }
            rc = nexctl_auth_root_local(token_arg);
            if (rc < 0) {
                (void)identity_pop();
                write_err_str(verb);
                write_err_str(": authentication failed\n");
                return 1;
            }
            write_str(verb);
            write_str(": authenticated as root\n");
        } else {
            rc = identity_drop_user(target_uid);
            if (rc < 0) {
                (void)identity_pop();
                write_err_str(verb);
                write_err_str(": switch user failed rc=");
                write_sdec((int32_t)rc);
                write_err_str("\n");
                return 1;
            }
        }
    }
    if (target_uid == 0u && identity.uid == target_uid) {
        if (token_arg == NULL) {
            write_str("root password: ");
            if (read_line(STDIN_FILENO, token, sizeof(token)) == 0u ||
                token[0] == '\0') {
                write_str("\n");
                write_err_str(verb);
                write_err_str(": password required\n");
                return 1;
            }
            write_str("\n");
            token_arg = token;
        }
        rc = nexctl_auth_root_local(token_arg);
        if (rc < 0) {
            write_err_str(verb);
            write_err_str(": authentication failed\n");
            return 1;
        }
        write_str(verb);
        write_str(": authenticated as root\n");
    }
    /* An interactive identity switch replaces the current shell while the kernel
     * keeps the previous identity context for a later shell-level exit. */
    if (command_start >= argc) {
        rc = exec_replace(command);
        if (rc < 0) {
            (void)identity_pop();
            write_err_str(verb);
            write_err_str(": exec failed rc=");
            write_sdec((int32_t)rc);
            write_err_str("\n");
            return 1;
        }
        return 0;
    }
    rc = exec_command_local(verb, command);
    (void)identity_pop();
    return rc;
}

int cmd_su(int argc, char **argv) {
    return switch_identity_and_exec_local(argc, argv, "su", 0u);
}

int cmd_sudo(int argc, char **argv) {
    struct syscall_identity_info identity;
    char command[CMD_PATH_MAX];
    char token[64];
    int command_start = 1;
    int rc;

    if (argc > 1 && streq_local(argv[1], "--")) {
        command_start = 2;
    }
    if (command_start >= argc ||
        !cmd_build_program_command(argc,
                                   argv,
                                   command_start,
                                   "sudo",
                                   0,
                                   command,
                                   sizeof(command))) {
        write_err_usage("sudo", " [--] <command> [args]\n");
        return 1;
    }
    if (identity_get(&identity) < 0) {
        write_err_str("sudo: identity query failed\n");
        return 1;
    }
    rc = identity_push();
    if (rc < 0) {
        write_err_str("sudo: identity context push failed rc=");
        write_sdec((int32_t)rc);
        write_err_str("\n");
        return 1;
    }
    token[0] = '\0';
    write_str("root password: ");
    if (read_line(STDIN_FILENO, token, sizeof(token)) == 0u || token[0] == '\0') {
        write_str("\n");
        (void)identity_pop();
        write_err_str("sudo: password required\n");
        return 1;
    }
    write_str("\n");
    rc = nexctl_auth_root_local(token);
    if (rc < 0) {
        (void)identity_pop();
        write_err_str("sudo: authentication failed\n");
        return 1;
    }
    if (cmdsuite_has_command(argv[command_start])) {
        rc = cmdsuite_dispatch_main(argc - command_start, argv + command_start);
    } else {
        rc = run_foreground_command_local("sudo", command);
    }
    (void)identity_pop();
    return rc;
}

static int login_interactive_local(void) {
    struct syscall_identity_info identity;
    char name[32];
    char password[64];
    uint32_t target_uid;
    int rc;

    (void)tty_claim();
    for (;;) {
        name[0] = '\0';
        password[0] = '\0';
        write_str("name: ");
        if (read_line(STDIN_FILENO, name, sizeof(name)) == 0u ||
            name[0] == '\0') {
            write_str("\n");
            write_err_str("login: name required\n");
            continue;
        }
        write_str("\n");
        write_str("password: ");
        (void)read_line(STDIN_FILENO, password, sizeof(password));
        write_str("\n");

        if (!nexctl_user_id_local(name, &target_uid)) {
            write_err_str("login: unknown user: ");
            write_err_str(name);
            write_err_str("\n");
            continue;
        }
        if (identity_get(&identity) < 0) {
            write_err_str("login: identity query failed\n");
            continue;
        }
        if (target_uid == 0u) {
            rc = nexctl_auth_root_local(password);
            if (rc < 0) {
                write_err_str("login: authentication failed\n");
                continue;
            }
            write_str("login: authenticated as root\n");
        } else if (identity.uid != target_uid) {
            rc = identity_push();
            if (rc < 0) {
                write_err_str("login: identity context push failed rc=");
                write_sdec((int32_t)rc);
                write_err_str("\n");
                continue;
            }
            {
                uint32_t passwd_uid = 0u;
                int passwd_rc = nexctl_passwd_auth_local(name, password, &passwd_uid);

                if ((passwd_rc >= 0 && (passwd_rc == 0 || passwd_uid != target_uid)) ||
                    (passwd_rc < 0 && password[0] != '\0' && !streq_local(password, name))) {
                    (void)identity_pop();
                    write_err_str("login: authentication failed\n");
                    continue;
                }
                rc = identity_drop_user(target_uid);
                if (rc < 0) {
                    (void)identity_pop();
                    write_err_str("login: switch user failed rc=");
                    write_sdec((int32_t)rc);
                    write_err_str("\n");
                    continue;
                }
            }
        }
        rc = exec_replace("/cmd/ush");
        if (rc < 0) {
            (void)identity_pop();
            write_err_str("login: exec failed rc=");
            write_sdec((int32_t)rc);
            write_err_str("\n");
            write_err_str("login: shell failed\n");
            continue;
        }
    }
    return 1;
}

int cmd_login(int argc, char **argv) {
    if (argc == 1) {
        return login_interactive_local();
    }
    return switch_identity_and_exec_local(argc, argv, "login", 1000u);
}

int cmd_getty(int argc, char **argv) {
    int fd;
    int rc;

    if (argc != 2) {
        write_err_usage("getty", " <tty>\n");
        return 1;
    }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        return cmd_report_syscall_failure("getty", "tty open failed", fd);
    }
    if (fd != STDIN_FILENO && dup2(fd, STDIN_FILENO) < 0) {
        close((uint32_t)fd);
        write_err_str("getty: stdin dup failed\n");
        return 1;
    }
    if (fd != STDOUT_FILENO && dup2(fd, STDOUT_FILENO) < 0) {
        close((uint32_t)fd);
        write_err_str("getty: stdout dup failed\n");
        return 1;
    }
    if (fd != STDERR_FILENO && dup2(fd, STDERR_FILENO) < 0) {
        close((uint32_t)fd);
        write_err_str("getty: stderr dup failed\n");
        return 1;
    }
    if (fd > STDERR_FILENO) {
        close((uint32_t)fd);
    }
    for (;;) {
        struct syscall_process_info info;
        int fg_rc;
        int wait_rc;

        (void)tty_claim();
        write_str("\nNexOS ");
        write_str(argv[1]);
        write_str("\n");
        rc = spawn("/cmd/nexbox login", SYS_SPAWN_ELF, 0u);
        if (rc > 0) {
            fg_rc = fg((uint32_t)rc);
            wait_rc = wait((uint32_t)rc, &info);
            if (fg_rc > 0 && wait_rc > 0) {
                continue;
            }
            rc = fg_rc <= 0 ? fg_rc : wait_rc;
        }
        if (rc != 0) {
            write_err_str("getty: login failed rc=");
            write_sdec((int32_t)rc);
            write_err_str("\n");
            sleep(100u);
        }
    }
    return 1;
}

static int nexctl_user_local(int argc, char **argv) {
    uint32_t uid = 1000u;
    int rc;

    if (argc == 2 ||
        streq_ignore_case_local(argv[2], "status") ||
        streq_ignore_case_local(argv[2], "id")) {
        return nexctl_user_status_local();
    }
    if (streq_ignore_case_local(argv[2], "drop")) {
        if (argc > 4 || (argc == 4 && !parse_u32_local(argv[3], &uid))) {
            write_err_usage("nexctl user drop", " [uid]\n");
            return 1;
        }
        rc = identity_drop_user(uid);
        if (rc < 0) {
            write_err_str("nexctl user drop failed rc=");
            write_sdec((int32_t)rc);
            write_err_str("\n");
            return 1;
        }
        return nexctl_user_status_local();
    }
    if (streq_ignore_case_local(argv[2], "run")) {
        char command[CMD_PATH_MAX];
        int sep = 0;
        int command_start;

        for (int i = 3; i < argc; i++) {
            if (streq_local(argv[i], "--")) {
                sep = i;
                break;
            }
        }
        if (sep == 4) {
            if (!parse_u32_local(argv[3], &uid)) {
                write_err_usage("nexctl user run", " [uid] -- <command> [args]\n");
                return 1;
            }
        } else if (sep != 3) {
            write_err_usage("nexctl user run", " [uid] -- <command> [args]\n");
            return 1;
        }
        command_start = sep + 1;
        if (command_start >= argc ||
            !cmd_build_program_command(argc,
                                       argv,
                                       command_start,
                                       "nexctl user run",
                                       0,
                                       command,
                                       sizeof(command))) {
            return 1;
        }
        rc = identity_drop_user(uid);
        if (rc < 0) {
            write_err_str("nexctl user run: drop failed rc=");
            write_sdec((int32_t)rc);
            write_err_str("\n");
            return 1;
        }
        return run_foreground_command_local("nexctl user run", command);
    }
    if (streq_ignore_case_local(argv[2], "auth-root") ||
        streq_ignore_case_local(argv[2], "root")) {
        if (argc != 4) {
            write_err_usage("nexctl user auth-root", " TOKEN\n");
            return 1;
        }
        rc = nexctl_auth_root_local(argv[3]);
        if (rc < 0) {
            write_err_str("nexctl user auth-root failed\n");
            return 1;
        }
        return nexctl_user_status_local();
    }
    if (streq_ignore_case_local(argv[2], "root-run")) {
        char command[CMD_PATH_MAX];
        int sep = 0;

        for (int i = 4; i < argc; i++) {
            if (streq_local(argv[i], "--")) {
                sep = i;
                break;
            }
        }
        if (argc < 6 || sep != 4 || sep + 1 >= argc) {
            write_err_usage("nexctl user root-run", " TOKEN -- <command> [args]\n");
            return 1;
        }
        if (!cmd_build_program_command(argc,
                                       argv,
                                       sep + 1,
                                       "nexctl user root-run",
                                       0,
                                       command,
                                       sizeof(command))) {
            return 1;
        }
        rc = nexctl_auth_root_local(argv[3]);
        if (rc < 0) {
            write_err_str("nexctl user root-run: auth failed\n");
            return 1;
        }
        return run_foreground_command_local("nexctl user root-run", command);
    }
    write_err_usage("nexctl user", " [status|drop [uid]|run [uid] -- <command>|auth-root TOKEN|root-run TOKEN -- <command>]\n");
    return 1;
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
    write_str("  cap [args]        show, change, or constrain capabilities\n");
    write_str("  user [args]       show, drop, or authenticate process identity\n");
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
    if (streq_ignore_case_local(argv[1], "cap") ||
        streq_ignore_case_local(argv[1], "caps") ||
        streq_ignore_case_local(argv[1], "capability")) {
        return nexctl_cap_local(argc, argv);
    }
    if (streq_ignore_case_local(argv[1], "user") ||
        streq_ignore_case_local(argv[1], "identity")) {
        return nexctl_user_local(argc, argv);
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
        return cmd_ps(argc - 1, argv + 1);
    }

    write_err_str("nexctl: unknown command: ");
    write_err_str(argv[1]);
    write_err_str("\n");
    nexctl_help_local();
    return 1;
}
