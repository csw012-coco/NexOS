#include "user/apps/elf/ush/ush_vars_internal.h"

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
    while (ush_vars_is_space(text[pos])) {
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
    while (text[pos] != '\0' && (single_quote || double_quote || !ush_vars_is_space(text[pos]))) {
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
    return out_len != 0 || quoted;
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
    uint32_t len = ush_vars_strlen(token);
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
        ush_vars_copy_line(dir_path, ".", sizeof(dir_path));
        ush_vars_copy_line(prefix, "", sizeof(prefix));
        ush_vars_copy_line(pattern, token, sizeof(pattern));
    } else {
        if (last_slash == 0u) {
            ush_vars_copy_line(dir_path, "/", sizeof(dir_path));
            ush_vars_copy_line(prefix, "/", sizeof(prefix));
        } else {
            for (i = 0; i < last_slash; i++) {
                dir_path[i] = token[i];
                prefix[i] = token[i];
            }
            dir_path[last_slash] = '\0';
            prefix[last_slash] = '/';
            prefix[last_slash + 1u] = '\0';
        }
        ush_vars_copy_line(pattern, token + last_slash + 1u, sizeof(pattern));
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
        ush_vars_copy_line(candidate, prefix, sizeof(candidate));
        ush_vars_copy_line(candidate + ush_vars_strlen(candidate),
                           entry.name,
                           sizeof(candidate) - ush_vars_strlen(candidate));
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
            value_len = ush_vars_strlen(value_text != NULL ? value_text : "");
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
