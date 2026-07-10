#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"

struct cmd32_entry {
    const char *name;
    int (*fn)(int argc, char **argv);
};

static int wrap_blk(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_blk();
}

static int wrap_mounts(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_mounts();
}

static int wrap_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_ps();
}

static int wrap_kill(int argc, char **argv) {
    return cmd_kill_like(argc, argv, "kill");
}

static int wrap_meminfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_meminfo();
}

static int wrap_minfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return cmd_minfo();
}

static int wrap_drivers(int argc, char **argv) {
    return cmd_drivers(argc, argv);
}

static const struct cmd32_entry commands[] = {
    {"blk", wrap_blk},
    {"parts", cmd_parts},
    {"mounts", wrap_mounts},
    {"mount", cmd_mount},
    {"umount", cmd_umount},
    {"df", cmd_df},
    {"ps", wrap_ps},
    {"kill", wrap_kill},
    {"sysinfo", cmd_sysinfo},
    {"meminfo", wrap_meminfo},
    {"minfo", wrap_minfo},
    {"uname", cmd_uname},
    {"fb", cmd_fb},
    {"drivers", wrap_drivers},
};

static const char *basename32(const char *path) {
    const char *base = path;

    if (path == 0) {
        return "";
    }
    while (*path != '\0') {
        if (*path == '/' || *path == '\\') {
            base = path + 1;
        }
        path++;
    }
    return base;
}

int cmd_help(void) {
    uint32_t i;

    write_str("NEXBOX32 cmdsuite subset\n");
    write_str("commands:");
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        write_str(" ");
        write_str(commands[i].name);
    }
    write_str("\n");
    return 0;
}

int cmdsuite_dispatch_main(int argc, char **argv) {
    const char *name;
    uint32_t i;

    if (argc <= 0 || argv == 0 || argv[0] == 0) {
        return cmd_help();
    }
    name = basename32(argv[0]);
    if (streq_ignore_case_local(name, "NEXBOX32.ELF") ||
        streq_ignore_case_local(name, "NEXBOX32") ||
        streq_ignore_case_local(name, "NEXBOX") ||
        streq_ignore_case_local(name, "CMDSUITE")) {
        if (argc < 2) {
            cmd_help();
            return 127;
        }
        argc--;
        argv++;
        name = argv[0];
    }
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (streq_ignore_case_local(name, commands[i].name)) {
            return commands[i].fn(argc, argv);
        }
    }
    write_err_text("nexbox32: unknown command: ");
    write_err_text(name);
    write_err_text("\n");
    return 127;
}

int main(int argc, char **argv) {
    return cmdsuite_dispatch_main(argc, argv);
}
