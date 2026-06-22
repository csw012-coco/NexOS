#include <nlibc.h>

enum {
    NEXBOX_BUFFER_SIZE = 256u,
    HEXDUMP_ROW_SIZE = 16u,
    NEXBOX_PATH_SIZE = 256u
};

static int dispatch(int argc, char **argv);

static const char *basename(const char *path) {
    const char *name = path;

    if (path == 0) {
        return "";
    }
    while (*path != '\0') {
        if (*path++ == '/') {
            name = path;
        }
    }
    return name;
}

static int write_all(int fd, const void *buffer, size_t size) {
    const char *cursor = buffer;

    while (size != 0u) {
        ssize_t written = write(fd, cursor, size);

        if (written <= 0) {
            return 0;
        }
        cursor += written;
        size -= (size_t)written;
    }
    return 1;
}

static int command_help(void) {
    puts("NexBox-lite i386 multicall userland");
    puts("applets: help echo pwd cd ls cat hexdump touch write append pipe-test");
    puts("usage: nexbox32 <applet> [args...]");
    puts("cd chaining: nexbox32 cd <dir> <applet> [args...]");
    puts("stdin: cat - reads one entered line");
    return 0;
}

static int command_pipe_test(void) {
    static const char direct_message[] = "direct pipe OK";
    char buffer[128];
    int pair[2];
    ssize_t count;

    if (pipe(pair) != 0 ||
        write(pair[1], direct_message, sizeof(direct_message)) !=
            (ssize_t)sizeof(direct_message) ||
        close(pair[1]) != 0) {
        fprintf(stderr, "pipe-test: direct setup failed\n");
        return 1;
    }
    count = read(pair[0], buffer, sizeof(buffer));
    close(pair[0]);
    if (count != (ssize_t)sizeof(direct_message) ||
        memcmp(buffer, direct_message, sizeof(direct_message)) != 0) {
        fprintf(stderr, "pipe-test: direct transfer failed\n");
        return 1;
    }
    puts("pipe-test: direct read/write OK");

    if (pipe(pair) != 0 ||
        dup2(STDOUT_FILENO, 10) != 10 ||
        dup2(pair[1], STDOUT_FILENO) != STDOUT_FILENO) {
        fprintf(stderr, "pipe-test: dup2 setup failed\n");
        return 1;
    }
    {
        pid_t child = spawn("/BOOT/NEXBOX32.ELF echo inherited fd");

        if (child <= 0) {
            return 1;
        }
        if (dup2(10, STDOUT_FILENO) != STDOUT_FILENO) {
            return 1;
        }
        close(10);
        close(pair[1]);
        count = read(pair[0], buffer, sizeof(buffer) - 1u);
        if (count <= 0) {
            fprintf(stderr, "pipe-test: inherited read failed\n");
            return 1;
        }
        {
            int status = waitpid(child);

            if (status != 0) {
                fprintf(stderr, "pipe-test: child status=%d\n", status);
                return 1;
            }
        }
        while ((size_t)count < sizeof(buffer) - 1u) {
            ssize_t more = read(pair[0],
                                buffer + count,
                                sizeof(buffer) - 1u - (size_t)count);

            if (more < 0) {
                close(pair[0]);
                return 1;
            }
            if (more == 0) {
                break;
            }
            count += more;
        }
        close(pair[0]);
    }
    buffer[count] = '\0';
    printf("pipe-test: captured \"%s\"", buffer);
    puts("pipe-test: dup2 + spawn inheritance OK");
    return 0;
}

static int command_echo(int argc, char **argv) {
    int newline = 1;
    int first = 1;

    for (int i = 1; i < argc; i++) {
        if (first && strcmp(argv[i], "-n") == 0) {
            newline = 0;
            continue;
        }
        if (!first && !write_all(STDOUT_FILENO, " ", 1u)) {
            return 1;
        }
        if (!write_all(STDOUT_FILENO, argv[i], strlen(argv[i]))) {
            return 1;
        }
        first = 0;
    }
    if (newline && !write_all(STDOUT_FILENO, "\n", 1u)) {
        return 1;
    }
    return 0;
}

static int command_pwd(void) {
    char cwd[NEXBOX_PATH_SIZE];

    if (getcwd(cwd, sizeof(cwd)) != 0) {
        fprintf(stderr, "pwd: getcwd failed\n");
        return 1;
    }
    puts(cwd);
    return 0;
}

static int command_cd(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "cd: usage: cd <path> [applet args...]\n");
        return 1;
    }
    if (chdir(argv[1]) != 0) {
        fprintf(stderr, "cd: %s: not a directory\n", argv[1]);
        return 1;
    }
    if (argc > 2) {
        return dispatch(argc - 2, argv + 2);
    }
    return command_pwd();
}

static int command_ls(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    struct syscall_dirent entry;
    int fd = opendir(path);
    int result;

    if (fd < 0) {
        fprintf(stderr, "ls: %s: open failed\n", path);
        return 1;
    }
    while ((result = readdir((uint32_t)fd, &entry)) > 0) {
        if ((entry.attributes & 0x10u) != 0u) {
            printf("%s/\n", entry.name);
        } else {
            printf("%s\t%u\n", entry.name, entry.size);
        }
    }
    close(fd);
    if (result < 0) {
        fprintf(stderr, "ls: read failed\n");
        return 1;
    }
    return 0;
}

static int command_cat(int argc, char **argv) {
    char buffer[NEXBOX_BUFFER_SIZE];

    if (argc < 2) {
        char *stdin_argv[] = {"cat", "-", 0};

        return command_cat(2, stdin_argv);
    }
    for (int i = 1; i < argc; i++) {
        int fd = strcmp(argv[i], "-") == 0
            ? STDIN_FILENO
            : open(argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(stderr, "cat: %s: open failed\n", argv[i]);
            return 1;
        }
        for (;;) {
            ssize_t count = read(fd, buffer, sizeof(buffer));

            if (count < 0) {
                fprintf(stderr, "cat: %s: read failed\n", argv[i]);
                close(fd);
                return 1;
            }
            if (count == 0) {
                break;
            }
            if (!write_all(STDOUT_FILENO, buffer, (size_t)count)) {
                if (fd != STDIN_FILENO) {
                    close(fd);
                }
                return 1;
            }
            if (fd == STDIN_FILENO) {
                break;
            }
        }
        if (fd != STDIN_FILENO) {
            close(fd);
        }
    }
    return 0;
}

static int command_touch(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "touch: usage: touch <path>...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_APPEND);

        if (fd < 0) {
            fprintf(stderr, "touch: %s: open failed\n", argv[i]);
            return 1;
        }
        close(fd);
    }
    return 0;
}

static int command_write_file(int argc, char **argv, int append) {
    FILE *stream;

    if (argc < 3) {
        fprintf(stderr,
                "%s: usage: %s <path> <text...>\n",
                append ? "append" : "write",
                append ? "append" : "write");
        return 1;
    }
    stream = fopen(argv[1], append ? "a" : "w");
    if (stream == 0) {
        fprintf(stderr,
                "%s: %s: open failed\n",
                append ? "append" : "write",
                argv[1]);
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        if (i != 2 && fwrite(" ", 1u, 1u, stream) != 1u) {
            fclose(stream);
            return 1;
        }
        if (fwrite(argv[i], 1u, strlen(argv[i]), stream) != strlen(argv[i])) {
            fclose(stream);
            return 1;
        }
    }
    if (fwrite("\n", 1u, 1u, stream) != 1u ||
        fclose(stream) != 0) {
        return 1;
    }
    return 0;
}

static void hexdump_row(uint32_t offset,
                        const uint8_t *row,
                        uint32_t count) {
    printf("%08x  ", offset);
    for (uint32_t i = 0; i < HEXDUMP_ROW_SIZE; i++) {
        if (i < count) {
            printf("%02x ", row[i]);
        } else {
            printf("   ");
        }
        if (i == 7u) {
            putchar(' ');
        }
    }
    printf(" |");
    for (uint32_t i = 0; i < count; i++) {
        uint8_t value = row[i];

        putchar(value >= 0x20u && value <= 0x7eu ? value : '.');
    }
    for (uint32_t i = count; i < HEXDUMP_ROW_SIZE; i++) {
        putchar(' ');
    }
    puts("|");
}

static int command_hexdump(int argc, char **argv) {
    uint8_t buffer[NEXBOX_BUFFER_SIZE];
    uint8_t row[HEXDUMP_ROW_SIZE];
    uint32_t row_count = 0u;
    uint32_t offset = 0u;
    int fd;

    if (argc != 2) {
        fprintf(stderr, "hexdump: usage: hexdump <path>\n");
        return 1;
    }
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "hexdump: %s: open failed\n", argv[1]);
        return 1;
    }
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));

        if (count < 0) {
            fprintf(stderr, "hexdump: read failed\n");
            close(fd);
            return 1;
        }
        if (count == 0) {
            break;
        }
        for (ssize_t i = 0; i < count; i++) {
            row[row_count++] = buffer[i];
            if (row_count == HEXDUMP_ROW_SIZE) {
                hexdump_row(offset, row, row_count);
                offset += row_count;
                row_count = 0u;
            }
        }
    }
    if (row_count != 0u) {
        hexdump_row(offset, row, row_count);
        offset += row_count;
    }
    printf("%08x\n", offset);
    close(fd);
    return 0;
}

static int dispatch(int argc, char **argv) {
    const char *command;

    if (argc <= 0 || argv == 0) {
        return command_help();
    }
    command = basename(argv[0]);
    if (strcmp(command, "NEXBOX32.ELF") == 0 ||
        strcmp(command, "nexbox32.elf") == 0 ||
        strcmp(command, "nexbox32") == 0 ||
        strcmp(command, "nexbox") == 0) {
        if (argc < 2) {
            return command_help();
        }
        argc--;
        argv++;
        command = argv[0];
    }

    if (strcmp(command, "help") == 0) {
        return command_help();
    }
    if (strcmp(command, "echo") == 0) {
        return command_echo(argc, argv);
    }
    if (strcmp(command, "pwd") == 0) {
        return command_pwd();
    }
    if (strcmp(command, "cd") == 0) {
        return command_cd(argc, argv);
    }
    if (strcmp(command, "ls") == 0) {
        return command_ls(argc, argv);
    }
    if (strcmp(command, "cat") == 0) {
        return command_cat(argc, argv);
    }
    if (strcmp(command, "hexdump") == 0) {
        return command_hexdump(argc, argv);
    }
    if (strcmp(command, "touch") == 0) {
        return command_touch(argc, argv);
    }
    if (strcmp(command, "write") == 0) {
        return command_write_file(argc, argv, 0);
    }
    if (strcmp(command, "append") == 0) {
        return command_write_file(argc, argv, 1);
    }
    if (strcmp(command, "pipe-test") == 0) {
        return command_pipe_test();
    }
    fprintf(stderr, "nexbox-lite: unsupported applet: %s\n", command);
    return 127;
}

int main(int argc, char **argv) {
    return dispatch(argc, argv);
}
