#include "user/apps/elf/ush/ush_exec_internal.h"

static const char ush_builtin_ansi_reset[] = "\x1b[0m";
static const char *g_ush_action_caps_path = "/HOME/ACTION.CAPS";

enum {
    USH_FUNCTION_CALL_DEPTH_MAX = 4u,
    USH_ACTION_POLICY_FILE_MAX = 768u
};

static uint32_t g_ush_function_call_depth;
static int g_ush_function_recursion_limit_enabled = 1;

static void ush_policy_read_word(const char **cursor_io, char *out, uint32_t out_size) {
    const char *cursor;
    uint32_t pos = 0;

    if (out == NULL || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (cursor_io == NULL || *cursor_io == NULL) {
        return;
    }
    cursor = *cursor_io;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' &&
           *cursor != ' ' && *cursor != '\t') {
        if (pos + 1u < out_size) {
            out[pos++] = *cursor;
        }
        cursor++;
    }
    out[pos] = '\0';
    *cursor_io = cursor;
}

static int ush_parse_cap_mask_local(const char *text, uint32_t *mask_out) {
    char *end = NULL;
    unsigned long value;

    if (text == NULL || mask_out == NULL || text[0] == '\0') {
        return 0;
    }
    if (streq_local(text, "all")) {
        *mask_out = USH_ACTION_CAP_ALL;
        return 1;
    }
    if (streq_local(text, "device.read")) {
        *mask_out = USH_ACTION_CAP_DEVICE_READ;
        return 1;
    }
    if (streq_local(text, "device.write")) {
        *mask_out = USH_ACTION_CAP_DEVICE_WRITE;
        return 1;
    }
    value = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return 0;
    }
    *mask_out = (uint32_t)value;
    return 1;
}

static void ush_policy_apply_line(uint32_t *mask_io, const char *line) {
    char verb[16];
    char kind[16];
    char value[32];
    uint32_t cap = 0;
    const char *cursor = line;

    if (mask_io == NULL || line == NULL) {
        return;
    }
    ush_policy_read_word(&cursor, verb, sizeof(verb));
    if (verb[0] == '\0' || verb[0] == '#') {
        return;
    }
    if (verb[0] >= '0' && verb[0] <= '9') {
        if (ush_parse_cap_mask_local(verb, &cap)) {
            *mask_io = cap & USH_ACTION_CAP_ALL;
        }
        return;
    }
    ush_policy_read_word(&cursor, kind, sizeof(kind));
    ush_policy_read_word(&cursor, value, sizeof(value));
    if (streq_local(verb, "mask")) {
        if (ush_parse_cap_mask_local(kind, &cap)) {
            *mask_io = cap & USH_ACTION_CAP_ALL;
        }
        return;
    }
    if (streq_local(kind, "cap") && ush_parse_cap_mask_local(value, &cap)) {
        if (streq_local(verb, "allow")) {
            *mask_io |= cap;
        } else if (streq_local(verb, "deny")) {
            *mask_io &= ~cap;
        }
    }
}

static uint32_t ush_policy_load_mask(void) {
    char buffer[USH_ACTION_POLICY_FILE_MAX];
    char line[96];
    uint32_t mask = USH_ACTION_CAP_ALL;
    uint32_t pos = 0;
    int fd = open(g_ush_action_caps_path, 0);
    uint32_t got;

    if (fd < 0) {
        return mask;
    }
    got = (uint32_t)read(fd, buffer, sizeof(buffer) - 1u);
    close(fd);
    buffer[got] = '\0';
    while (buffer[pos] != '\0') {
        uint32_t line_pos = 0;

        while (buffer[pos] != '\0' && buffer[pos] != '\n' && line_pos + 1u < sizeof(line)) {
            line[line_pos++] = buffer[pos++];
        }
        while (buffer[pos] != '\0' && buffer[pos] != '\n') {
            pos++;
        }
        if (buffer[pos] == '\n') {
            pos++;
        }
        line[line_pos] = '\0';
        ush_policy_apply_line(&mask, line);
    }
    return mask;
}

static int ush_path_targets_devfs(const char *cwd, const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (starts_with_local(path, "/dev/")) {
        return 1;
    }
    return path[0] != '/' && cwd != NULL && streq_local(cwd, "/dev");
}

int ush_check_device_redirect_cap(const char *cwd, const char *path, uint32_t cap, const char *op) {
    if (!ush_path_targets_devfs(cwd, path)) {
        return 1;
    }
    if ((ush_policy_load_mask() & cap) != 0u) {
        return 1;
    }
    write_err_str("redirect: denied ");
    write_err_str(op);
    write_err_str(": ");
    write_err_str(path);
    write_err_str("\n");
    return 0;
}

void ush_write_colored_err(const char *ansi, const char *text) {
    write_err_str(ansi);
    write_err_str(text);
    write_err_str(ush_builtin_ansi_reset);
}

static int ush_line_has_function_operator_local(const char *line) {
    uint32_t pos = 0;
    int single_quote = 0;
    int double_quote = 0;

    if (line == NULL) {
        return 0;
    }
    while (line[pos] != '\0') {
        if (!single_quote && line[pos] == '\\' && line[pos + 1u] != '\0') {
            pos += 2u;
            continue;
        }
        if (!double_quote && line[pos] == '\'') {
            single_quote = !single_quote;
        } else if (!single_quote && line[pos] == '"') {
            double_quote = !double_quote;
        } else if (!single_quote && !double_quote &&
                   (line[pos] == ';' || line[pos] == '|' || line[pos] == '&' ||
                    line[pos] == '<' || line[pos] == '>')) {
            return 1;
        }
        pos++;
    }
    return 0;
}

int ush_try_function_call_local(char *cwd, const char *line, int require_plain_line, int *handled_out) {
    char token[64];
    const char *cursor = line;
    const char *function_body;
    int rc;

    if (handled_out == NULL) {
        return 1;
    }
    *handled_out = 0;
    if (require_plain_line && ush_line_has_function_operator_local(line)) {
        return 0;
    }
    if (!read_token_local(&cursor, token, sizeof(token))) {
        return 0;
    }
    function_body = ush_function_lookup_local(token);
    if (function_body == NULL) {
        return 0;
    }
    *handled_out = 1;
    if (g_ush_function_recursion_limit_enabled &&
        g_ush_function_call_depth >= USH_FUNCTION_CALL_DEPTH_MAX) {
        write_err_str("function: recursion limit\n");
        return 1;
    }
    g_ush_function_call_depth++;
    rc = ush_execute_line(cwd, function_body);
    g_ush_function_call_depth--;
    return rc;
}

void ush_function_recursion_limit_set(int enabled) {
    g_ush_function_recursion_limit_enabled = enabled ? 1 : 0;
}

int ush_change_directory(char *cwd, uint32_t cwd_size, const char *arg) {
    if (arg == NULL || arg[0] == '\0') {
        if (chdir("/") != 0) {
            ush_write_error("cd: no such directory\n");
            return 1;
        }
        ush_refresh_cwd_local(cwd, cwd_size);
        return 0;
    }
    if (chdir(arg) != 0) {
        ush_write_error("cd: no such directory\n");
        return 1;
    }
    ush_refresh_cwd_local(cwd, cwd_size);
    return 0;
}

int ush_preload_file_local(const char *path) {
    static uint8_t buffer[16384];
    int fd;

    if (path == NULL || path[0] == '\0') {
        write_err_str("usage: preload <file>\n");
        return 1;
    }
    fd = open(path, 0);
    if (fd < 0) {
        write_err_str("preload: open failed: ");
        write_err_str(path);
        write_err_str("\n");
        return 1;
    }
    for (;;) {
        ssize_t got = read((uint32_t)fd, buffer, sizeof(buffer));

        if (got < 0) {
            close((uint32_t)fd);
            write_err_str("preload: read failed: ");
            write_err_str(path);
            write_err_str("\n");
            return 1;
        }
        if (got == 0) {
            break;
        }
    }
    close((uint32_t)fd);
    return 0;
}
