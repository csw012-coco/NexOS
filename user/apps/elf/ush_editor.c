#include "user/apps/elf/ush_shared.h"
#include "user/libc/include/unistd.h"

static char g_ush_prompt_path[64] = "/";
static const char *g_ush_prompt_override = NULL;
static const char ush_ansi_reset[] = "\x1b[0m";
static const char ush_ansi_error[] = "\x1b[1;31m";
static char g_ush_history[USH_HISTORY_MAX][USH_LINE_MAX + 1];
static uint32_t g_ush_history_len = 0;
static uint32_t g_ush_history_next = 0;

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

static void write_dec_local(uint32_t value) {
    char buf[11];
    uint32_t pos = 0;

    if (value == 0) {
        write_stdout("0", 1);
        return;
    }
    while (value != 0) {
        buf[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (pos != 0) {
        write_stdout(&buf[--pos], 1);
    }
}

static int is_space_local(char ch) {
    return ch == ' ' || ch == '\t';
}

static char ascii_lower_local(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
}

static int starts_with_ignore_case_local(const char *text, const char *prefix) {
    uint32_t i = 0;

    while (prefix[i] != '\0') {
        if (ascii_lower_local(text[i]) != ascii_lower_local(prefix[i])) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int ush_read_char_nonblock(char *out) {
    return nex_read(STDIN_FILENO, out, 1u, NEX_READ_NONBLOCK | NEX_READ_CHAR) > 0;
}

static int ush_read_escape_char(char *out) {
    for (uint32_t i = 0; i < 4u; i++) {
        if (ush_read_char_nonblock(out)) {
            return 1;
        }
        yield();
    }
    return 0;
}

static void lowercase_copy_local(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = ascii_lower_local(src[i]);
        i++;
    }
    dst[i] = '\0';
}

static void ush_format_prompt(char *out, uint32_t out_size) {
    char cwd[128];

    if (out == NULL || out_size == 0) {
        return;
    }
    if (getcwd(cwd, sizeof(cwd)) < 0 || cwd[0] == '\0') {
        cwd[0] = '/';
        cwd[1] = '\0';
    }
    if (snprintf(out, out_size, "[ush@%s]> ", cwd) < 0) {
        copy_line_local(out, "[ush@/]> ", out_size);
    }
}

static uint32_t ush_prompt_display_width(void) {
    char prompt[160];

    if (g_ush_prompt_override != NULL) {
        return str_len_local(g_ush_prompt_override);
    }
    ush_format_prompt(prompt, sizeof(prompt));
    return str_len_local(prompt );
}

static void ush_ansi_clear_line(void) {
    write_str("\r\x1b[2K");
}

static void ush_ansi_set_column(uint32_t column) {
    write_str("\x1b[");
    write_dec_local(column);
    write_str("G");
}

static void ush_write_colored_err(const char *ansi, const char *text) {
    write_err_str(ansi);
    write_err_str(text);
    write_err_str(ush_ansi_reset);
}

static void ush_editor_set_line(struct ush_editor *editor, const char *text) {
    copy_line_local(editor->line, text, sizeof(editor->line));
    editor->len = str_len_local(editor->line);
    editor->cursor = editor->len;
}

static void ush_editor_render(const struct ush_editor *editor) {
    uint32_t cursor_column = ush_prompt_display_width() + editor->cursor + 1u;

    ush_ansi_clear_line();
    if (g_ush_prompt_override != NULL) {
        write_str(g_ush_prompt_override);
    } else {
        ush_write_prompt();
    }
    if (editor->len != 0) {
        write_stdout(editor->line, editor->len);
    }
    ush_ansi_set_column(cursor_column);
}

static void ush_editor_sync_rendered_len(struct ush_editor *editor) {
    editor->rendered_len = editor->len;
}

static void ush_editor_insert_char(struct ush_editor *editor, char ch) {
    uint32_t i;

    if (editor->len >= USH_LINE_MAX) {
        return;
    }
    for (i = editor->len; i > editor->cursor; i--) {
        editor->line[i] = editor->line[i - 1u];
    }
    editor->line[editor->cursor] = ch;
    editor->len++;
    editor->cursor++;
    editor->line[editor->len] = '\0';
}

static void ush_editor_backspace(struct ush_editor *editor) {
    uint32_t i;

    if (editor->cursor == 0 || editor->len == 0) {
        return;
    }
    for (i = editor->cursor - 1u; i < editor->len; i++) {
        editor->line[i] = editor->line[i + 1u];
    }
    editor->cursor--;
    editor->len--;
}

static void ush_editor_delete(struct ush_editor *editor) {
    uint32_t i;

    if (editor->cursor >= editor->len) {
        return;
    }
    for (i = editor->cursor; i < editor->len; i++) {
        editor->line[i] = editor->line[i + 1u];
    }
    editor->len--;
}

static void ush_editor_move_home(struct ush_editor *editor) {
    editor->cursor = 0;
}

static void ush_editor_move_end(struct ush_editor *editor) {
    editor->cursor = editor->len;
}

static void ush_editor_history_store(struct ush_editor *editor, const char *line) {
    (void)editor;
    if (line[0] == '\0') {
        return;
    }
    copy_line_local(g_ush_history[g_ush_history_next], line, sizeof(g_ush_history[0]));
    g_ush_history_next = (g_ush_history_next + 1u) % USH_HISTORY_MAX;
    if (g_ush_history_len < USH_HISTORY_MAX) {
        g_ush_history_len++;
    }
}

static int ush_editor_history_load(struct ush_editor *editor, int32_t history_index) {
    uint32_t slot;

    if (history_index < 0 || (uint32_t)history_index >= g_ush_history_len) {
        return 0;
    }
    slot = (g_ush_history_next + USH_HISTORY_MAX - g_ush_history_len + (uint32_t)history_index) % USH_HISTORY_MAX;
    ush_editor_set_line(editor, g_ush_history[slot]);
    return 1;
}

static void ush_editor_history_up(struct ush_editor *editor) {
    if (g_ush_history_len == 0) {
        return;
    }
    if (editor->history_index < 0) {
        copy_line_local(editor->scratch, editor->line, sizeof(editor->scratch));
        editor->scratch_saved = 1u;
        editor->history_index = (int32_t)g_ush_history_len - 1;
    } else if (editor->history_index > 0) {
        editor->history_index--;
    } else {
        return;
    }
    (void)ush_editor_history_load(editor, editor->history_index);
}

static void ush_editor_history_down(struct ush_editor *editor) {
    if (editor->history_index < 0) {
        return;
    }
    if ((uint32_t)(editor->history_index + 1) < g_ush_history_len) {
        editor->history_index++;
        (void)ush_editor_history_load(editor, editor->history_index);
        return;
    }
    editor->history_index = -1;
    if (editor->scratch_saved) {
        ush_editor_set_line(editor, editor->scratch);
    } else {
        ush_editor_set_line(editor, "");
    }
}

static uint32_t common_prefix_len_local(const char *a, const char *b) {
    uint32_t i = 0;

    while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
        i++;
    }
    return i;
}

static int ush_editor_replace_fragment(struct ush_editor *editor,
                                       uint32_t start,
                                       uint32_t end,
                                       const char *replacement,
                                       int append_space) {
    char updated[USH_LINE_MAX + 1];
    uint32_t prefix_len = start;
    uint32_t replacement_len = str_len_local(replacement);
    uint32_t suffix_len = editor->len > end ? editor->len - end : 0;
    uint32_t pos = 0;
    uint32_t i;

    if (prefix_len + replacement_len + suffix_len + (append_space ? 1u : 0u) > USH_LINE_MAX) {
        return 0;
    }
    for (i = 0; i < prefix_len; i++) {
        updated[pos++] = editor->line[i];
    }
    for (i = 0; i < replacement_len; i++) {
        updated[pos++] = replacement[i];
    }
    if (append_space) {
        updated[pos++] = ' ';
    }
    for (i = 0; i < suffix_len; i++) {
        updated[pos++] = editor->line[end + i];
    }
    updated[pos] = '\0';
    ush_editor_set_line(editor, updated);
    editor->cursor = prefix_len + replacement_len + (append_space ? 1u : 0u);
    return 1;
}

static int ush_complete_command_local(struct ush_editor *editor, uint32_t start, uint32_t end) {
    static const char *const shell_builtins[] = {"cd", "exit", "exec", "set", "export", "alias", "functions", "history", "source"};
    char prefix[USH_LINE_MAX + 1];
    char best[USH_LINE_MAX + 1];
    char name_buf[USH_LINE_MAX + 1];
    struct syscall_dirent entry;
    int match_count = 0;
    uint32_t i;
    int fd;

    if (end <= start || end - start > USH_LINE_MAX) {
        return 0;
    }
    for (i = 0; i < end - start; i++) {
        prefix[i] = editor->line[start + i];
    }
    prefix[end - start] = '\0';

    for (i = 0; i < sizeof(shell_builtins) / sizeof(shell_builtins[0]); i++) {
        if (!starts_with_ignore_case_local(shell_builtins[i], prefix)) {
            continue;
        }
        if (match_count == 0) {
            copy_line_local(best, shell_builtins[i], sizeof(best));
        } else {
            best[common_prefix_len_local(best, shell_builtins[i])] = '\0';
        }
        match_count++;
    }

    fd = opendir("/CMD");
    if (fd >= 0) {
        while (readdir((uint32_t)fd, &entry) > 0) {
            lowercase_copy_local(name_buf, sizeof(name_buf), entry.name);
            if (!starts_with_ignore_case_local(name_buf, prefix)) {
                continue;
            }
            if (match_count == 0) {
                copy_line_local(best, name_buf, sizeof(best));
            } else {
                best[common_prefix_len_local(best, name_buf)] = '\0';
            }
            match_count++;
        }
        close((uint32_t)fd);
    }

    if (match_count == 0 || best[0] == '\0') {
        return 0;
    }
    return ush_editor_replace_fragment(editor, start, end, best, match_count == 1);
}

static int ush_complete_path_local(struct ush_editor *editor, uint32_t start, uint32_t end) {
    char fragment[USH_LINE_MAX + 1];
    char dir_path[USH_LINE_MAX + 1];
    char name_prefix[USH_LINE_MAX + 1];
    char candidate[USH_LINE_MAX + 1];
    char best_name[USH_LINE_MAX + 1];
    struct syscall_dirent entry;
    uint32_t i;
    uint32_t slash_index = 0xffffffffu;
    int match_count = 0;
    int unique_is_dir = 0;
    int fd;

    if (end < start || end - start > USH_LINE_MAX) {
        return 0;
    }
    for (i = 0; i < end - start; i++) {
        fragment[i] = editor->line[start + i];
        if (fragment[i] == '/') {
            slash_index = i;
        }
    }
    fragment[end - start] = '\0';
    if (slash_index == 0xffffffffu) {
        copy_line_local(dir_path, ".", sizeof(dir_path));
        copy_line_local(name_prefix, fragment, sizeof(name_prefix));
    } else {
        for (i = 0; i < slash_index; i++) {
            dir_path[i] = fragment[i];
        }
        if (slash_index == 0u) {
            dir_path[0] = '/';
            dir_path[1] = '\0';
        } else {
            dir_path[slash_index] = '\0';
        }
        copy_line_local(name_prefix, fragment + slash_index + 1u, sizeof(name_prefix));
    }

    fd = opendir(dir_path);
    if (fd < 0) {
        return 0;
    }
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (!starts_with_ignore_case_local(entry.name, name_prefix)) {
            continue;
        }
        if (match_count == 0) {
            copy_line_local(best_name, entry.name, sizeof(best_name));
            unique_is_dir = (entry.attributes & 0x10u) != 0;
        } else {
            best_name[common_prefix_len_local(best_name, entry.name)] = '\0';
            unique_is_dir = 0;
        }
        match_count++;
    }
    close((uint32_t)fd);

    if (match_count == 0 || best_name[0] == '\0') {
        return 0;
    }

    candidate[0] = '\0';
    if (slash_index != 0xffffffffu) {
        if (slash_index == 0u) {
            copy_line_local(candidate, "/", sizeof(candidate));
        } else {
            copy_line_local(candidate, fragment, slash_index + 1u);
        }
        copy_line_local(candidate + str_len_local(candidate), best_name, sizeof(candidate) - str_len_local(candidate));
    } else {
        copy_line_local(candidate, best_name, sizeof(candidate));
    }
    if (match_count == 1 && unique_is_dir) {
        uint32_t len = str_len_local(candidate);

        if (len + 1u < sizeof(candidate)) {
            candidate[len] = '/';
            candidate[len + 1u] = '\0';
        }
        return ush_editor_replace_fragment(editor, start, end, candidate, 0);
    }
    return ush_editor_replace_fragment(editor, start, end, candidate, match_count == 1);
}

static void ush_editor_complete(struct ush_editor *editor) {
    uint32_t start = editor->cursor;
    int first_token = 1;
    uint32_t i;

    while (start > 0u && !is_space_local(editor->line[start - 1u])) {
        start--;
    }
    for (i = 0; i < start; i++) {
        if (!is_space_local(editor->line[i])) {
            first_token = 0;
            break;
        }
    }
    if (first_token && start == editor->cursor) {
        return;
    }
    for (i = start; i < editor->cursor; i++) {
        if (editor->line[i] == '/') {
            (void)ush_complete_path_local(editor, start, editor->cursor);
            return;
        }
    }
    if (first_token) {
        (void)ush_complete_command_local(editor, start, editor->cursor);
    } else {
        (void)ush_complete_path_local(editor, start, editor->cursor);
    }
}

void ush_write_error(const char *text) {
    ush_write_colored_err(ush_ansi_error, text);
}

void ush_write_prompt(void) {
    char prompt[160];

    ush_format_prompt(prompt, sizeof(prompt));
    write_str(prompt);
}

void ush_prompt_sync(const char *cwd) {
    copy_line_local(g_ush_prompt_path, cwd != 0 ? cwd : "/", sizeof(g_ush_prompt_path));
}

void ush_prompt_override(const char *prompt) {
    g_ush_prompt_override = prompt;
}

int read_line_chars(struct ush_editor *editor, char *line, uint32_t max_len) {
    char ch = 0;
    char esc = 0;
    char bracket = 0;
    char tail = 0;

    if (max_len == 0) {
        return 0;
    }
    editor->len = 0;
    editor->cursor = 0;
    editor->rendered_len = 0;
    editor->line[0] = '\0';
    editor->history_index = -1;
    editor->scratch_saved = 0;
    for (;;) {
        if (!ush_read_char_nonblock(&ch)) {
            yield();
            continue;
        }
        if (ch == 0x03) {
            editor->line[0] = '\0';
            editor->len = 0;
            editor->cursor = 0;
            editor->rendered_len = 0;
            write_str("^C\n");
            line[0] = '\0';
            return 1;
        }
        if (ch == 0x01) {
            ush_editor_move_home(editor);
            ush_editor_render(editor);
            ush_editor_sync_rendered_len(editor);
            continue;
        }
        if (ch == 0x05) {
            ush_editor_move_end(editor);
            ush_editor_render(editor);
            ush_editor_sync_rendered_len(editor);
            continue;
        }
        if (ch == 0x0c) {
            clear();
            ush_editor_render(editor);
            ush_editor_sync_rendered_len(editor);
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            copy_line_local(line, editor->line, max_len);
            ush_editor_history_store(editor, line);
            editor->history_index = -1;
            editor->scratch_saved = 0;
            write_str("\n");
            return 1;
        }
        if (ch == '\b' || ch == 0x7f) {
            ush_editor_backspace(editor);
            ush_editor_render(editor);
            ush_editor_sync_rendered_len(editor);
            continue;
        }
        if (ch == '\t') {
            ush_editor_complete(editor);
            ush_editor_render(editor);
            ush_editor_sync_rendered_len(editor);
            continue;
        }
        if (ch == '\x1b') {
            if (!ush_read_escape_char(&esc)) {
                continue;
            }
            if (esc == 'O') {
                if (!ush_read_escape_char(&bracket)) {
                    continue;
                }
                switch (bracket) {
                    case 'H':
                        ush_editor_move_home(editor);
                        break;
                    case 'F':
                        ush_editor_move_end(editor);
                        break;
                    default:
                        break;
                }
                ush_editor_render(editor);
                ush_editor_sync_rendered_len(editor);
                continue;
            }
            if (esc != '[') {
                continue;
            }
            if (!ush_read_escape_char(&bracket)) {
                continue;
            }
            if (bracket >= '0' && bracket <= '9') {
                if (!ush_read_escape_char(&tail)) {
                    continue;
                }
                if (tail == '~') {
                    switch (bracket) {
                        case '1':
                        case '7':
                            ush_editor_move_home(editor);
                            break;
                        case '3':
                            ush_editor_delete(editor);
                            break;
                        case '4':
                        case '8':
                            ush_editor_move_end(editor);
                            break;
                        default:
                            break;
                    }
                }
                ush_editor_render(editor);
                ush_editor_sync_rendered_len(editor);
                continue;
            }
            switch (bracket) {
                case 'A':
                    ush_editor_history_up(editor);
                    break;
                case 'B':
                    ush_editor_history_down(editor);
                    break;
                case 'C':
                    if (editor->cursor < editor->len) {
                        editor->cursor++;
                    }
                    break;
                case 'D':
                    if (editor->cursor != 0) {
                        editor->cursor--;
                    }
                    break;
                case 'H':
                    ush_editor_move_home(editor);
                    break;
                case 'F':
                    ush_editor_move_end(editor);
                    break;
                default:
                    break;
            }
            ush_editor_render(editor);
            ush_editor_sync_rendered_len(editor);
            continue;
        }
        if (ch >= ' ' && ch <= '~' && editor->len + 1u < max_len) {
            ush_editor_insert_char(editor, ch);
            ush_editor_render(editor);
            ush_editor_sync_rendered_len(editor);
        }
    }
}

void ush_history_list(void) {
    uint32_t i;

    for (i = 0; i < g_ush_history_len; i++) {
        uint32_t slot = (g_ush_history_next + USH_HISTORY_MAX - g_ush_history_len + i) % USH_HISTORY_MAX;

        fdprintf(STDOUT_FILENO, "%u  %s\n", i + 1u, g_ush_history[slot]);
    }
    if (g_ush_history_len == 0) {
        write_str("<empty>\n");
    }
}
