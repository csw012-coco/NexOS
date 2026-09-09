#include "user/apps/elf/ush/ush_exec_internal.h"

static const char ush_media_ansi_error[] = "\x1b[1;31m";
static const char ush_media_ansi_value[] = "\x1b[1;33m";

static int ush_parse_wav_command(const char *original_line,
                                 const char *path,
                                 char *out,
                                 uint32_t out_size) {
    char token[64];
    const char *cursor = original_line;
    uint32_t out_len = 0;
    uint32_t i;

    if (!ends_with_ignore_case_local(path, ".wav")) {
        return 0;
    }
    copy_line_local(out, "/cmd/mplay", out_size);
    out_len = str_len_local(out);
    if (out_len + str_len_local(path) + 2u >= out_size) {
        return 0;
    }
    out[out_len++] = ' ';
    for (i = 0; path[i] != '\0'; i++) {
        out[out_len++] = path[i];
    }
    if (!read_token_local(&cursor, token, sizeof(token))) {
        out[out_len] = '\0';
        return 1;
    }
    cursor = skip_spaces_local(cursor);
    if (cursor != NULL && *cursor != '\0') {
        if (out_len + str_len_local(cursor) + 2u >= out_size) {
            return 0;
        }
        out[out_len++] = ' ';
        for (i = 0; cursor[i] != '\0'; i++) {
            out[out_len++] = cursor[i];
        }
    }
    out[out_len] = '\0';
    return 1;
}

int ush_try_wav_command(const char *line, const char *path, int background) {
    char command[256];
    int rc;

    if (!ush_parse_wav_command(line, path, command, sizeof(command))) {
        return 0;
    }
    rc = ush_spawn_command_local(command, SYS_SPAWN_ELF, background);
    if (rc == 0) {
        return 1;
    }
    ush_write_colored_err(ush_media_ansi_error, "mplay exec failed: ");
    ush_write_colored_err(ush_media_ansi_value, command);
    write_err_str(" rc=");
    eprintf("%d\n", rc);
    return 1;
}
