#include "user/apps/elf/ush_shared.h"

static char g_ush_prompt_path[64] = "/";
static const char *g_ush_prompt_override = NULL;
static const char ush_ansi_reset[] = "\x1b[0m";
static const char ush_ansi_error[] = "\x1b[1;31m";
enum { USH_TAB_WIDTH = 8u };
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

static void ush_idle_wait(uint32_t *polls_without_input) {
    if (polls_without_input == NULL) {
        yield();
        return;
    }
    if (*polls_without_input < 64u) {
        (*polls_without_input)++;
        return;
    }
    *polls_without_input = 0u;
    yield();
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

static void ush_ansi_clear_screen(void) {
    write_str("\x1b[2J\x1b[H");
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

static int ush_utf8_is_continuation(unsigned char ch) {
    return (ch & 0xc0u) == 0x80u;
}

static uint32_t ush_utf8_prev_offset(const char *text, uint32_t cursor) {
    uint32_t pos;

    if (cursor == 0u) {
        return 0u;
    }
    pos = cursor - 1u;
    while (pos > 0u && ush_utf8_is_continuation((unsigned char)text[pos])) {
        pos--;
    }
    return pos;
}

static uint32_t ush_utf8_next_offset(const char *text, uint32_t len, uint32_t cursor) {
    uint32_t pos;

    if (cursor >= len) {
        return len;
    }
    pos = cursor + 1u;
    while (pos < len && ush_utf8_is_continuation((unsigned char)text[pos])) {
        pos++;
    }
    return pos;
}

static uint8_t ush_utf8_decode_next(const char *text, uint32_t len, uint32_t offset, uint32_t *codepoint) {
    unsigned char first;
    uint32_t needed;
    uint32_t value;
    uint32_t minimum;

    if (text == NULL || codepoint == NULL || offset >= len) {
        return 0u;
    }
    first = (unsigned char)text[offset];
    if (first < 0x80u) {
        *codepoint = first;
        return 1u;
    }
    if ((first & 0xe0u) == 0xc0u) {
        needed = 2u;
        value = first & 0x1fu;
        minimum = 0x80u;
    } else if ((first & 0xf0u) == 0xe0u) {
        needed = 3u;
        value = first & 0x0fu;
        minimum = 0x800u;
    } else if ((first & 0xf8u) == 0xf0u) {
        needed = 4u;
        value = first & 0x07u;
        minimum = 0x10000u;
    } else {
        *codepoint = 0xfffdu;
        return 1u;
    }
    if (offset + needed > len) {
        *codepoint = 0xfffdu;
        return 1u;
    }
    for (uint32_t i = 1u; i < needed; i++) {
        unsigned char next = (unsigned char)text[offset + i];

        if (!ush_utf8_is_continuation(next)) {
            *codepoint = 0xfffdu;
            return 1u;
        }
        value = (value << 6) | (uint32_t)(next & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) {
        *codepoint = 0xfffdu;
        return 1u;
    }
    *codepoint = value;
    return (uint8_t)needed;
}

static uint8_t ush_codepoint_width(uint32_t codepoint) {
    if ((codepoint >= 0x0300u && codepoint <= 0x036fu) ||
        (codepoint >= 0x1ab0u && codepoint <= 0x1affu) ||
        (codepoint >= 0x1dc0u && codepoint <= 0x1dffu) ||
        (codepoint >= 0x20d0u && codepoint <= 0x20ffu) ||
        (codepoint >= 0xfe20u && codepoint <= 0xfe2fu)) {
        return 0u;
    }
    if ((codepoint >= 0x1100u && codepoint <= 0x115fu) ||
        (codepoint >= 0x2329u && codepoint <= 0x232au) ||
        (codepoint >= 0x2e80u && codepoint <= 0xa4cfu) ||
        (codepoint >= 0xac00u && codepoint <= 0xd7a3u) ||
        (codepoint >= 0xf900u && codepoint <= 0xfaffu) ||
        (codepoint >= 0xfe10u && codepoint <= 0xfe19u) ||
        (codepoint >= 0xfe30u && codepoint <= 0xfe6fu) ||
        (codepoint >= 0xff00u && codepoint <= 0xff60u) ||
        (codepoint >= 0xffe0u && codepoint <= 0xffe6u) ||
        (codepoint >= 0x20000u && codepoint <= 0x3fffdu)) {
        return 2u;
    }
    return 1u;
}

static uint32_t ush_utf8_columns_until(const char *text, uint32_t limit) {
    uint32_t cols = 0;

    for (uint32_t i = 0; i < limit && text[i] != '\0';) {
        uint32_t codepoint;
        uint8_t consumed = ush_utf8_decode_next(text, limit, i, &codepoint);

        if (consumed == 0u || i + consumed > limit) {
            consumed = 1u;
            codepoint = (unsigned char)text[i];
        }
        if (codepoint == '\t') {
            cols += USH_TAB_WIDTH - (cols % USH_TAB_WIDTH);
        } else {
            cols += ush_codepoint_width(codepoint);
        }
        i += consumed;
    }
    return cols;
}

static void ush_editor_render(const struct ush_editor *editor) {
    uint32_t cursor_column = ush_prompt_display_width() + ush_utf8_columns_until(editor->line, editor->cursor) + 1u;

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

static void ush_editor_insert_quoted_char(struct ush_editor *editor, char ch) {
    unsigned char uch = (unsigned char)ch;

    if (ch == '\t') {
        ush_editor_insert_char(editor, '\t');
        return;
    }
    if (ch == '\r' || ch == '\n') {
        if (editor->len + 2u > USH_LINE_MAX) {
            return;
        }
        ush_editor_insert_char(editor, '^');
        ush_editor_insert_char(editor, 'M');
        return;
    }
    if (uch >= ' ') {
        ush_editor_insert_char(editor, ch);
        return;
    }
    if (editor->len + 2u > USH_LINE_MAX) {
        return;
    }
    ush_editor_insert_char(editor, '^');
    ush_editor_insert_char(editor, (char)(uch + '@'));
}

static void ush_editor_backspace(struct ush_editor *editor) {
    uint32_t i;
    uint32_t start;
    uint32_t removed;

    if (editor->cursor == 0 || editor->len == 0) {
        return;
    }
    start = ush_utf8_prev_offset(editor->line, editor->cursor);
    removed = editor->cursor - start;
    for (i = start; i + removed <= editor->len; i++) {
        editor->line[i] = editor->line[i + removed];
    }
    editor->cursor = start;
    editor->len -= removed;
}

static void ush_editor_delete(struct ush_editor *editor) {
    uint32_t i;
    uint32_t end;
    uint32_t removed;

    if (editor->cursor >= editor->len) {
        return;
    }
    end = ush_utf8_next_offset(editor->line, editor->len, editor->cursor);
    removed = end - editor->cursor;
    for (i = editor->cursor; i + removed <= editor->len; i++) {
        editor->line[i] = editor->line[i + removed];
    }
    editor->len -= removed;
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

static int prefix_extended_local(const char *prefix, const char *best) {
    uint32_t prefix_len = str_len_local(prefix);

    return str_len_local(best) > prefix_len && common_prefix_len_local(prefix, best) == prefix_len;
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

static void ush_complete_print_candidate(const char *name, int is_dir) {
    write_str("  ");
    write_str(name);
    if (is_dir) {
        write_str("/");
    }
    write_str("\n");
}

static void ush_complete_print_command_matches(const char *prefix) {
    static const char *const shell_builtins[] = {"cd", "exit", "exec", "set", "export", "alias", "functions", "history", "source"};
    char name_buf[USH_LINE_MAX + 1];
    struct syscall_dirent entry;
    uint32_t i;
    int fd;

    write_str("\n");
    for (i = 0; i < sizeof(shell_builtins) / sizeof(shell_builtins[0]); i++) {
        if (starts_with_ignore_case_local(shell_builtins[i], prefix)) {
            ush_complete_print_candidate(shell_builtins[i], 0);
        }
    }
    fd = opendir("/CMD");
    if (fd >= 0) {
        while (readdir((uint32_t)fd, &entry) > 0) {
            lowercase_copy_local(name_buf, sizeof(name_buf), entry.name);
            if (starts_with_ignore_case_local(name_buf, prefix)) {
                ush_complete_print_candidate(name_buf, 0);
            }
        }
        close((uint32_t)fd);
    }
}

static void ush_complete_print_path_matches(const char *dir_path, const char *name_prefix) {
    struct syscall_dirent entry;
    int fd;

    fd = opendir(dir_path);
    if (fd < 0) {
        return;
    }
    write_str("\n");
    while (readdir((uint32_t)fd, &entry) > 0) {
        if (starts_with_ignore_case_local(entry.name, name_prefix)) {
            ush_complete_print_candidate(entry.name, (entry.attributes & 0x10u) != 0);
        }
    }
    close((uint32_t)fd);
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

    if (end < start || end - start > USH_LINE_MAX) {
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
    if (match_count == 1 || prefix_extended_local(prefix, best)) {
        return ush_editor_replace_fragment(editor, start, end, best, match_count == 1);
    }
    ush_complete_print_command_matches(prefix);
    return 1;
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
    if (match_count == 1 || prefix_extended_local(fragment, candidate)) {
        return ush_editor_replace_fragment(editor, start, end, candidate, match_count == 1);
    }
    ush_complete_print_path_matches(dir_path, name_prefix);
    return 1;
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
    if (editor == NULL || line == NULL || max_len == 0) {
        return 0;
    }
    ush_editor_set_line(editor, "");
    ush_editor_sync_rendered_len(editor);
    uint32_t idle_polls = 0u;
    int quoted_insert = 0;
    for (;;) {
        char ch;

        if (!ush_read_char_nonblock(&ch)) {
            ush_idle_wait(&idle_polls);
            continue;
        }
        idle_polls = 0u;
        if (quoted_insert) {
            quoted_insert = 0;
            ush_editor_insert_quoted_char(editor, ch);
            ush_editor_render(editor);
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            uint32_t len = editor->len;

            if (len >= max_len) {
                len = max_len - 1u;
            }
            for (uint32_t i = 0; i < len; i++) {
                line[i] = editor->line[i];
            }
            line[len] = '\0';
            trim_line(line);
            ush_editor_set_line(editor, line);
            ush_editor_history_store(editor, line);
            editor->history_index = -1;
            editor->scratch_saved = 0;
            write_str("\n");
            return 1;
        }
        if (ch == '\b' || ch == 0x7f) {
            ush_editor_backspace(editor);
            ush_editor_render(editor);
            continue;
        }
        if (ch == '\t') {
            ush_editor_complete(editor);
            ush_editor_render(editor);
            continue;
        }
        if (ch == 0x16) {
            quoted_insert = 1;
            continue;
        }
        if (ch == 0x0c) {
            ush_ansi_clear_screen();
            ush_editor_render(editor);
            continue;
        }
        if (ch == 0x01) {
            ush_editor_move_home(editor);
            ush_editor_render(editor);
            continue;
        }
        if (ch == 0x05) {
            ush_editor_move_end(editor);
            ush_editor_render(editor);
            continue;
        }
        if (ch == '\x1b') {
            char seq1;
            char seq2;

            if (!ush_read_escape_char(&seq1)) {
                continue;
            }
            if (seq1 != '[' || !ush_read_escape_char(&seq2)) {
                continue;
            }
            if (seq2 == 'A') {
                ush_editor_history_up(editor);
            } else if (seq2 == 'B') {
                ush_editor_history_down(editor);
            } else if (seq2 == 'C') {
                editor->cursor = ush_utf8_next_offset(editor->line, editor->len, editor->cursor);
            } else if (seq2 == 'D') {
                editor->cursor = ush_utf8_prev_offset(editor->line, editor->cursor);
            } else if (seq2 == 'H') {
                ush_editor_move_home(editor);
            } else if (seq2 == 'F') {
                ush_editor_move_end(editor);
            } else if (seq2 == '3') {
                char tilde;

                if (ush_read_escape_char(&tilde) && tilde == '~') {
                    ush_editor_delete(editor);
                }
            }
            ush_editor_render(editor);
            continue;
        }
        if ((unsigned char)ch >= ' ') {
            ush_editor_insert_char(editor, ch);
            ush_editor_render(editor);
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
