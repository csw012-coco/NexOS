#pragma once

#include "user/libc/include/fcntl.h"
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/stdlib.h"
#include "user/libc/include/string.h"
#include "user/libc/include/strings.h"
#include "user/libc/include/nexos/file.h"
#include "user/libc/include/nexos/fs.h"
#include "user/libc/include/nexos/process.h"
#include "user/libc/include/nexos/string.h"
#include "user/libc/include/nexos/system.h"

#define USH_LINE_MAX 63u
#define USH_HISTORY_MAX 8u
#define USH_VAR_MAX 16u
#define USH_FUNCTION_MAX 8u
#define USH_FUNCTION_BODY_MAX 255u
#define USH_VAR_NAME_MAX 31u
#define USH_VAR_VALUE_MAX 255u

struct ush_editor {
    char line[USH_LINE_MAX + 1];
    char scratch[USH_LINE_MAX + 1];
    uint32_t len;
    uint32_t cursor;
    uint32_t rendered_len;
    int32_t history_index;
    uint8_t scratch_saved;
};

struct ush_command_spec {
    char command[USH_LINE_MAX + 1];
    char input[64];
    char output[64];
    char err_output[64];
    uint8_t append;
    uint8_t err_append;
    uint8_t stderr_to_stdout;
};

struct ush_script_args_snapshot {
    uint32_t argc;
    char argv[10][USH_VAR_VALUE_MAX + 1];
    char joined[USH_VAR_VALUE_MAX + 1];
    char count[12];
};

int ush_build_invoked_command_line(int argc, char **argv, char *out, uint32_t out_size);
int ush_execute_line(char *cwd, const char *line);
int ush_run_script_file(char *cwd, const char *path, int argc, char **argv);
int ush_parse_command_spec(const char *text, struct ush_command_spec *spec);
int ush_split_pipeline(const char *line, char *left, uint32_t left_size, char *right, uint32_t right_size);

void ush_write_error(const char *text);
void ush_write_prompt(void);
void ush_prompt_sync(const char *cwd);
void ush_prompt_override(const char *prompt);
int read_line_chars(struct ush_editor *editor, char *line, uint32_t max_len);
void ush_history_list(void);

int ush_var_name_valid_local(const char *name);
int ush_var_assign_local(const char *name, const char *value, int exported_if_new);
int ush_var_export_local(const char *name, const char *value);
void ush_var_list_local(int exported_only);
void ush_var_list_shell_local(void);
int ush_alias_assign_local(const char *name, const char *value);
void ush_alias_list_local(void);
int ush_function_assign_local(const char *name, const char *body);
const char *ush_function_lookup_local(const char *name);
void ush_function_list_local(void);
void ush_function_recursion_limit_set(int enabled);
int ush_expand_command_text_local(const char *text, char *out, uint32_t out_size);
int ush_parse_assignment_local(const char *text,
                               char *name,
                               uint32_t name_size,
                               char *value,
                               uint32_t value_size);
int ush_expand_variables_local(const char *text, char *out, uint32_t out_size);
void ush_refresh_cwd_local(char *cwd, uint32_t cwd_size);
void ush_init_vars_local(const char *cwd);
void ush_set_script_args_local(int argc, char **argv);
void ush_clear_script_args_local(void);
void ush_save_script_args_local(struct ush_script_args_snapshot *out);
void ush_restore_script_args_local(const struct ush_script_args_snapshot *snapshot);
