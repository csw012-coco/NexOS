#include "user/libc/include/fcntl.h"
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/string.h"
#include "user/libc/include/unistd.h"
#include "user/libc/include/nexos/file.h"
#include "user/libc/include/nexos/string.h"
#include "user/libc/include/nexos/system.h"

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
    got = read_line(STDIN_FILENO, path, sizeof(path));
    if (got == 0) {
        eprintf("read failed\n");
        exit_with_code(1);
    }
    trim_line(path);

    printf("mode> ");
    got = read_line(STDIN_FILENO, mode, sizeof(mode));
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
    got = read_line(STDIN_FILENO, text, sizeof(text));
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
