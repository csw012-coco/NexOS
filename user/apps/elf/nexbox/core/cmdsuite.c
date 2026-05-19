#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

uint32_t str_len_local(const char *text) {
    uint32_t len = 0;

    while (text != NULL && text[len] != '\0') {
        len++;
    }
    return len;
}

void copy_line_local(char *dst, const char *src, uint32_t max_len) {
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

int streq_local(const char *a, const char *b) {
    uint32_t i = 0;

    if (a == NULL || b == NULL) {
        return a == b;
    }
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

int streq_ignore_case_local(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

static int cmd_name_has_path_local(const char *name) {
    uint32_t i = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '/' || name[0] == '.') {
        return 1;
    }
    while (name[i] != '\0') {
        if (name[i] == '/') {
            return 1;
        }
        i++;
    }
    return 0;
}

static char cmd_ascii_tolower_local(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int cmd_copy_program_name_local(char *dst, uint32_t dst_size, const char *name) {
    uint32_t dst_pos = 0;
    uint32_t src_pos = 0;

    if (dst == NULL || dst_size == 0 || name == NULL || name[0] == '\0') {
        return 0;
    }
    if (!cmd_name_has_path_local(name)) {
        const char prefix[] = "/cmd/";

        while (prefix[dst_pos] != '\0') {
            if (dst_pos + 1u >= dst_size) {
                return 0;
            }
            dst[dst_pos] = prefix[dst_pos];
            dst_pos++;
        }
        while (name[src_pos] != '\0') {
            if (dst_pos + 1u >= dst_size) {
                return 0;
            }
            dst[dst_pos++] = cmd_ascii_tolower_local(name[src_pos++]);
        }
        dst[dst_pos] = '\0';
        return 1;
    }
    copy_line_local(dst, name, dst_size);
    return dst[0] != '\0';
}

int parse_u32_local(const char *text, uint32_t *out) {
    char *end = 0;
    unsigned long value;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return 0;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

void write_dec(uint32_t value) {
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

void write_sdec(int32_t value) {
    uint32_t magnitude;

    if (value < 0) {
        write_stdout("-", 1);
        magnitude = (uint32_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t)value;
    }
    write_dec(magnitude);
}

void write_hex_u32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[8];
    uint32_t i;

    for (i = 0; i < sizeof(buf); i++) {
        buf[sizeof(buf) - 1u - i] = hex[value & 0x0fu];
        value >>= 4;
    }
    write_stdout(buf, sizeof(buf));
}

void write_hex_u64(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[16];
    uint32_t i;

    for (i = 0; i < sizeof(buf); i++) {
        buf[sizeof(buf) - 1u - i] = hex[value & 0x0fu];
        value >>= 4;
    }
    write_stdout(buf, sizeof(buf));
}

const char *process_exit_reason_local(int32_t exit_code) {
    switch (exit_code) {
        case -4:
            return "illegal instruction";
        case -8:
            return "floating point exception";
        case -11:
            return "segmentation fault";
        default:
            return NULL;
    }
}

void write_process_exit_status(int32_t exit_code) {
    const char *reason = process_exit_reason_local(exit_code);

    if (reason != NULL) {
        write_str(reason);
        write_str(" (");
        write_sdec(exit_code);
        write_str(")");
        return;
    }
    write_sdec(exit_code);
}

void write_text_padded(const char *text, uint32_t width) {
    uint32_t len = str_len_local(text);

    write_str(text);
    while (len < width) {
        write_stdout(" ", 1);
        len++;
    }
}

void write_human_size(uint64_t bytes) {
    static const char *const units[] = {"B", "K", "M", "G", "T"};
    uint64_t whole = bytes;
    uint64_t rem = 0;
    uint32_t unit = 0;

    while (whole >= 1024u && unit + 1u < (sizeof(units) / sizeof(units[0]))) {
        rem = whole % 1024u;
        whole /= 1024u;
        unit++;
    }
    write_dec((uint32_t)whole);
    if (unit != 0u && whole < 10u && rem != 0u) {
        char digit = (char)('0' + ((rem * 10u) / 1024u));

        write_stdout(".", 1);
        write_stdout(&digit, 1);
    }
    write_str(units[unit]);
}

void write_err_text(const char *text) {
    write_err_str(text);
}

void write_err_usage(const char *verb, const char *suffix) {
    write_err_str("usage: ");
    write_err_str(verb);
    write_err_str(suffix);
}

int cmd_open_resolved_path(const char *arg, uint32_t flags) {
    if (arg == NULL || arg[0] == '\0') {
        return -1;
    }
    return open(arg, flags);
}

int cmd_build_program_command(int argc,
                              char **argv,
                              int start,
                              const char *verb,
                              int resolve_dot_name,
                              char *out,
                              uint32_t out_size) {
    const char *name;
    uint32_t out_len;
    int i;

    if (out == NULL || out_size == 0) {
        return 0;
    }
    if (argc <= start || argv[start] == NULL || argv[start][0] == '\0') {
        write_err_usage(verb, " <name> [args]\n");
        out[0] = '\0';
        return 0;
    }

    name = argv[start];
    (void)resolve_dot_name;

    if (!cmd_copy_program_name_local(out, out_size, name)) {
        write_err_str(verb);
        write_err_str(": command line too long\n");
        out[0] = '\0';
        return 0;
    }
    out_len = str_len_local(out);
    for (i = start + 1; i < argc; i++) {
        uint32_t arg_len = str_len_local(argv[i]);
        uint32_t j;

        if (out_len + arg_len + 2u > out_size) {
            write_err_str(verb);
            write_err_str(": command line too long\n");
            out[0] = '\0';
            return 0;
        }
        out[out_len++] = ' ';
        for (j = 0; j < arg_len; j++) {
            out[out_len++] = argv[i][j];
        }
        out[out_len] = '\0';
    }
    return 1;
}
