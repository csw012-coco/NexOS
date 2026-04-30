#include "kernel/internal/core/kernel_config_internal.h"
#include "fs/vfs.h"
#include "lib/string.h"

enum {
    KERNEL_CONFIG_BUFFER_SIZE = 512u
};

static char g_kernel_config_buffer[KERNEL_CONFIG_BUFFER_SIZE + 1u];

static int config_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void config_trim(char *text) {
    uint32_t start = 0;
    uint32_t end;
    uint32_t i = 0;

    if (text == 0) {
        return;
    }
    end = str_len(text);
    while (text[start] != '\0' && config_is_space(text[start])) {
        start++;
    }
    while (end > start && config_is_space(text[end - 1u])) {
        end--;
    }
    while (start < end) {
        text[i++] = text[start++];
    }
    text[i] = '\0';
}

static void config_copy(char *dst, const char *src, uint32_t dst_size) {
    uint32_t i = 0;

    if (dst == 0 || dst_size == 0) {
        return;
    }
    while (src != 0 && src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int config_parse_bool(const char *value, uint8_t *out) {
    if (value == 0 || out == 0) {
        return 0;
    }
    if (streq(value, "1") || streq(value, "true") || streq(value, "yes") || streq(value, "on")) {
        *out = 1u;
        return 1;
    }
    if (streq(value, "0") || streq(value, "false") || streq(value, "no") || streq(value, "off")) {
        *out = 0u;
        return 1;
    }
    return 0;
}

static void config_set_init_path(struct kernel_config *config, const char *value) {
    if (config == 0 || value == 0) {
        return;
    }
    while (*value == '/') {
        value++;
    }
    if (*value == '\0') {
        return;
    }
    config_copy(config->init_path, value, sizeof(config->init_path));
    config->init_path_set = 1u;
}

static void config_apply_pair(struct kernel_config *config, char *key, char *value) {
    uint8_t bool_value;

    config_trim(key);
    config_trim(value);
    if (key[0] == '\0' || value[0] == '\0') {
        return;
    }
    if (streq(key, "init")) {
        config_set_init_path(config, value);
        return;
    }
    if (streq(key, "ring3_smoke")) {
        if (config_parse_bool(value, &bool_value)) {
            config->ring3_smoke = bool_value;
        }
    }
}

static void config_parse_line(struct kernel_config *config, char *line) {
    uint32_t i;

    config_trim(line);
    if (line[0] == '\0' || line[0] == '#') {
        return;
    }
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '#') {
            line[i] = '\0';
            break;
        }
    }
    config_trim(line);
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '=') {
            line[i] = '\0';
            config_apply_pair(config, line, line + i + 1u);
            return;
        }
    }
}

static void config_parse_buffer(struct kernel_config *config, char *buffer) {
    char line[128];
    uint32_t pos = 0;
    uint32_t line_len = 0;

    while (buffer[pos] != '\0') {
        char ch = buffer[pos++];

        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[line_len] = '\0';
            config_parse_line(config, line);
            line_len = 0;
            continue;
        }
        if (line_len + 1u < sizeof(line)) {
            line[line_len++] = ch;
        }
    }
    if (line_len != 0) {
        line[line_len] = '\0';
        config_parse_line(config, line);
    }
}

void kernel_config_defaults(struct kernel_config *config) {
    if (config == 0) {
        return;
    }
    config->loaded = 0u;
    config->init_path_set = 0u;
    config->ring3_smoke = 1u;
    config->init_path[0] = '\0';
}

void kernel_config_load(struct vfs *vfs, const char *path, struct kernel_config *config) {
    uint32_t bytes_read = 0;

    if (config == 0) {
        return;
    }
    kernel_config_defaults(config);
    if (vfs == 0 || path == 0 || path[0] == '\0') {
        return;
    }
    if (vfs_read_file_all(vfs,
                          path,
                          0,
                          g_kernel_config_buffer,
                          KERNEL_CONFIG_BUFFER_SIZE,
                          &bytes_read) != 0) {
        return;
    }
    g_kernel_config_buffer[bytes_read] = '\0';
    config->loaded = 1u;
    config_parse_buffer(config, g_kernel_config_buffer);
}
