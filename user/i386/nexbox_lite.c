#include <nlibc.h>

enum {
    NEXBOX_BUFFER_SIZE = 256u,
    HEXDUMP_ROW_SIZE = 16u,
    NEXBOX_PATH_SIZE = 256u,
    SHELL_TOKEN_MAX = 32u,
    SHELL_WORD_MAX = 16u,
    SHELL_TEXT_MAX = 512u,
    SHELL_SAVE_STDIN = 12,
    SHELL_SAVE_STDOUT = 13
};

static int dispatch(int argc, char **argv);

struct shell_stage {
    char *words[SHELL_WORD_MAX];
    int word_count;
    const char *input_path;
    const char *output_path;
};

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
    puts("applets: help echo pwd cd ls cat hexdump touch write append pipe-test sh shell-test");
    puts("usage: nexbox32 <applet> [args...]");
    puts("cd chaining: nexbox32 cd <dir> <applet> [args...]");
    puts("stdin: cat - reads one entered line");
    return 0;
}

static int shell_tokenize(int argc,
                          char **argv,
                          char storage[SHELL_TEXT_MAX],
                          char *tokens[SHELL_TOKEN_MAX]) {
    uint32_t used = 0u;
    int count = 0;

    for (int argument = 1; argument < argc; argument++) {
        const char *cursor = argv[argument];

        while (*cursor != '\0') {
            uint32_t start;

            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
            if (*cursor == '\0') {
                break;
            }
            if (count >= (int)SHELL_TOKEN_MAX || used + 2u > SHELL_TEXT_MAX) {
                return -1;
            }
            tokens[count++] = storage + used;
            if (*cursor == '|' || *cursor == '<' || *cursor == '>') {
                storage[used++] = *cursor++;
                storage[used++] = '\0';
                continue;
            }
            start = used;
            while (*cursor != '\0' &&
                   *cursor != ' ' &&
                   *cursor != '\t' &&
                   *cursor != '|' &&
                   *cursor != '<' &&
                   *cursor != '>') {
                if (used + 1u >= SHELL_TEXT_MAX) {
                    return -1;
                }
                storage[used++] = *cursor++;
            }
            if (used == start) {
                return -1;
            }
            storage[used++] = '\0';
        }
    }
    return count;
}

static int shell_parse_stage(char **tokens,
                             int begin,
                             int end,
                             struct shell_stage *stage) {
    memset(stage, 0, sizeof(*stage));
    for (int i = begin; i < end; i++) {
        if (strcmp(tokens[i], "<") == 0 ||
            strcmp(tokens[i], ">") == 0) {
            int input = tokens[i][0] == '<';

            if (++i >= end ||
                strcmp(tokens[i], "<") == 0 ||
                strcmp(tokens[i], ">") == 0 ||
                strcmp(tokens[i], "|") == 0) {
                return 0;
            }
            if (input) {
                if (stage->input_path != 0) {
                    return 0;
                }
                stage->input_path = tokens[i];
            } else {
                if (stage->output_path != 0) {
                    return 0;
                }
                stage->output_path = tokens[i];
            }
            continue;
        }
        if (stage->word_count >= (int)SHELL_WORD_MAX) {
            return 0;
        }
        stage->words[stage->word_count++] = tokens[i];
    }
    return stage->word_count != 0;
}

static int shell_build_command(const struct shell_stage *stage,
                               char command[SHELL_TEXT_MAX]) {
    uint32_t used = 0u;
    const char *prefix = stage->words[0][0] == '/'
        ? ""
        : "/BOOT/NEXBOX32.ELF ";

    for (uint32_t i = 0u; prefix[i] != '\0'; i++) {
        command[used++] = prefix[i];
    }
    for (int word = 0; word < stage->word_count; word++) {
        if (word != 0) {
            if (used + 1u >= SHELL_TEXT_MAX) {
                return 0;
            }
            command[used++] = ' ';
        }
        for (uint32_t i = 0u; stage->words[word][i] != '\0'; i++) {
            if (used + 1u >= SHELL_TEXT_MAX) {
                return 0;
            }
            command[used++] = stage->words[word][i];
        }
    }
    command[used] = '\0';
    return 1;
}

static pid_t shell_spawn_stage(const struct shell_stage *stage,
                               int input_fd,
                               int output_fd) {
    char command[SHELL_TEXT_MAX];
    int opened_input = -1;
    int opened_output = -1;
    pid_t child = -1;

    if (!shell_build_command(stage, command) ||
        dup2(STDIN_FILENO, SHELL_SAVE_STDIN) != SHELL_SAVE_STDIN ||
        dup2(STDOUT_FILENO, SHELL_SAVE_STDOUT) != SHELL_SAVE_STDOUT) {
        return -1;
    }
    if (stage->input_path != 0) {
        opened_input = open(stage->input_path, O_RDONLY);
        input_fd = opened_input;
    }
    if (stage->output_path != 0) {
        opened_output = open(stage->output_path,
                             O_WRONLY | O_CREAT | O_TRUNC);
        output_fd = opened_output;
    }
    if ((stage->input_path != 0 && opened_input < 0) ||
        (stage->output_path != 0 && opened_output < 0) ||
        (input_fd >= 0 && dup2(input_fd, STDIN_FILENO) != STDIN_FILENO) ||
        (output_fd >= 0 && dup2(output_fd, STDOUT_FILENO) != STDOUT_FILENO)) {
        goto restore;
    }
    if (opened_input >= 0) {
        close(opened_input);
        opened_input = -1;
    }
    if (opened_output >= 0) {
        close(opened_output);
        opened_output = -1;
    }
    child = spawn(command);

restore:
    (void)dup2(SHELL_SAVE_STDIN, STDIN_FILENO);
    (void)dup2(SHELL_SAVE_STDOUT, STDOUT_FILENO);
    close(SHELL_SAVE_STDIN);
    close(SHELL_SAVE_STDOUT);
    if (opened_input >= 0) {
        close(opened_input);
    }
    if (opened_output >= 0) {
        close(opened_output);
    }
    return child;
}

static int command_shell(int argc, char **argv) {
    char storage[SHELL_TEXT_MAX];
    char *tokens[SHELL_TOKEN_MAX];
    struct shell_stage left;
    struct shell_stage right;
    int token_count;
    int pipe_index = -1;
    int pair[2] = {-1, -1};
    pid_t left_child;
    pid_t right_child = -1;
    int left_status;
    int right_status = 0;

    token_count = shell_tokenize(argc, argv, storage, tokens);
    if (token_count <= 0) {
        fprintf(stderr, "sh: usage: sh command [| command] [< file] [> file]\n");
        return 2;
    }
    for (int i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (pipe_index >= 0) {
                fprintf(stderr, "sh: only one pipe is currently supported\n");
                return 2;
            }
            pipe_index = i;
        }
    }
    if (!shell_parse_stage(tokens,
                           0,
                           pipe_index >= 0 ? pipe_index : token_count,
                           &left) ||
        (pipe_index >= 0 &&
         !shell_parse_stage(tokens,
                            pipe_index + 1,
                            token_count,
                            &right))) {
        fprintf(stderr, "sh: invalid command or redirection\n");
        return 2;
    }
    if (pipe_index < 0 &&
        left.input_path == 0 &&
        left.output_path == 0 &&
        strcmp(left.words[0], "cd") == 0) {
        const char *path = left.word_count > 1 ? left.words[1] : "/";

        if (left.word_count > 2) {
            fprintf(stderr, "cd: usage: cd [path]\n");
            return 2;
        }
        if (chdir(path) != 0) {
            fprintf(stderr, "cd: %s: not a directory\n", path);
            return 1;
        }
        return 0;
    }
    if (pipe_index < 0) {
        left_child = shell_spawn_stage(&left, -1, -1);
        return left_child > 0 ? waitpid(left_child) : 1;
    }
    if (left.output_path != 0 || right.input_path != 0) {
        fprintf(stderr, "sh: pipe conflicts with explicit redirection\n");
        return 2;
    }
    if (pipe(pair) != 0) {
        fprintf(stderr, "sh: pipe creation failed\n");
        return 1;
    }
    left_child = shell_spawn_stage(&left, -1, pair[1]);
    close(pair[1]);
    pair[1] = -1;
    if (left_child <= 0) {
        close(pair[0]);
        return 1;
    }
    right_child = shell_spawn_stage(&right, pair[0], -1);
    close(pair[0]);
    if (right_child <= 0) {
        (void)waitpid(left_child);
        return 1;
    }
    left_status = waitpid(left_child);
    right_status = waitpid(right_child);
    return right_status != 0 ? right_status : left_status;
}

static int command_shell_test(void) {
    char *pipeline[] = {"sh", "echo", "pipeline", "|", "cat", 0};
    char *redirect_out[] = {
        "sh", "echo", "redirected", ">", "/SHELL.TXT", 0
    };
    char *redirect_in[] = {"sh", "cat", "<", "/SHELL.TXT", 0};

    if (command_shell(5, pipeline) != 0) {
        fprintf(stderr, "shell-test: pipeline failed\n");
        return 1;
    }
    if (command_shell(5, redirect_out) != 0) {
        fprintf(stderr, "shell-test: output redirection failed\n");
        return 1;
    }
    if (command_shell(4, redirect_in) != 0) {
        fprintf(stderr, "shell-test: input redirection failed\n");
        return 1;
    }
    puts("shell-test: pipe + redirection OK");
    return 0;
}

static int command_ush(void) {
    char line[SHELL_TEXT_MAX];
    char cwd[NEXBOX_PATH_SIZE];

    puts("NexOS ush32");
    puts("type 'help' for applets, 'exit' to leave");
    for (;;) {
        char *shell_argv[] = {"sh", line, 0};
        ssize_t count;

        if (getcwd(cwd, sizeof(cwd)) != 0) {
            cwd[0] = '?';
            cwd[1] = '\0';
        }
        printf("ush32:%s$ ", cwd);
        count = read(STDIN_FILENO, line, sizeof(line) - 1u);
        if (count <= 0) {
            putchar('\n');
            return count == 0 ? 0 : 1;
        }
        line[count] = '\0';
        while (count > 0 &&
               (line[count - 1] == '\n' || line[count - 1] == '\r')) {
            line[--count] = '\0';
        }
        if (line[0] == '\0') {
            continue;
        }
        if (strcmp(line, "exit") == 0) {
            return 0;
        }
        (void)command_shell(2, shell_argv);
    }
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
    if (strcmp(command, "USH32.ELF") == 0 ||
        strcmp(command, "ush32.elf") == 0 ||
        strcmp(command, "ush32") == 0 ||
        strcmp(command, "ush") == 0) {
        return command_ush();
    }
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
    if (strcmp(command, "sh") == 0) {
        return command_shell(argc, argv);
    }
    if (strcmp(command, "shell-test") == 0) {
        return command_shell_test();
    }
    if (strcmp(command, "ush") == 0) {
        return command_ush();
    }
    fprintf(stderr, "nexbox-lite: unsupported applet: %s\n", command);
    return 127;
}

int main(int argc, char **argv) {
    return dispatch(argc, argv);
}
