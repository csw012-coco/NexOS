#include "user/apps/elf/ush/ush_exec_internal.h"

int g_ush_suppress_background_report;
uint32_t g_ush_last_background_pid;
int g_ush_last_foreground_status;

struct ush_execute_workspace {
    char seq_left[USH_LINE_MAX + 1];
    char seq_right[USH_LINE_MAX + 1];
    char bg_left[USH_LINE_MAX + 1];
    char bg_right[USH_LINE_MAX + 1];
    char bg_command[USH_LINE_MAX + 1];
    char or_left[USH_LINE_MAX + 1];
    char or_right[USH_LINE_MAX + 1];
    char and_left[USH_LINE_MAX + 1];
    char and_right[USH_LINE_MAX + 1];
    char background_line[USH_LINE_MAX + 1];
    char expanded_line[USH_LINE_MAX + 1];
    char expanded_command[USH_LINE_MAX + 1];
    char pipeline_texts[USH_PIPELINE_STAGE_MAX][USH_LINE_MAX + 1];
    struct ush_command_spec pipeline_stages[USH_PIPELINE_STAGE_MAX];
};

static struct ush_execute_workspace g_ush_execute_workspace;

int ush_execute_pipeline_stage_command(char *cwd, const char *line, uint32_t *pid_out) {
    int previous_suppress = g_ush_suppress_background_report;
    int rc;

    if (pid_out != NULL) {
        *pid_out = 0u;
    }
    g_ush_suppress_background_report = 1;
    rc = ush_execute_command_core(cwd, line, 1);
    g_ush_suppress_background_report = previous_suppress;
    if (rc == 0 && pid_out != NULL) {
        *pid_out = g_ush_last_background_pid;
    }
    return rc;
}

static int ush_execute_line_core(char *cwd, const char *line) {
    struct ush_execute_workspace *workspace = &g_ush_execute_workspace;
    char left_copy[USH_LINE_MAX + 1];
    char right_copy[USH_LINE_MAX + 1];
    uint32_t pipeline_stage_count = 0;
    uint32_t i;
    int seq_rc;
    int bg_list_rc;
    int or_rc;
    int and_rc;
    int background = 0;
    int background_rc;
    int pipeline_rc;

    seq_rc = ush_split_sequence(line,
                                workspace->seq_left,
                                sizeof(workspace->seq_left),
                                workspace->seq_right,
                                sizeof(workspace->seq_right));
    if (seq_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (seq_rc > 0) {
        copy_line_local(left_copy, workspace->seq_left, sizeof(left_copy));
        copy_line_local(right_copy, workspace->seq_right, sizeof(right_copy));
        (void)ush_execute_line(cwd, left_copy);
        return ush_execute_line(cwd, right_copy);
    }
    bg_list_rc = ush_split_background_list(line,
                                           workspace->bg_left,
                                           sizeof(workspace->bg_left),
                                           workspace->bg_right,
                                           sizeof(workspace->bg_right));
    if (bg_list_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (bg_list_rc > 0) {
        uint32_t bg_len = str_len_local(workspace->bg_left);

        if (bg_len + 3u > sizeof(workspace->bg_command)) {
            write_err_str("parse error\n");
            return 1;
        }
        copy_line_local(workspace->bg_command,
                        workspace->bg_left,
                        sizeof(workspace->bg_command));
        workspace->bg_command[bg_len++] = ' ';
        workspace->bg_command[bg_len++] = '&';
        workspace->bg_command[bg_len] = '\0';
        copy_line_local(left_copy, workspace->bg_command, sizeof(left_copy));
        copy_line_local(right_copy, workspace->bg_right, sizeof(right_copy));
        (void)ush_execute_line(cwd, left_copy);
        return ush_execute_line(cwd, right_copy);
    }
    or_rc = ush_split_orif(line,
                           workspace->or_left,
                           sizeof(workspace->or_left),
                           workspace->or_right,
                           sizeof(workspace->or_right));
    if (or_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (or_rc > 0) {
        int left_status;

        copy_line_local(left_copy, workspace->or_left, sizeof(left_copy));
        copy_line_local(right_copy, workspace->or_right, sizeof(right_copy));
        left_status = ush_execute_line(cwd, left_copy);
        if (left_status == 0) {
            return 0;
        }
        return ush_execute_line(cwd, right_copy);
    }
    and_rc = ush_split_andif(line,
                             workspace->and_left,
                             sizeof(workspace->and_left),
                             workspace->and_right,
                             sizeof(workspace->and_right));
    if (and_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (and_rc > 0) {
        int left_status;

        copy_line_local(left_copy, workspace->and_left, sizeof(left_copy));
        copy_line_local(right_copy, workspace->and_right, sizeof(right_copy));
        left_status = ush_execute_line(cwd, left_copy);
        if (left_status != 0) {
            return left_status;
        }
        return ush_execute_line(cwd, right_copy);
    }
    background_rc = ush_strip_trailing_background_local(line,
                                                       workspace->background_line,
                                                       sizeof(workspace->background_line),
                                                       &background);
    if (background_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    if (!ush_expand_variables_local(workspace->background_line,
                                    workspace->expanded_line,
                                    sizeof(workspace->expanded_line))) {
        write_err_str("expand error\n");
        return 1;
    }
    pipeline_rc = ush_split_pipeline_stages_local(workspace->expanded_line,
                                                  workspace->pipeline_texts,
                                                  USH_PIPELINE_STAGE_MAX,
                                                  &pipeline_stage_count);
    if (pipeline_rc < 0) {
        write_err_str("parse error\n");
        return 1;
    }
    for (i = 0; i < pipeline_stage_count; i++) {
        if (!ush_parse_command_spec(workspace->pipeline_texts[i],
                                    &workspace->pipeline_stages[i])) {
            write_err_str("parse error\n");
            return 1;
        }
        if (!ush_expand_command_text_local(workspace->pipeline_stages[i].command,
                                           workspace->expanded_command,
                                           sizeof(workspace->expanded_command))) {
            write_err_str("expand error\n");
            return 1;
        }
        copy_line_local(workspace->pipeline_stages[i].command,
                        workspace->expanded_command,
                        sizeof(workspace->pipeline_stages[i].command));
    }
    if (pipeline_stage_count == 1u) {
        return ush_execute_with_redirection(cwd,
                                            &workspace->pipeline_stages[0],
                                            background);
    }
    if (background) {
        write_err_str("background: pipelines are not supported\n");
        return 1;
    }
    return ush_execute_pipeline(cwd,
                                workspace->pipeline_stages,
                                pipeline_stage_count);
}

int ush_execute_line(char *cwd, const char *line) {
    int function_handled = 0;
    int function_rc;

    function_rc = ush_try_function_call_local(cwd, line, 1, &function_handled);
    if (function_handled) {
        return function_rc;
    }
    return ush_execute_line_core(cwd, line);
}
