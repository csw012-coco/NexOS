#include "user/apps/elf/ush_exec_internal.h"

int ush_validate_pipeline_local(const struct ush_command_spec *stages, uint32_t stage_count) {
    uint32_t i;

    if (stages == NULL || stage_count < 2u) {
        return 0;
    }
    for (i = 0; i < stage_count; i++) {
        if (i != 0u && stages[i].input[0] != '\0') {
            write_err_str("pipe: only the first command can use input redirection\n");
            return 0;
        }
        if (i + 1u != stage_count && stages[i].output[0] != '\0') {
            write_err_str("pipe: only the last command can use output redirection\n");
            return 0;
        }
    }
    return 1;
}

int ush_configure_pipeline_stdio(const uint32_t saved[3],
                                        int read_fd,
                                        int write_fd,
                                        int restore_stderr) {
    if (read_fd >= 0) {
        if (dup2(read_fd, STDIN_FILENO) < 0) {
            return 0;
        }
    } else if (saved[0] != 0xffffffffu && dup2(saved[0], STDIN_FILENO) < 0) {
        return 0;
    }
    if (write_fd >= 0) {
        if (dup2(write_fd, STDOUT_FILENO) < 0) {
            return 0;
        }
    } else if (saved[1] != 0xffffffffu && dup2(saved[1], STDOUT_FILENO) < 0) {
        return 0;
    }
    if (restore_stderr && saved[2] != 0xffffffffu && dup2(saved[2], STDERR_FILENO) < 0) {
        return 0;
    }
    return 1;
}

int ush_apply_pipeline_stage_redirections(const char *cwd,
                                                 const struct ush_command_spec *spec,
                                                 int allow_input,
                                                 int allow_output) {
    int fd;

    if (spec->input[0] != '\0') {
        if (!allow_input) {
            write_err_str("pipe: only the first command can use input redirection\n");
            return 0;
        }
        if (!ush_check_device_redirect_cap(cwd, spec->input, USH_ACTION_CAP_DEVICE_READ, "device.read")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->input, 0);
        if (fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDIN_FILENO);
            write_err_str("pipe: input open failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDIN_FILENO);
    }
    if (spec->output[0] != '\0') {
        uint32_t flags;

        if (!allow_output) {
            write_err_str("pipe: only the last command can use output redirection\n");
            return 0;
        }
        if (!ush_check_device_redirect_cap(cwd, spec->output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        flags = O_CREAT | (spec->append ? O_APPEND : O_TRUNC);
        fd = ush_open_resolved_path(cwd, spec->output, flags);
        if (fd < 0) {
            write_err_str("pipe: output open failed\n");
            return 0;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDOUT_FILENO);
            write_err_str("pipe: output dup failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDOUT_FILENO);
    }
    if (spec->stderr_to_stdout) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            write_err_str("pipe: stderr dup failed\n");
            return 0;
        }
    } else if (spec->err_output[0] != '\0') {
        uint32_t flags = O_CREAT | (spec->err_append ? O_APPEND : O_TRUNC);

        if (!ush_check_device_redirect_cap(cwd, spec->err_output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->err_output, flags);
        if (fd < 0) {
            write_err_str("pipe: stderr open failed\n");
            return 0;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDERR_FILENO);
            write_err_str("pipe: stderr dup failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDERR_FILENO);
    }
    return 1;
}

int ush_apply_redirections(const char *cwd, const struct ush_command_spec *spec) {
    int fd;

    if (spec->input[0] != '\0') {
        if (!ush_check_device_redirect_cap(cwd, spec->input, USH_ACTION_CAP_DEVICE_READ, "device.read")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->input, 0);
        if (fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDIN_FILENO);
            write_err_str("redirect: input open failed\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDIN_FILENO);
    }
    if (spec->output[0] != '\0') {
        uint32_t flags = O_CREAT | (spec->append ? O_APPEND : O_TRUNC);

        if (!ush_check_device_redirect_cap(cwd, spec->output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->output, flags);
        if (fd < 0) {
            write_err_str("redirect: output open failed: ");
            write_err_str(spec->output);
            write_err_str("\n");
            return 0;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDOUT_FILENO);
            write_err_str("redirect: output dup failed: ");
            write_err_str(spec->output);
            write_err_str("\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDOUT_FILENO);
    }
    if (spec->stderr_to_stdout) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
            write_err_str("redirect: stderr dup failed\n");
            return 0;
        }
    } else if (spec->err_output[0] != '\0') {
        uint32_t flags = O_CREAT | (spec->err_append ? O_APPEND : O_TRUNC);

        if (!ush_check_device_redirect_cap(cwd, spec->err_output, USH_ACTION_CAP_DEVICE_WRITE, "device.write")) {
            return 0;
        }
        fd = ush_open_resolved_path(cwd, spec->err_output, flags);
        if (fd < 0) {
            write_err_str("redirect: stderr open failed: ");
            write_err_str(spec->err_output);
            write_err_str("\n");
            return 0;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            ush_close_if_not_target_local(fd, STDERR_FILENO);
            write_err_str("redirect: stderr dup failed: ");
            write_err_str(spec->err_output);
            write_err_str("\n");
            return 0;
        }
        ush_close_if_not_target_local(fd, STDERR_FILENO);
    }
    return 1;
}

