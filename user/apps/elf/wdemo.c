#include "user/libc/include/fcntl.h"
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/string.h"
#include "user/libc/include/unistd.h"
#include "user/libc/include/nexos/file.h"
#include "user/libc/include/nexos/string.h"
#include "user/libc/include/nexos/system.h"

static uint32_t read_stdin_line(char *line, uint32_t size) {
    ssize_t got;

    if (line == NULL || size == 0u) {
        return 0u;
    }
    got = nex_read(STDIN_FILENO, line, size, NEX_READ_BLOCKING);
    if (got <= 0) {
        line[0] = '\0';
        return 0u;
    }
    line[size - 1u] = '\0';
    return (uint32_t)got;
}

int main(int argc, char **argv) {
    char path[32];
    char text[64];
    char mode[16];
    uint32_t got;
    uint32_t len;
    uint32_t written;
    uint32_t flags = O_CREAT;
    int fd;

    (void)argc;
    (void)argv;

    printf("wdemo ELF: enter path, mode (write/append/trunc), then one line\n");
    printf("file> ");
    got = read_stdin_line(path, sizeof(path));
    if (got == 0) {
        eprintf("read failed\n");
        exit_with_code(1);
    }
    trim_line(path);

    printf("mode> ");
    got = read_stdin_line(mode, sizeof(mode));
    if (got == 0) {
        eprintf("read failed\n");
        exit_with_code(1);
    }
    trim_line(mode);
    if (mode[0] == 'a') {
        flags |= O_APPEND;
    } else if (mode[0] == 't') {
        flags |= O_TRUNC;
    }

    fd = open(path, flags);
    if (fd < 0) {
        eprintf("open failed\n");
        exit_with_code(1);
    }

    printf("text> ");
    got = read_stdin_line(text, sizeof(text));
    if (got == 0) {
        close((uint32_t)fd);
        eprintf("read failed\n");
        exit_with_code(1);
    }
    len = strlen(text);

    written = write_fd((uint32_t)fd, text, len);
    close((uint32_t)fd);
    if (written != len) {
        eprintf("write failed\n");
        exit_with_code(1);
    }

    printf("written bytes=%u\n", written);
    return 0;
}
