#include "user/libc/include/nlibc.h"

static FILE g_stdin_file = {STDIN_FILENO};
static FILE g_stdout_file = {STDOUT_FILENO};
static FILE g_stderr_file = {STDERR_FILENO};

FILE *stdin = &g_stdin_file;
FILE *stdout = &g_stdout_file;
FILE *stderr = &g_stderr_file;

int fgetc(FILE *stream) {
    char ch;

    if (stream == 0) {
        return EOF;
    }
    if (nex_read(stream->fd, &ch, 1u, NEX_READ_BLOCKING | NEX_READ_CHAR) <= 0) {
        return EOF;
    }
    return (unsigned char)ch;
}

char *fgets(char *dst, int size, FILE *stream) {
    int pos = 0;

    if (dst == 0 || size <= 0 || stream == 0) {
        return 0;
    }
    while (pos + 1 < size) {
        int ch = fgetc(stream);

        if (ch == EOF) {
            break;
        }
        dst[pos++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (pos == 0) {
        return 0;
    }
    dst[pos] = '\0';
    return dst;
}

int getchar(void) {
    return fgetc(stdin);
}

uint32_t read_line(uint32_t fd, char *buf, uint32_t size) {
    uint32_t len = 0;
    uint32_t saw_input = 0;
    char ch;

    if (buf == 0 || size == 0) {
        return 0;
    }

    while (len + 1u < size) {
        uint32_t got = (uint32_t)nex_read((int)fd, &ch, 1u, NEX_READ_BLOCKING | NEX_READ_CHAR);

        if (got == 0u) {
            break;
        }
        saw_input = 1u;
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            break;
        }
        buf[len++] = ch;
    }
    buf[len] = '\0';
    if (saw_input == 0u) {
        return 0;
    }
    return len != 0u ? len : 1u;
}

void clear(void) {
    int fd = open("/dev/tty", 0);
    write(fd, "\033[2J\033[H", 7); // Clear screen and move cursor to home position
    close(fd);
}
