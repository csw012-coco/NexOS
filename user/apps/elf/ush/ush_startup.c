#include "user/apps/elf/ush/ush_shared.h"

static int streq_startup_local(const char *a, const char *b) {
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

static uint32_t str_len_startup_local(const char *text) {
    uint32_t len = 0;

    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static void copy_line_startup_local(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (dst == 0 || max_len == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int is_space_startup_local(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void trim_startup_local(char *text) {
    uint32_t start = 0;
    uint32_t end;
    uint32_t i = 0;

    if (text == NULL) {
        return;
    }
    end = str_len_startup_local(text);
    while (text[start] != '\0' && is_space_startup_local(text[start])) {
        start++;
    }
    while (end > start && is_space_startup_local(text[end - 1u])) {
        end--;
    }
    while (start < end) {
        text[i++] = text[start++];
    }
    text[i] = '\0';
}

static char ascii_tolower_startup_local(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static const char *basename_startup_local(const char *path) {
    const char *base = path;
    uint32_t i = 0;

    if (path == 0) {
        return "";
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            base = path + i + 1u;
        }
        i++;
    }
    return base;
}

static void copy_lowercase_startup_local(char *dst, uint32_t dst_size, const char *src) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = ascii_tolower_startup_local(src[i]);
        i++;
    }
    dst[i] = '\0';
}

int ush_build_invoked_command_line(int argc, char **argv, char *out, uint32_t out_size) {
    char program[32];
    const char *base;
    uint32_t i;

    if (argc <= 0 || argv == 0 || out == 0 || out_size == 0) {
        return 0;
    }
    base = basename_startup_local(argv[0]);
    copy_lowercase_startup_local(program, sizeof(program), base);
    if (program[0] == '\0' ||
        streq_startup_local(program, "ush") ||
        streq_startup_local(program, "ush.elf") ||
        streq_startup_local(program, "ush32") ||
        streq_startup_local(program, "ush32.elf")) {
        return 0;
    }
    copy_line_startup_local(out, program, out_size);
    for (i = 1u; i < (uint32_t)argc; i++) {
        uint32_t arg_len;
        uint32_t out_len = str_len_startup_local(out);
        uint32_t j;

        if (argv[i] == 0 || argv[i][0] == '\0') {
            continue;
        }
        arg_len = str_len_startup_local(argv[i]);
        if (out_len + arg_len + 2u >= out_size) {
            write_err_str("ush: command line too long\n");
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

static int ush_config_parse_bool_local(const char *value, int *out) {
    if (value == 0 || out == 0) {
        return 0;
    }
    if (streq_startup_local(value, "1") || streq_startup_local(value, "true") ||
        streq_startup_local(value, "yes") || streq_startup_local(value, "on")) {
        *out = 1;
        return 1;
    }
    if (streq_startup_local(value, "0") || streq_startup_local(value, "false") ||
        streq_startup_local(value, "no") || streq_startup_local(value, "off")) {
        *out = 0;
        return 1;
    }
    return 0;
}

static void ush_config_apply_pair_local(char *key, char *value) {
    int enabled;

    trim_startup_local(key);
    trim_startup_local(value);
    if (key[0] == '\0' || value[0] == '\0') {
        return;
    }
    if (streq_startup_local(key, "function_recursion_limit") ||
        streq_startup_local(key, "ush_function_recursion_limit")) {
        if (ush_config_parse_bool_local(value, &enabled)) {
            ush_function_recursion_limit_set(enabled);
        }
    }
}

static void ush_config_apply_line_local(char *line) {
    uint32_t i;

    trim_startup_local(line);
    if (line[0] == '\0' || line[0] == '#') {
        return;
    }
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '#') {
            line[i] = '\0';
            break;
        }
    }
    trim_startup_local(line);
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '=') {
            line[i] = '\0';
            ush_config_apply_pair_local(line, line + i + 1u);
            return;
        }
    }
}

void ush_load_config_local(void) {
    char line[USH_LINE_MAX + 1];
    struct syscall_identity_info identity;
    int fd;

    if (identity_get(&identity) >= 0 && identity.uid != 0u) {
        return;
    }
    fd = open("/system/config/nex.scf", 0);
    if (fd < 0) {
        fd = open("system/config/nex.scf", 0);
    }
    if (fd < 0) {
        fd = open("/NEX.SCF", 0);
    }
    if (fd < 0) {
        fd = open("NEX.SCF", 0);
    }
    if (fd < 0) {
        fd = open("/NEXOS.SCF", 0);
    }
    if (fd < 0) {
        fd = open("NEXOS.SCF", 0);
    }
    if (fd < 0) {
        return;
    }
    while (read_line((uint32_t)fd, line, sizeof(line)) != 0u) {
        ush_config_apply_line_local(line);
    }
    close((uint32_t)fd);
}

int ush_is_direct_shell_invocation(const char *path) {
    const char *base = basename_startup_local(path);

    return streq_startup_local(base, "ush") ||
           streq_startup_local(base, "USH") ||
           streq_startup_local(base, "USH.ELF") ||
           streq_startup_local(base, "ush.elf") ||
           streq_startup_local(base, "USH32") ||
           streq_startup_local(base, "ush32") ||
           streq_startup_local(base, "USH32.ELF") ||
           streq_startup_local(base, "ush32.elf");
}

int ush_bind_interactive_stdio_path(const char *path) {
    int tty_fd = open(path != NULL ? path : "/dev/tty", O_RDWR);

    if (tty_fd < 0) {
        return 0;
    }
    if (tty_fd != STDIN_FILENO) {
        (void)dup2(tty_fd, STDIN_FILENO);
    }
    if (tty_fd != STDOUT_FILENO) {
        (void)dup2(tty_fd, STDOUT_FILENO);
    }
    if (tty_fd != STDERR_FILENO) {
        (void)dup2(tty_fd, STDERR_FILENO);
    }
    if (tty_fd > STDERR_FILENO) {
        close((uint32_t)tty_fd);
    }
    return 1;
}

void ush_bind_interactive_stdio(void) {
    (void)ush_bind_interactive_stdio_path("/dev/tty");
}
