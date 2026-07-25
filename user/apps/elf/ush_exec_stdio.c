#include "user/apps/elf/ush_exec_internal.h"

void ush_init_saved_stdio(uint32_t saved[3]) {
    saved[0] = 0xffffffffu;
    saved[1] = 0xffffffffu;
    saved[2] = 0xffffffffu;
}

static int ush_save_one_stdio(uint32_t saved[3], uint32_t fd_index) {
    static const uint32_t save_fds[3] = {13u, 14u, 15u};
    uint32_t save_fd;

    if (saved == NULL || fd_index > 2u || saved[fd_index] != 0xffffffffu) {
        return 0;
    }
    save_fd = save_fds[fd_index];
    if (dup2(fd_index, save_fd) < 0) {
        return 0;
    }
    saved[fd_index] = save_fd;
    return 1;
}

int ush_save_stdio(uint32_t saved[3], int save_stdin, int save_stdout, int save_stderr) {
    ush_init_saved_stdio(saved);
    if (save_stdin && !ush_save_one_stdio(saved, 0u)) {
        return 0;
    }
    if (save_stdout && !ush_save_one_stdio(saved, 1u)) {
        ush_restore_stdio(saved);
        return 0;
    }
    if (save_stderr && !ush_save_one_stdio(saved, 2u)) {
        ush_restore_stdio(saved);
        return 0;
    }
    return 1;
}

void ush_restore_stdio(const uint32_t saved[3]) {
    if (saved[0] != 0xffffffffu) {
        (void)dup2(saved[0], STDIN_FILENO);
        if (saved[0] != STDIN_FILENO) {
            (void)close(saved[0]);
        }
    }
    if (saved[1] != 0xffffffffu) {
        (void)dup2(saved[1], STDOUT_FILENO);
        if (saved[1] != STDOUT_FILENO) {
            (void)close(saved[1]);
        }
    }
    if (saved[2] != 0xffffffffu) {
        (void)dup2(saved[2], STDERR_FILENO);
        if (saved[2] != STDERR_FILENO) {
            (void)close(saved[2]);
        }
    }
}

int ush_restore_stdio_keep_saved(const uint32_t saved[3]) {
    if (saved[0] != 0xffffffffu && dup2(saved[0], STDIN_FILENO) < 0) {
        return 0;
    }
    if (saved[1] != 0xffffffffu && dup2(saved[1], STDOUT_FILENO) < 0) {
        return 0;
    }
    if (saved[2] != 0xffffffffu && dup2(saved[2], STDERR_FILENO) < 0) {
        return 0;
    }
    return 1;
}

void ush_close_if_not_target_local(int fd, uint32_t target_fd) {
    if (fd >= 0 && (uint32_t)fd != target_fd) {
        close((uint32_t)fd);
    }
}
