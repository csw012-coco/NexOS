#include "fs/vfs_internal.h"
#include "arch/x86/i386/services/shared_services.h"
#include "kernel/public/core/tty.h"
#include "kernel/public/proc/boot_user_init.h"
#include "kernel/public/proc/process_scheduler_ops.h"
#include "lib/string.h"

static void __attribute__((noreturn)) command_services_panic_init_exit(const char *reason) {
    boot_user_services_log(reason);
    for (;;) {
        __asm__ volatile("cli; hlt" : : : "memory");
    }
}

static int command_starts_with(const char *line, const char *command) {
    while (*command != '\0') {
        if (*line++ != *command++) {
            return 0;
        }
    }
    return *line == '\0' || *line == ' ';
}

static const char *command_argument(const char *line) {
    while (*line != '\0' && *line != ' ') {
        line++;
    }
    while (*line == ' ') {
        line++;
    }
    return line;
}

static void command_services_ls(const char *path) {
    struct vfs *vfs = shared_services_active_vfs();
    struct tty *tty = shared_services_active_tty();
    struct vfs_node directory;
    struct vfs_dirent entry;
    uint32_t index = 0;
    int64_t result;

    if (vfs == 0 || tty == 0) {
        return;
    }
    if (path == 0 || path[0] == '\0') {
        path = "/";
    }
    if (vfs_opendir(vfs, path, &directory) != 0) {
        tty_write_str(tty, "ls: directory not found\n", 0x0cu);
        return;
    }

    while ((result = vfs_readdir(vfs, &directory, &index, &entry)) > 0) {
        tty_write_str(tty, entry.name, 0x0fu);
        if ((entry.attributes & VFS_ATTR_DIR) != 0u) {
            tty_write_str(tty, "/\n", 0x0au);
        } else {
            tty_write_str(tty, "\n", 0x0fu);
        }
    }
    if (result < 0) {
        tty_write_str(tty, "ls: read error\n", 0x0cu);
    }
}

static void command_services_cat(const char *path) {
    struct vfs *vfs = shared_services_active_vfs();
    struct tty *tty = shared_services_active_tty();
    struct vfs_node node;
    uint32_t offset = 0;
    char buffer[129];
    int64_t count;

    if (vfs == 0 || tty == 0) {
        return;
    }
    if (path == 0 || path[0] == '\0') {
        tty_write_str(tty, "usage: cat <path>\n", 0x0eu);
        return;
    }
    if (vfs_open(vfs, path, 0u, &node) != 0) {
        tty_write_str(tty, "cat: file not found\n", 0x0cu);
        return;
    }

    do {
        count = vfs_read(vfs,
                         &node,
                         &offset,
                         buffer,
                         sizeof(buffer) - 1u,
                         VFS_READ_BLOCKING);
        if (count > 0) {
            buffer[count] = '\0';
            tty_write(tty, buffer, (uint32_t)count, 0x0fu);
        }
    } while (count > 0);
    if (count < 0) {
        tty_write_str(tty, "\ncat: read error\n", 0x0cu);
    } else {
        tty_write_str(tty, "\n", 0x0fu);
    }
}

static void command_services_ps(void) {
    struct tty *tty = shared_services_active_tty();

    if (tty == 0) {
        return;
    }
    for (uint32_t slot = 0; slot < 8u; slot++) {
        struct process_snapshot snapshot;

        if (!process_scheduler_snapshot(slot, &snapshot)) {
            continue;
        }
        tty_write_str(tty, "pid=", 0x0fu);
        tty_write_dec(tty, snapshot.pid, 0x0fu);
        tty_write_str(tty, " state=", 0x0fu);
        tty_write_dec(tty, snapshot.state, 0x0fu);
        tty_write_str(tty, " exit=", 0x0fu);
        tty_write_dec(tty, (uint32_t)snapshot.exit_code, 0x0fu);
        tty_write_str(tty, " name=", 0x0fu);
        tty_write_str(tty, snapshot.name, 0x0au);
        tty_write_str(tty, "\n", 0x0fu);
    }
}

static void command_services_test32(void) {
    (void)smoke_services_run_test32_selftest();
}

static void command_services_run(const char *command) {
    struct boot_user_init_config config;
    struct tty *tty = shared_services_active_tty();

    if (command == 0 || command[0] == '\0') {
        if (tty != 0) {
            tty_write_str(tty,
                          "usage: run <path> [args...]\n",
                          0x0eu);
        }
        return;
    }
    boot_user_services_init_config(&config);
    (void)boot_user_init_run_command(&config, command);
}

int command_services_autostart_shell(void) {
    struct boot_user_init_config config;

    boot_user_services_init_config(&config);
    boot_user_services_apply_kernel_config(&config);
    if (!boot_user_init_autostart_shell(&config)) {
        command_services_panic_init_exit("KERNEL PANIC: init failed");
    }
    command_services_panic_init_exit("KERNEL PANIC: init exited");
}

static int command_services_has_shell_operator(const char *line) {
    while (*line != '\0') {
        if (*line == '|' || *line == '<' || *line == '>') {
            return 1;
        }
        line++;
    }
    return 0;
}

static void command_services_shell(const char *line) {
    char command[512];
    static const char prefix[] = "/cmd/nexbox sh ";
    uint32_t used = 0u;
    struct tty *tty = shared_services_active_tty();

    for (uint32_t i = 0u; prefix[i] != '\0'; i++) {
        command[used++] = prefix[i];
    }
    while (*line != '\0' && used + 1u < sizeof(command)) {
        command[used++] = *line++;
    }
    if (*line != '\0') {
        if (tty != 0) {
            tty_write_str(tty, "sh: command too long\n", 0x0cu);
        }
        return;
    }
    command[used] = '\0';
    command_services_run(command);
}

void command_services_execute(const char *line) {
    struct tty *tty = shared_services_active_tty();

    if (line[0] == '\0' || tty == 0) {
        return;
    }
    if (command_services_has_shell_operator(line)) {
        command_services_shell(line);
    } else if (streq(line, "help")) {
        tty_write_str(tty,
                      "commands: help clear ps test32 run <elf> [args] ls [path] cat <path>\n"
                      "shell: cmd1 | cmd2, cmd > file, cmd < file\n",
                      0x0fu);
    } else if (streq(line, "clear")) {
        tty_clear(tty);
    } else if (command_starts_with(line, "ls")) {
        command_services_ls(command_argument(line));
    } else if (command_starts_with(line, "cat")) {
        command_services_cat(command_argument(line));
    } else if (streq(line, "ps")) {
        command_services_ps();
    } else if (streq(line, "test32")) {
        command_services_test32();
    } else if (command_starts_with(line, "run")) {
        command_services_run(command_argument(line));
    } else {
        tty_write_str(tty, "unknown command: ", 0x0cu);
        tty_write_str(tty, line, 0x0fu);
        tty_write_str(tty, "\n", 0x0fu);
    }
}
