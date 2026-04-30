#include "user/apps/elf/ush_shared.h"

static struct {
    uint8_t used;
    char name[USH_VAR_NAME_MAX + 1];
    char value[USH_VAR_VALUE_MAX + 1];
} g_ush_vars[USH_VAR_MAX];

static struct {
    uint8_t used;
    char name[USH_VAR_NAME_MAX + 1];
    char value[USH_LINE_MAX + 1];
} g_ush_aliases[USH_VAR_MAX];

static struct {
    uint8_t used;
    char name[USH_VAR_NAME_MAX + 1];
    char body[USH_FUNCTION_BODY_MAX + 1];
} g_ush_functions[USH_FUNCTION_MAX];

static struct {
    uint32_t argc;
    char argv[10][USH_VAR_VALUE_MAX + 1];
    char joined[USH_VAR_VALUE_MAX + 1];
    char count[12];
} g_ush_script_args;

static uint32_t str_len_local(const char *text) {
    uint32_t len = 0;

    while (text != NULL && text[len] != '\0') {
        len++;
    }
    return len;
}

static void copy_line_local(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (dst == NULL || max_len == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int streq_local(const char *a, const char *b) {
    uint32_t i = 0;

    for (;;) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
        i++;
    }
}

static int is_space_local(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void trim_in_place_local(char *text) {
    uint32_t start = 0;
    uint32_t end = str_len_local(text);
    uint32_t i = 0;

    while (text[start] != '\0' && is_space_local(text[start])) {
        start++;
    }
    while (end > start && is_space_local(text[end - 1u])) {
        end--;
    }
    while (start < end) {
        text[i++] = text[start++];
    }
    text[i] = '\0';
}

static int ush_var_name_char_local(char ch, int first) {
    if (ch >= 'A' && ch <= 'Z') {
        return 1;
    }
    if (ch >= 'a' && ch <= 'z') {
        return 1;
    }
    if (ch == '_') {
        return 1;
    }
    return !first && ch >= '0' && ch <= '9';
}

static int ush_var_find_local(const char *name) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (g_ush_vars[i].used && streq_local(g_ush_vars[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int contains_char_local(const char *text, char ch) {
    uint32_t i = 0;

    while (text != NULL && text[i] != '\0') {
        if (text[i] == ch) {
            return 1;
        }
        i++;
    }
    return 0;
}

static int ush_alias_find_local(const char *name) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (g_ush_aliases[i].used && streq_local(g_ush_aliases[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int ush_function_find_local(const char *name) {
    uint32_t i;

    for (i = 0; i < USH_FUNCTION_MAX; i++) {
        if (g_ush_functions[i].used && streq_local(g_ush_functions[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int ush_alias_alloc_local(void) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_aliases[i].used) {
            g_ush_aliases[i].used = 1u;
            g_ush_aliases[i].name[0] = '\0';
            g_ush_aliases[i].value[0] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static int ush_function_alloc_local(void) {
    uint32_t i;

    for (i = 0; i < USH_FUNCTION_MAX; i++) {
        if (!g_ush_functions[i].used) {
            g_ush_functions[i].used = 1u;
            g_ush_functions[i].name[0] = '\0';
            g_ush_functions[i].body[0] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static int ush_alias_store_local(int slot, const char *name, const char *value) {
    if (slot < 0 || slot >= (int)USH_VAR_MAX || !ush_var_name_valid_local(name)) {
        return 0;
    }
    if (str_len_local(name) > USH_VAR_NAME_MAX || str_len_local(value != NULL ? value : "") > USH_LINE_MAX) {
        return 0;
    }
    copy_line_local(g_ush_aliases[slot].name, name, sizeof(g_ush_aliases[slot].name));
    copy_line_local(g_ush_aliases[slot].value, value != NULL ? value : "", sizeof(g_ush_aliases[slot].value));
    return 1;
}

static int ush_function_store_local(int slot, const char *name, const char *body) {
    if (slot < 0 || slot >= (int)USH_FUNCTION_MAX || !ush_var_name_valid_local(name)) {
        return 0;
    }
    if (str_len_local(name) > USH_VAR_NAME_MAX ||
        str_len_local(body != NULL ? body : "") > USH_FUNCTION_BODY_MAX) {
        return 0;
    }
    copy_line_local(g_ush_functions[slot].name, name, sizeof(g_ush_functions[slot].name));
    copy_line_local(g_ush_functions[slot].body, body != NULL ? body : "", sizeof(g_ush_functions[slot].body));
    return 1;
}

static const char *ush_alias_lookup_local(const char *name) {
    int slot = ush_alias_find_local(name);

    if (slot < 0) {
        return NULL;
    }
    return g_ush_aliases[slot].value;
}

static int ush_glob_match_star_local(const char *pattern, const char *text) {
    uint32_t p = 0;
    uint32_t t = 0;
    uint32_t star = 0xffffffffu;
    uint32_t retry = 0;

    while (text[t] != '\0') {
        if (pattern[p] == '*') {
            star = p++;
            retry = t;
            continue;
        }
        if (pattern[p] == text[t]) {
            p++;
            t++;
            continue;
        }
        if (star != 0xffffffffu) {
            p = star + 1u;
            t = ++retry;
            continue;
        }
        return 0;
    }
    while (pattern[p] == '*') {
        p++;
    }
    return pattern[p] == '\0';
}

static int ush_parse_token_local(const char *text,
                                 uint32_t *pos_io,
                                 char *cooked,
                                 uint32_t cooked_size,
                                 uint32_t *raw_start_out,
                                 uint32_t *raw_end_out,
                                 int *quoted_out) {
    uint32_t pos;
    uint32_t out_len = 0;
    int single_quote = 0;
    int double_quote = 0;
    int quoted = 0;

    if (text == NULL || pos_io == NULL || cooked == NULL || cooked_size == 0) {
        return 0;
    }
    pos = *pos_io;
    while (is_space_local(text[pos])) {
        pos++;
    }
    if (text[pos] == '\0') {
        cooked[0] = '\0';
        *pos_io = pos;
        return 0;
    }
    if (raw_start_out != NULL) {
        *raw_start_out = pos;
    }
    while (text[pos] != '\0' && (single_quote || double_quote || !is_space_local(text[pos]))) {
        char ch = text[pos];

        if (!single_quote && ch == '\\') {
            pos++;
            if (text[pos] == '\0') {
                break;
            }
            ch = text[pos++];
        } else if (!double_quote && ch == '\'') {
            quoted = 1;
            single_quote = !single_quote;
            pos++;
            continue;
        } else if (!single_quote && ch == '"') {
            quoted = 1;
            double_quote = !double_quote;
            pos++;
            continue;
        } else {
            pos++;
        }

        if (out_len + 1u >= cooked_size) {
            return 0;
        }
        cooked[out_len++] = ch;
    }
    if (single_quote || double_quote) {
        return 0;
    }
    cooked[out_len] = '\0';
    if (raw_end_out != NULL) {
        *raw_end_out = pos;
    }
    if (quoted_out != NULL) {
        *quoted_out = quoted;
    }
    *pos_io = pos;
    return out_len != 0;
}

static int ush_append_token_local(char *out, uint32_t out_size, uint32_t *out_pos, const char *token) {
    uint32_t i = 0;

    if (*out_pos != 0) {
        if (*out_pos + 1u >= out_size) {
            return 0;
        }
        out[(*out_pos)++] = ' ';
    }
    while (token[i] != '\0') {
        if (*out_pos + 1u >= out_size) {
            return 0;
        }
        out[(*out_pos)++] = token[i++];
    }
    out[*out_pos] = '\0';
    return 1;
}

static int ush_expand_glob_token_local(const char *token, char *out, uint32_t out_size, uint32_t *out_pos) {
    char dir_path[USH_LINE_MAX + 1];
    char pattern[USH_LINE_MAX + 1];
    char prefix[USH_LINE_MAX + 1];
    struct syscall_dirent entry;
    uint32_t len = str_len_local(token);
    uint32_t last_slash = 0xffffffffu;
    uint32_t i;
    int fd;
    int matched = 0;

    for (i = 0; i < len; i++) {
        if (token[i] == '/') {
            last_slash = i;
        }
    }

    if (last_slash == 0xffffffffu) {
        copy_line_local(dir_path, ".", sizeof(dir_path));
        copy_line_local(prefix, "", sizeof(prefix));
        copy_line_local(pattern, token, sizeof(pattern));
    } else {
        if (last_slash == 0u) {
            copy_line_local(dir_path, "/", sizeof(dir_path));
            copy_line_local(prefix, "/", sizeof(prefix));
        } else {
            for (i = 0; i < last_slash; i++) {
                dir_path[i] = token[i];
                prefix[i] = token[i];
            }
            dir_path[last_slash] = '\0';
            prefix[last_slash] = '/';
            prefix[last_slash + 1u] = '\0';
        }
        copy_line_local(pattern, token + last_slash + 1u, sizeof(pattern));
    }

    fd = opendir(dir_path);
    if (fd < 0) {
        return ush_append_token_local(out, out_size, out_pos, token);
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        char candidate[USH_LINE_MAX + 1];

        if (!ush_glob_match_star_local(pattern, entry.name)) {
            continue;
        }
        candidate[0] = '\0';
        copy_line_local(candidate, prefix, sizeof(candidate));
        copy_line_local(candidate + str_len_local(candidate),
                        entry.name,
                        sizeof(candidate) - str_len_local(candidate));
        if (!ush_append_token_local(out, out_size, out_pos, candidate)) {
            close((uint32_t)fd);
            return 0;
        }
        matched = 1;
    }
    close((uint32_t)fd);
    if (!matched) {
        return ush_append_token_local(out, out_size, out_pos, token);
    }
    return 1;
}

static const char *ush_var_lookup_local(const char *name) {
    int slot;
    char *env_value;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    slot = ush_var_find_local(name);
    if (slot >= 0) {
        return g_ush_vars[slot].value;
    }
    env_value = getenv(name);
    return env_value != NULL ? env_value : NULL;
}

static const char *ush_special_var_lookup_local(const char *name) {
    uint32_t index = 0;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    if (name[1] == '\0') {
        if (name[0] == '#') {
            return g_ush_script_args.count;
        }
        if (name[0] == '@' || name[0] == '*') {
            return g_ush_script_args.joined;
        }
        if (name[0] >= '0' && name[0] <= '9') {
            index = (uint32_t)(name[0] - '0');
            return index < 10u ? g_ush_script_args.argv[index] : NULL;
        }
    }
    return NULL;
}

static int ush_var_alloc_local(void) {
    uint32_t i;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_vars[i].used) {
            g_ush_vars[i].used = 1u;
            g_ush_vars[i].name[0] = '\0';
            g_ush_vars[i].value[0] = '\0';
            return (int)i;
        }
    }
    return -1;
}

static int ush_var_store_local(int slot, const char *name, const char *value) {
    if (slot < 0 || slot >= (int)USH_VAR_MAX || !ush_var_name_valid_local(name)) {
        return 0;
    }
    if (str_len_local(name) > USH_VAR_NAME_MAX ||
        str_len_local(value != NULL ? value : "") > USH_VAR_VALUE_MAX) {
        return 0;
    }
    copy_line_local(g_ush_vars[slot].name, name, sizeof(g_ush_vars[slot].name));
    copy_line_local(g_ush_vars[slot].value, value != NULL ? value : "", sizeof(g_ush_vars[slot].value));
    return 1;
}

static void ush_var_clear_slot_local(int slot) {
    if (slot < 0 || slot >= (int)USH_VAR_MAX) {
        return;
    }
    g_ush_vars[slot].used = 0u;
    g_ush_vars[slot].name[0] = '\0';
    g_ush_vars[slot].value[0] = '\0';
}

static void ush_sync_pwd_var_local(const char *cwd) {
    (void)ush_var_assign_local("PWD", cwd != NULL ? cwd : "/", 1);
}

int ush_var_name_valid_local(const char *name) {
    uint32_t i = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    while (name[i] != '\0') {
        if (!ush_var_name_char_local(name[i], i == 0u)) {
            return 0;
        }
        i++;
    }
    return 1;
}

int ush_var_assign_local(const char *name, const char *value, int exported_if_new) {
    int slot = ush_var_find_local(name);
    char *env_value = getenv(name);

    if (slot >= 0) {
        if (!ush_var_store_local(slot, name, value)) {
            return 0;
        }
        return 1;
    }
    if (env_value != NULL || exported_if_new) {
        return setenv(name, value != NULL ? value : "", 1) == 0;
    }
    slot = ush_var_alloc_local();
    if (slot < 0) {
        return 0;
    }
    if (!ush_var_store_local(slot, name, value)) {
        ush_var_clear_slot_local(slot);
        return 0;
    }
    return 1;
}

int ush_var_export_local(const char *name, const char *value) {
    int slot;
    const char *current_value;

    if (value != NULL) {
        slot = ush_var_find_local(name);
        if (slot >= 0) {
            ush_var_clear_slot_local(slot);
        }
        return setenv(name, value, 1) == 0;
    }

    slot = ush_var_find_local(name);
    if (slot >= 0) {
        current_value = g_ush_vars[slot].value;
        if (setenv(name, current_value, 1) != 0) {
            return 0;
        }
        ush_var_clear_slot_local(slot);
        return 1;
    }

    current_value = getenv(name);
    if (current_value != NULL) {
        return 1;
    }
    return setenv(name, "", 1) == 0;
}

void ush_var_list_local(int exported_only) {
    uint32_t i = 0;
    int listed = 0;

    (void)exported_only;
    while (environ != NULL && environ[i] != NULL) {
        write_str(environ[i]);
        write_str("\n");
        listed = 1;
        i++;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}

void ush_var_list_shell_local(void) {
    uint32_t i;
    int listed = 0;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_vars[i].used) {
            continue;
        }
        write_str(g_ush_vars[i].name);
        write_str("=");
        write_str(g_ush_vars[i].value);
        write_str("\n");
        listed = 1;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}

int ush_alias_assign_local(const char *name, const char *value) {
    int slot = ush_alias_find_local(name);

    if (slot < 0) {
        slot = ush_alias_alloc_local();
    }
    if (slot < 0) {
        return 0;
    }
    return ush_alias_store_local(slot, name, value);
}

void ush_alias_list_local(void) {
    uint32_t i;
    int listed = 0;

    for (i = 0; i < USH_VAR_MAX; i++) {
        if (!g_ush_aliases[i].used) {
            continue;
        }
        write_str("alias ");
        write_str(g_ush_aliases[i].name);
        write_str("=");
        write_str(g_ush_aliases[i].value);
        write_str("\n");
        listed = 1;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}

int ush_function_assign_local(const char *name, const char *body) {
    int slot = ush_function_find_local(name);

    if (slot < 0) {
        slot = ush_function_alloc_local();
    }
    if (slot < 0) {
        return 0;
    }
    return ush_function_store_local(slot, name, body);
}

const char *ush_function_lookup_local(const char *name) {
    int slot = ush_function_find_local(name);

    if (slot < 0) {
        return NULL;
    }
    return g_ush_functions[slot].body;
}

void ush_function_list_local(void) {
    uint32_t i;
    int listed = 0;

    for (i = 0; i < USH_FUNCTION_MAX; i++) {
        if (!g_ush_functions[i].used) {
            continue;
        }
        write_str(g_ush_functions[i].name);
        write_str(" { ");
        write_str(g_ush_functions[i].body);
        write_str("; }\n");
        listed = 1;
    }
    if (!listed) {
        write_str("<empty>\n");
    }
}

int ush_expand_command_text_local(const char *text, char *out, uint32_t out_size) {
    char token[USH_LINE_MAX + 1];
    uint32_t in_pos = 0;
    uint32_t out_pos = 0;
    int token_index = 0;

    if (text == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    while (text[in_pos] != '\0') {
        uint32_t raw_start = 0;
        uint32_t raw_end = 0;
        const char *emit = token;
        const char *alias_value;
        int quoted = 0;

        if (!ush_parse_token_local(text,
                                   &in_pos,
                                   token,
                                   sizeof(token),
                                   &raw_start,
                                   &raw_end,
                                   &quoted)) {
            break;
        }

        if (token_index == 0 && !quoted) {
            alias_value = ush_alias_lookup_local(token);
            if (alias_value != NULL && alias_value[0] != '\0') {
                emit = alias_value;
            }
        }

        if (!quoted && contains_char_local(emit, '*')) {
            if (!ush_expand_glob_token_local(emit, out, out_size, &out_pos)) {
                return 0;
            }
        } else {
            if (emit != token) {
                if (!ush_append_token_local(out, out_size, &out_pos, emit)) {
                    return 0;
                }
            } else {
                uint32_t i;

                if (out_pos != 0) {
                    if (out_pos + 1u >= out_size) {
                        return 0;
                    }
                    out[out_pos++] = ' ';
                }
                for (i = raw_start; i < raw_end; i++) {
                    if (out_pos + 1u >= out_size) {
                        return 0;
                    }
                    out[out_pos++] = text[i];
                }
                out[out_pos] = '\0';
            }
        }
        token_index++;
    }
    return 1;
}

int ush_parse_assignment_local(const char *text,
                               char *name,
                               uint32_t name_size,
                               char *value,
                               uint32_t value_size) {
    char buffer[USH_LINE_MAX + 1];
    uint32_t pos = 0;
    uint32_t eq = 0xffffffffu;
    uint32_t value_pos = 0;

    if (text == NULL || name == NULL || value == NULL || name_size == 0 || value_size == 0) {
        return 0;
    }
    copy_line_local(buffer, text, sizeof(buffer));
    trim_in_place_local(buffer);
    if (buffer[0] == '\0') {
        return 0;
    }
    while (buffer[pos] != '\0') {
        if (buffer[pos] == '=') {
            eq = pos;
            break;
        }
        pos++;
    }
    if (eq == 0xffffffffu || eq == 0u || eq + 1u > sizeof(buffer)) {
        return 0;
    }
    buffer[eq] = '\0';
    trim_in_place_local(buffer);
    if (!ush_var_name_valid_local(buffer)) {
        return 0;
    }
    copy_line_local(name, buffer, name_size);
    while (text[value_pos] != '\0') {
        if (text[value_pos] == '=') {
            value_pos++;
            break;
        }
        value_pos++;
    }
    copy_line_local(value, text + value_pos, value_size);
    return 1;
}

int ush_expand_variables_local(const char *text, char *out, uint32_t out_size) {
    uint32_t in_pos = 0;
    uint32_t out_pos = 0;

    if (text == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    while (text[in_pos] != '\0') {
        if (text[in_pos] == '$') {
            char name[USH_VAR_NAME_MAX + 1];
            uint32_t name_len = 0;
            uint32_t cursor = in_pos + 1u;
            int brace = 0;
            const char *value_text;
            uint32_t value_len;
            uint32_t i;

            if (text[cursor] == '{') {
                brace = 1;
                cursor++;
            }
            if (text[cursor] == '#' || text[cursor] == '@' || text[cursor] == '*' ||
                (text[cursor] >= '0' && text[cursor] <= '9')) {
                name[name_len++] = text[cursor++];
            } else if (!ush_var_name_char_local(text[cursor], 1)) {
                if (out_pos + 1u >= out_size) {
                    return 0;
                }
                out[out_pos++] = text[in_pos++];
                continue;
            } else {
                while (text[cursor] != '\0' && ush_var_name_char_local(text[cursor], name_len == 0u)) {
                    if (name_len + 1u >= sizeof(name)) {
                        return 0;
                    }
                    name[name_len++] = text[cursor++];
                }
            }
            if (brace) {
                if (text[cursor] != '}') {
                    return 0;
                }
                cursor++;
            }
            name[name_len] = '\0';
            value_text = ush_special_var_lookup_local(name);
            if (value_text == NULL) {
                value_text = ush_var_lookup_local(name);
            }
            value_len = str_len_local(value_text != NULL ? value_text : "");
            if (out_pos + value_len + 1u > out_size) {
                return 0;
            }
            for (i = 0; i < value_len; i++) {
                out[out_pos++] = value_text[i];
            }
            in_pos = cursor;
            continue;
        }
        if (out_pos + 1u >= out_size) {
            return 0;
        }
        out[out_pos++] = text[in_pos++];
    }
    out[out_pos] = '\0';
    return 1;
}

void ush_refresh_cwd_local(char *cwd, uint32_t cwd_size) {
    if (cwd == NULL || cwd_size == 0) {
        return;
    }
    if (getcwd(cwd, cwd_size) < 0 || cwd[0] == '\0') {
        copy_line_local(cwd, "/", cwd_size);
    }
    ush_sync_pwd_var_local(cwd);
}

void ush_init_vars_local(const char *cwd) {
    uint32_t i = 0;

    while (i < USH_VAR_MAX) {
        g_ush_vars[i].used = 0u;
        g_ush_vars[i].name[0] = '\0';
        g_ush_vars[i].value[0] = '\0';
        g_ush_aliases[i].used = 0u;
        g_ush_aliases[i].name[0] = '\0';
        g_ush_aliases[i].value[0] = '\0';
        i++;
    }
    if (getenv("PATH") == NULL) {
        (void)setenv("PATH", "/CMD", 1);
    }
    if (getenv("SHELL") == NULL) {
        (void)setenv("SHELL", "/CMD/USH", 1);
    }
    ush_clear_script_args_local();
    ush_sync_pwd_var_local(cwd);
}

void ush_set_script_args_local(int argc, char **argv) {
    uint32_t i;
    uint32_t joined_len = 0;
    uint32_t count = 0;

    ush_clear_script_args_local();
    if (argc <= 0 || argv == NULL) {
        return;
    }
    for (i = 0; i < 10u && i < (uint32_t)argc; i++) {
        copy_line_local(g_ush_script_args.argv[i], argv[i] != NULL ? argv[i] : "", sizeof(g_ush_script_args.argv[i]));
    }
    g_ush_script_args.argc = (uint32_t)argc;
    count = (uint32_t)argc > 0u ? (uint32_t)argc - 1u : 0u;
    (void)snprintf(g_ush_script_args.count, sizeof(g_ush_script_args.count), "%u", count);

    for (i = 1u; i < (uint32_t)argc; i++) {
        const char *arg = argv[i] != NULL ? argv[i] : "";
        uint32_t arg_len = str_len_local(arg);
        uint32_t j;

        if (arg_len == 0) {
            continue;
        }
        if (joined_len != 0u) {
            if (joined_len + 1u >= sizeof(g_ush_script_args.joined)) {
                break;
            }
            g_ush_script_args.joined[joined_len++] = ' ';
        }
        for (j = 0; j < arg_len; j++) {
            if (joined_len + 1u >= sizeof(g_ush_script_args.joined)) {
                break;
            }
            g_ush_script_args.joined[joined_len++] = arg[j];
        }
        if (joined_len + 1u >= sizeof(g_ush_script_args.joined)) {
            break;
        }
    }
    g_ush_script_args.joined[joined_len] = '\0';
}

void ush_clear_script_args_local(void) {
    uint32_t i;

    g_ush_script_args.argc = 0u;
    g_ush_script_args.joined[0] = '\0';
    copy_line_local(g_ush_script_args.count, "0", sizeof(g_ush_script_args.count));
    for (i = 0; i < 10u; i++) {
        g_ush_script_args.argv[i][0] = '\0';
    }
}

void ush_save_script_args_local(struct ush_script_args_snapshot *out) {
    uint32_t i;

    if (out == NULL) {
        return;
    }
    out->argc = g_ush_script_args.argc;
    copy_line_local(out->joined, g_ush_script_args.joined, sizeof(out->joined));
    copy_line_local(out->count, g_ush_script_args.count, sizeof(out->count));
    for (i = 0; i < 10u; i++) {
        copy_line_local(out->argv[i], g_ush_script_args.argv[i], sizeof(out->argv[i]));
    }
}

void ush_restore_script_args_local(const struct ush_script_args_snapshot *snapshot) {
    uint32_t i;

    if (snapshot == NULL) {
        return;
    }
    g_ush_script_args.argc = snapshot->argc;
    copy_line_local(g_ush_script_args.joined, snapshot->joined, sizeof(g_ush_script_args.joined));
    copy_line_local(g_ush_script_args.count, snapshot->count, sizeof(g_ush_script_args.count));
    for (i = 0; i < 10u; i++) {
        copy_line_local(g_ush_script_args.argv[i], snapshot->argv[i], sizeof(g_ush_script_args.argv[i]));
    }
}
