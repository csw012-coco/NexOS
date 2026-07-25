#pragma once

#include "user/apps/elf/ush_shared.h"

enum {
    USH_PIPELINE_STAGE_MAX = 8u,
    USH_ACTION_CAP_DEVICE_READ = 1u << 16,
    USH_ACTION_CAP_DEVICE_WRITE = 1u << 21,
    USH_ACTION_CAP_ALL = (1u << 22) - 1u
};

extern int g_ush_suppress_background_report;
extern uint32_t g_ush_last_background_pid;
extern int g_ush_last_foreground_status;

void ush_init_saved_stdio(uint32_t saved[3]);
int ush_save_stdio(uint32_t saved[3], int save_stdin, int save_stdout, int save_stderr);
void ush_restore_stdio(const uint32_t saved[3]);
int ush_restore_stdio_keep_saved(const uint32_t saved[3]);
void ush_close_if_not_target_local(int fd, uint32_t target_fd);

int streq_local(const char *a, const char *b);
int starts_with_local(const char *text, const char *prefix);
int contains_char_local(const char *text, char ch);
uint32_t str_len_local(const char *text);
void copy_line_local(char *dst, const char *src, uint32_t max_len);
void trim_in_place_local(char *text);
const char *skip_spaces_local(const char *text);
void upper_in_place_local(char *text);
void lower_in_place_local(char *text);
int ends_with_ignore_case_local(const char *text, const char *suffix);
int read_token_local(const char **text_io, char *out, uint32_t out_size);
void ush_write_colored_err(const char *ansi, const char *text);
int ush_parse_exit_code_local(const char *text, uint64_t *code_out);
int ush_source_script_local(char *cwd, const char *text);
int ush_session_load_local(char *cwd, const char *text);
int ush_preload_file_local(const char *path);
int ush_change_directory(char *cwd, uint32_t cwd_size, const char *arg);
int ush_try_function_call_local(char *cwd, const char *line, int require_plain_line, int *handled_out);
int ush_try_external_command(char *cwd, const char *line, int background, int *handled_out);

int ush_check_device_redirect_cap(const char *cwd, const char *path, uint32_t cap, const char *op);
int ush_open_resolved_path(const char *cwd, const char *arg, uint32_t flags);
int ush_validate_pipeline_local(const struct ush_command_spec *stages, uint32_t stage_count);
int ush_configure_pipeline_stdio(const uint32_t saved[3], int read_fd, int write_fd, int restore_stderr);
int ush_apply_pipeline_stage_redirections(const char *cwd,
                                          const struct ush_command_spec *spec,
                                          int allow_input,
                                          int allow_output);
int ush_apply_redirections(const char *cwd, const struct ush_command_spec *spec);

int ush_execute_command_core(char *cwd, const char *line, int background);
int ush_execute_with_redirection(char *cwd, const struct ush_command_spec *spec, int background);
int ush_execute_pipeline_stage_command(char *cwd, const char *line, uint32_t *pid_out);
int ush_wait_pipeline_pid(uint32_t pid, int *status_out);
int ush_execute_pipeline(char *cwd, const struct ush_command_spec *stages, uint32_t stage_count);
