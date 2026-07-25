#include "user/apps/elf/ush_exec_internal.h"

static int ush_process_exists_local(uint32_t pid) {
    struct syscall_process_info info;

    if (pid == 0u) {
        return 0;
    }
    for (uint32_t slot = 0; slot < NEX_PROC_SLOTS_MAX; slot++) {
        if (proc_query(NEX_PROC_QUERY_ALL, slot, &info) > 0 &&
            info.pid == pid) {
            return 1;
        }
    }
    return 0;
}

int ush_wait_pipeline_pid(uint32_t pid, int *status_out) {
    struct syscall_process_info info;

    if (status_out != NULL) {
        *status_out = 0;
    }
    if (pid == 0u) {
        return 0;
    }
    while (wait(pid, &info) == 0) {
        if (!ush_process_exists_local(pid)) {
            return 0;
        }
        /*
         * Waiting one timer tick adds up to 10 ms of visible latency after
         * short-lived commands. Yield directly to another runnable process
         * so the child can finish without turning this into a busy loop.
         */
        yield();
    }
    if (status_out != NULL) {
        *status_out = info.exit_code == 0 ? 0 : 1;
    }
    return 1;
}

static void ush_foreground_pipeline_all(const uint32_t *pids, uint32_t count) {
    if (pids == NULL) {
        return;
    }
    for (uint32_t remaining = count; remaining != 0u; remaining--) {
        uint32_t i = remaining - 1u;
        uint32_t pid = pids[i];

        if (pid == 0u) {
            continue;
        }
        (void)fg(pid);
    }
}

static void ush_wait_pipeline_jobs(const uint32_t *pids, uint32_t count, int *status_out) {
    int last_status = 0;

    if (pids == NULL) {
        if (status_out != NULL) {
            *status_out = 0;
        }
        return;
    }
    for (uint32_t remaining = count; remaining != 0u; remaining--) {
        uint32_t i = remaining - 1u;
        int stage_status = 0;

        (void)ush_wait_pipeline_pid(pids[i], &stage_status);
        if (i + 1u == count) {
            last_status = stage_status;
        }
    }
    if (status_out != NULL) {
        *status_out = last_status;
    }
}

int ush_execute_pipeline(char *cwd, const struct ush_command_spec *stages, uint32_t stage_count) {
    uint32_t saved[3];
    uint32_t background_pids[USH_PIPELINE_STAGE_MAX];
    uint32_t background_count = 0u;
    int prev_read = -1;
    int next_read = -1;
    int next_write = -1;
    int rc = 0;
    uint32_t i;

    if (stages == NULL || stage_count < 2u) {
        return 1;
    }
    if (!ush_validate_pipeline_local(stages, stage_count)) {
        return 1;
    }
    if (!ush_save_stdio(saved, 1, 1, 1)) {
        write_err_str("pipe: stdio save failed\n");
        return 1;
    }
    for (i = 0; i < USH_PIPELINE_STAGE_MAX; i++) {
        background_pids[i] = 0u;
    }
    for (i = 0; i < stage_count; i++) {
        next_read = -1;
        next_write = -1;
        if (i + 1u < stage_count) {
            int pair[2];

            if (pipe(pair) < 0) {
                write_err_str("pipe failed\n");
                goto cleanup_pipe;
            }
            next_read = pair[0];
            next_write = pair[1];
        }
        if (!ush_configure_pipeline_stdio(saved, prev_read, next_write, 1)) {
            write_err_str("pipe: dup2 failed\n");
            goto cleanup_pipe;
        }
        if (!ush_apply_pipeline_stage_redirections(cwd,
                                                   &stages[i],
                                                   i == 0u,
                                                   i + 1u == stage_count)) {
            goto cleanup_pipe;
        }
        {
            uint32_t stage_pid = 0u;

            rc = ush_execute_pipeline_stage_command(cwd, stages[i].command, &stage_pid);
            if (rc == 0 && stage_pid != 0u && background_count < USH_PIPELINE_STAGE_MAX) {
                background_pids[background_count++] = stage_pid;
            }
        }
        if (!ush_restore_stdio_keep_saved(saved)) {
            write_err_str("pipe: stdio restore failed\n");
            goto cleanup_pipe;
        }
        if (next_write >= 0) {
            close((uint32_t)next_write);
            next_write = -1;
        }
        if (prev_read >= 0) {
            close((uint32_t)prev_read);
            prev_read = -1;
        }
        prev_read = next_read;
        next_read = -1;
        if (rc != 0) {
            goto cleanup_pipe;
        }
    }
    ush_restore_stdio(saved);
    if (prev_read >= 0) {
        close((uint32_t)prev_read);
    }
    ush_foreground_pipeline_all(background_pids, background_count);
    ush_wait_pipeline_jobs(background_pids, background_count, &rc);
    return rc;

cleanup_pipe:
    if (next_write >= 0) {
        close((uint32_t)next_write);
    }
    if (next_read >= 0) {
        close((uint32_t)next_read);
    }
    if (prev_read >= 0) {
        close((uint32_t)prev_read);
    }
    ush_restore_stdio(saved);
    ush_wait_pipeline_jobs(background_pids, background_count, NULL);
    return rc < 0 ? rc : 1;
}
