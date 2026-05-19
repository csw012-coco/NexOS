#include "user/apps/elf/nexbox/applets/text/cmdsuite_text_common.h"

static int table_split_assignment_local(const char *text,
                                        char *name,
                                        uint32_t name_size,
                                        char *value,
                                        uint32_t value_size) {
    uint32_t pos = 0;
    uint32_t value_pos = 0;

    if (text == NULL || name == NULL || value == NULL || name_size == 0 || value_size == 0) {
        return 0;
    }
    while (text[pos] != '\0' && text[pos] != '=') {
        if (pos + 1u >= name_size) {
            return 0;
        }
        name[pos] = text[pos];
        pos++;
    }
    if (pos == 0 || text[pos] != '=') {
        return 0;
    }
    name[pos] = '\0';
    pos++;
    while (text[pos] != '\0') {
        if (value_pos + 1u >= value_size) {
            return 0;
        }
        value[value_pos++] = text[pos++];
    }
    value[value_pos] = '\0';
    return value_pos != 0;
}

static uint32_t table_token_at_local(const char *line,
                                     uint32_t wanted,
                                     char *out,
                                     uint32_t out_size) {
    uint32_t pos = 0;
    uint32_t index = 0;

    if (line == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    while (line[pos] != '\0') {
        uint32_t out_pos = 0;

        while (line[pos] == ' ' || line[pos] == '\t') {
            pos++;
        }
        if (line[pos] == '\0') {
            break;
        }
        if (line[pos] == '"') {
            pos++;
            while (line[pos] != '\0') {
                char ch = line[pos++];

                if (ch == '\\' && line[pos] != '\0') {
                    ch = line[pos++];
                } else if (ch == '"') {
                    break;
                }
                if (index == wanted && out_pos + 1u < out_size) {
                    out[out_pos++] = ch;
                }
            }
            while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t') {
                pos++;
            }
        } else {
            while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t') {
                if (index == wanted && out_pos + 1u < out_size) {
                    out[out_pos++] = line[pos];
                }
                pos++;
            }
        }
        if (index == wanted) {
            out[out_pos] = '\0';
            return out_pos;
        }
        index++;
    }
    return 0;
}

static int table_column_index_local(const char *header, const char *name, uint32_t *index_out) {
    char token[64];
    uint32_t index = 0;

    if (header == NULL || name == NULL || index_out == NULL) {
        return 0;
    }
    while (table_token_at_local(header, index, token, sizeof(token)) != 0) {
        if (streq_local(token, name)) {
            *index_out = index;
            return 1;
        }
        index++;
    }
    return 0;
}

static void event_table_write_token_local(const char *text) {
    uint32_t i = 0;
    int quote = 0;

    if (text == NULL || text[0] == '\0') {
        write_str("-");
        return;
    }
    while (text[i] != '\0') {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '"' || text[i] == '\\') {
            quote = 1;
            break;
        }
        i++;
    }
    if (quote) {
        write_str("\"");
    }
    i = 0;
    while (text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            write_str("\\");
        }
        write_stdout(&text[i], 1u);
        i++;
    }
    if (quote) {
        write_str("\"");
    }
}

static int event_table_field_value_local(const char *line, const char *key, char *out, uint32_t out_size) {
    uint32_t pos = 0;
    uint32_t key_len = str_len_local(key);

    if (line == NULL || key == NULL || out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    while (line[pos] != '\0') {
        while (line[pos] == ' ' || line[pos] == '\t') {
            pos++;
        }
        if (line[pos] == '\0') {
            break;
        }
        if ((pos == 0u || line[pos - 1u] == ' ' || line[pos - 1u] == '\t') &&
            strncmp(line + pos, key, key_len) == 0 && line[pos + key_len] == '=') {
            uint32_t out_pos = 0;

            pos += key_len + 1u;
            while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t' && out_pos + 1u < out_size) {
                out[out_pos++] = line[pos++];
            }
            out[out_pos] = '\0';
            return out_pos != 0u;
        }
        while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t') {
            pos++;
        }
    }
    return 0;
}

static void event_table_write_field_local(const char *line, const char *key) {
    char value[64];

    if (event_table_field_value_local(line, key, value, sizeof(value))) {
        event_table_write_token_local(value);
    } else {
        write_str("-");
    }
}

static int event_table_type_local(const char *line, char *out, uint32_t out_size) {
    const char *cursor = line;

    if (line == NULL || out == NULL || out_size == 0u || !starts_with(line, "event ")) {
        return 0;
    }
    cursor += 6;
    return table_token_at_local(cursor, 0, out, out_size) != 0u;
}

static int cmd_as_event_table_local(void) {
    char line[256];
    char type[40];

    write_str("# nex/type: table\n");
    write_str("# nex/columns: type seq tick state key code shift ctrl caps op path bytes id dx dy buttons disk part dev speed source\n");
    write_str("type seq tick state key code shift ctrl caps op path bytes id dx dy buttons disk part dev speed source\n");
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        if (!event_table_type_local(line, type, sizeof(type))) {
            continue;
        }
        event_table_write_token_local(type);
        write_str(" ");
        event_table_write_field_local(line, "seq");
        write_str(" ");
        event_table_write_field_local(line, "tick");
        write_str(" ");
        event_table_write_field_local(line, "state");
        write_str(" ");
        event_table_write_field_local(line, "key");
        write_str(" ");
        event_table_write_field_local(line, "code");
        write_str(" ");
        event_table_write_field_local(line, "shift");
        write_str(" ");
        event_table_write_field_local(line, "ctrl");
        write_str(" ");
        event_table_write_field_local(line, "caps");
        write_str(" ");
        event_table_write_field_local(line, "op");
        write_str(" ");
        event_table_write_field_local(line, "path");
        write_str(" ");
        event_table_write_field_local(line, "bytes");
        write_str(" ");
        event_table_write_field_local(line, "id");
        write_str(" ");
        event_table_write_field_local(line, "dx");
        write_str(" ");
        event_table_write_field_local(line, "dy");
        write_str(" ");
        event_table_write_field_local(line, "buttons");
        write_str(" ");
        event_table_write_field_local(line, "disk");
        write_str(" ");
        event_table_write_field_local(line, "part");
        write_str(" ");
        event_table_write_field_local(line, "dev");
        write_str(" ");
        event_table_write_field_local(line, "speed");
        write_str(" ");
        event_table_write_field_local(line, "source");
        write_str("\n");
    }
    return 0;
}

int cmd_as(int argc, char **argv) {
    char line[256];
    uint32_t got;

    if (argc != 2 || (!streq_local(argv[1], "table") && !streq_local(argv[1], "event"))) {
        write_err_usage("as", " <table|event>\n");
        return 1;
    }
    if (streq_local(argv[1], "event")) {
        return cmd_as_event_table_local();
    }
    got = read_line(STDIN_FILENO, line, sizeof(line));
    if (got == 0u) {
        return 0;
    }
    write_str("# nex/type: table\n");
    write_str("# nex/columns: ");
    write_str(line);
    write_str("\n");
    write_str(line);
    write_str("\n");
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        write_str(line);
        write_str("\n");
    }
    return 0;
}

int cmd_pick(int argc, char **argv) {
    char name[32];
    char value[64];
    char header[256];
    char line[256];
    char token[64];
    uint32_t column = 0;
    int have_header = 0;

    if (argc != 2 || !table_split_assignment_local(argv[1], name, sizeof(name), value, sizeof(value))) {
        write_err_usage("pick", " <column=value>\n");
        return 1;
    }
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        if (starts_with(line, "# nex/type:") || starts_with(line, "# nex/columns:")) {
            write_str(line);
            write_str("\n");
            continue;
        }
        if (!have_header) {
            copy_line_local(header, line, sizeof(header));
            if (!table_column_index_local(header, name, &column)) {
                write_err_str("pick: column not found: ");
                write_err_str(name);
                write_err_str("\n");
                return 1;
            }
            write_str(header);
            write_str("\n");
            have_header = 1;
            continue;
        }
        if (table_token_at_local(line, column, token, sizeof(token)) != 0 && streq_local(token, value)) {
            write_str(line);
            write_str("\n");
        }
    }
    return have_header ? 0 : 1;
}

enum {
    TABLE_PIPE_ROW_MAX = 48u,
    TABLE_PIPE_LINE_MAX = 256u,
    TABLE_PIPE_COL_MAX = 12u
};

static char g_table_pipe_header[TABLE_PIPE_LINE_MAX];
static char g_table_pipe_rows[TABLE_PIPE_ROW_MAX][TABLE_PIPE_LINE_MAX];
static uint32_t g_table_pipe_row_count;

static int table_read_input_local(void) {
    char line[TABLE_PIPE_LINE_MAX];
    int have_header = 0;

    g_table_pipe_header[0] = '\0';
    g_table_pipe_row_count = 0;
    while (read_line(STDIN_FILENO, line, sizeof(line)) != 0u) {
        if (starts_with(line, "# nex/type:") || starts_with(line, "# nex/columns:")) {
            continue;
        }
        if (!have_header) {
            copy_line_local(g_table_pipe_header, line, sizeof(g_table_pipe_header));
            have_header = 1;
            continue;
        }
        if (g_table_pipe_row_count >= TABLE_PIPE_ROW_MAX) {
            write_err_str("table: too many rows\n");
            return 0;
        }
        copy_line_local(g_table_pipe_rows[g_table_pipe_row_count],
                        line,
                        sizeof(g_table_pipe_rows[g_table_pipe_row_count]));
        g_table_pipe_row_count++;
    }
    return have_header;
}

static void table_write_meta_local(const char *header) {
    write_str("# nex/type: table\n");
    write_str("# nex/columns: ");
    write_str(header);
    write_str("\n");
}

static void table_write_headered_local(const char *header) {
    table_write_meta_local(header);
    write_str(header);
    write_str("\n");
}

static int table_compare_text_local(const char *a, const char *b) {
    uint32_t i = 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] < b[i]) {
            return -1;
        }
        if (a[i] > b[i]) {
            return 1;
        }
        i++;
    }
    if (a[i] == b[i]) {
        return 0;
    }
    return a[i] == '\0' ? -1 : 1;
}

static void table_append_selected_token_local(char *line_out,
                                              uint32_t line_size,
                                              uint32_t *pos_io,
                                              const char *token) {
    uint32_t i = 0;
    int quote = 0;

    if (line_out == NULL || pos_io == NULL || token == NULL || line_size == 0) {
        return;
    }
    if (*pos_io != 0 && *pos_io + 1u < line_size) {
        line_out[(*pos_io)++] = ' ';
    }
    while (token[i] != '\0') {
        if (token[i] == ' ' || token[i] == '\t' || token[i] == '"' || token[i] == '\\') {
            quote = 1;
            break;
        }
        i++;
    }
    i = 0;
    if (quote && *pos_io + 1u < line_size) {
        line_out[(*pos_io)++] = '"';
    }
    while (token[i] != '\0' && *pos_io + 1u < line_size) {
        if ((token[i] == '"' || token[i] == '\\') && *pos_io + 2u < line_size) {
            line_out[(*pos_io)++] = '\\';
        }
        line_out[(*pos_io)++] = token[i++];
    }
    if (quote && *pos_io + 1u < line_size) {
        line_out[(*pos_io)++] = '"';
    }
    line_out[*pos_io] = '\0';
}

int cmd_select(int argc, char **argv) {
    uint32_t columns[TABLE_PIPE_COL_MAX];
    uint32_t selected = 0;
    char out_line[TABLE_PIPE_LINE_MAX];
    char token[64];
    uint32_t pos = 0;

    if (argc < 2 || argc > (int)(TABLE_PIPE_COL_MAX + 1u)) {
        write_err_usage("select", " <column> [column...]\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (!table_column_index_local(g_table_pipe_header, argv[i], &columns[selected])) {
            write_err_str("select: column not found: ");
            write_err_str(argv[i]);
            write_err_str("\n");
            return 1;
        }
        selected++;
    }
    out_line[0] = '\0';
    for (uint32_t i = 0; i < selected; i++) {
        (void)table_token_at_local(g_table_pipe_header, columns[i], token, sizeof(token));
        table_append_selected_token_local(out_line, sizeof(out_line), &pos, token);
    }
    table_write_headered_local(out_line);
    for (uint32_t r = 0; r < g_table_pipe_row_count; r++) {
        pos = 0;
        out_line[0] = '\0';
        for (uint32_t i = 0; i < selected; i++) {
            (void)table_token_at_local(g_table_pipe_rows[r], columns[i], token, sizeof(token));
            table_append_selected_token_local(out_line, sizeof(out_line), &pos, token);
        }
        write_str(out_line);
        write_str("\n");
    }
    return 0;
}

int cmd_sort_by(int argc, char **argv) {
    uint32_t column = 0;
    char left[64];
    char right[64];

    if (argc != 2) {
        write_err_usage("sort-by", " <column>\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    if (!table_column_index_local(g_table_pipe_header, argv[1], &column)) {
        write_err_str("sort-by: column not found: ");
        write_err_str(argv[1]);
        write_err_str("\n");
        return 1;
    }
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        for (uint32_t j = i + 1u; j < g_table_pipe_row_count; j++) {
            char tmp[TABLE_PIPE_LINE_MAX];

            (void)table_token_at_local(g_table_pipe_rows[i], column, left, sizeof(left));
            (void)table_token_at_local(g_table_pipe_rows[j], column, right, sizeof(right));
            if (table_compare_text_local(left, right) > 0) {
                copy_line_local(tmp, g_table_pipe_rows[i], sizeof(tmp));
                copy_line_local(g_table_pipe_rows[i], g_table_pipe_rows[j], sizeof(g_table_pipe_rows[i]));
                copy_line_local(g_table_pipe_rows[j], tmp, sizeof(g_table_pipe_rows[j]));
            }
        }
    }
    table_write_headered_local(g_table_pipe_header);
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        write_str(g_table_pipe_rows[i]);
        write_str("\n");
    }
    return 0;
}

int cmd_count_by(int argc, char **argv) {
    uint32_t column = 0;
    uint8_t counted[TABLE_PIPE_ROW_MAX];
    char value[64];
    char other[64];

    if (argc != 2) {
        write_err_usage("count-by", " <column>\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    if (!table_column_index_local(g_table_pipe_header, argv[1], &column)) {
        write_err_str("count-by: column not found: ");
        write_err_str(argv[1]);
        write_err_str("\n");
        return 1;
    }
    for (uint32_t i = 0; i < TABLE_PIPE_ROW_MAX; i++) {
        counted[i] = 0;
    }
    table_write_headered_local("value count");
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        uint32_t count = 0;
        char out_line[TABLE_PIPE_LINE_MAX];
        uint32_t pos = 0;

        if (counted[i]) {
            continue;
        }
        (void)table_token_at_local(g_table_pipe_rows[i], column, value, sizeof(value));
        for (uint32_t j = i; j < g_table_pipe_row_count; j++) {
            (void)table_token_at_local(g_table_pipe_rows[j], column, other, sizeof(other));
            if (streq_local(value, other)) {
                counted[j] = 1;
                count++;
            }
        }
        out_line[0] = '\0';
        table_append_selected_token_local(out_line, sizeof(out_line), &pos, value);
        write_str(out_line);
        write_str(" ");
        write_dec(count);
        write_str("\n");
    }
    return 0;
}

static void table_write_json_string_local(const char *text) {
    uint32_t i = 0;

    write_str("\"");
    while (text != NULL && text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\\') {
            write_stdout("\\", 1);
        }
        write_stdout(&text[i], 1);
        i++;
    }
    write_str("\"");
}

int cmd_to(int argc, char **argv) {
    char headers[TABLE_PIPE_COL_MAX][64];
    uint32_t header_count = 0;
    char token[64];

    if (argc != 2 || !streq_local(argv[1], "json")) {
        write_err_usage("to", " json\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    while (header_count < TABLE_PIPE_COL_MAX &&
           table_token_at_local(g_table_pipe_header, header_count, headers[header_count], sizeof(headers[header_count])) != 0) {
        header_count++;
    }
    write_str("[\n");
    for (uint32_t r = 0; r < g_table_pipe_row_count; r++) {
        write_str("  {");
        for (uint32_t c = 0; c < header_count; c++) {
            if (c != 0) {
                write_str(", ");
            }
            table_write_json_string_local(headers[c]);
            write_str(": ");
            (void)table_token_at_local(g_table_pipe_rows[r], c, token, sizeof(token));
            table_write_json_string_local(token);
        }
        write_str(r + 1u == g_table_pipe_row_count ? "}\n" : "},\n");
    }
    write_str("]\n");
    return 0;
}

int cmd_view(int argc, char **argv) {
    if (argc != 2 || !streq_local(argv[1], "table")) {
        write_err_usage("view", " table\n");
        return 1;
    }
    if (!table_read_input_local()) {
        return 1;
    }
    write_str(g_table_pipe_header);
    write_str("\n");
    for (uint32_t i = 0; i < g_table_pipe_row_count; i++) {
        write_str(g_table_pipe_rows[i]);
        write_str("\n");
    }
    return 0;
}
