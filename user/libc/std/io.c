#include "user/libc/include/nlibc.h"

static FILE g_stdin_file = {STDIN_FILENO, 0};
static FILE g_stdout_file = {STDOUT_FILENO, 0};
static FILE g_stderr_file = {STDERR_FILENO, 0};

FILE *stdin = &g_stdin_file;
FILE *stdout = &g_stdout_file;
FILE *stderr = &g_stderr_file;

FILE *fopen(const char *path, const char *mode) {
    FILE *stream;
    int flags = 0;
    int fd;

    if (path == 0 || mode == 0) {
        return 0;
    }
    if (mode[0] == 'w') {
        flags = O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_CREAT | O_APPEND;
    } else if (mode[0] != 'r') {
        return 0;
    }
    fd = open(path, flags);
    if (fd < 0) {
        return 0;
    }
    stream = malloc(sizeof(*stream));
    if (stream == 0) {
        close(fd);
        return 0;
    }
    stream->fd = fd;
    stream->owned = 1;
    return stream;
}

int fclose(FILE *stream) {
    int result;

    if (stream == 0 || !stream->owned) {
        return EOF;
    }
    result = close(stream->fd);
    free(stream);
    return result;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream) {
    size_t bytes;
    size_t total = 0;

    if (ptr == 0 || stream == 0 || size == 0 || count == 0 || count > (size_t)-1 / size) {
        return 0;
    }
    bytes = size * count;
    while (total < bytes) {
        ssize_t got = read(stream->fd, (uint8_t *)ptr + total, bytes - total);

        if (got <= 0) {
            break;
        }
        total += (size_t)got;
    }
    return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream) {
    size_t bytes;
    ssize_t written;

    if (ptr == 0 || stream == 0 || size == 0 || count == 0 || count > (size_t)-1 / size) {
        return 0;
    }
    bytes = size * count;
    written = write(stream->fd, ptr, bytes);
    return written > 0 ? (size_t)written / size : 0;
}

int fseek(FILE *stream, long offset, int whence) {
    return stream != 0 && lseek(stream->fd, offset, whence) >= 0 ? 0 : -1;
}

long ftell(FILE *stream) {
    return stream != 0 ? lseek(stream->fd, 0, SEEK_CUR) : -1;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

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

int putchar(int ch) {
    char value = (char)ch;

    return write(STDOUT_FILENO, &value, 1u) == 1 ? (unsigned char)value : EOF;
}

int puts(const char *text) {
    size_t len;

    if (text == 0) {
        return EOF;
    }
    len = strlen(text);
    if (write(STDOUT_FILENO, text, len) != (ssize_t)len ||
        write(STDOUT_FILENO, "\n", 1u) != 1) {
        return EOF;
    }
    return 0;
}

int rename(const char *old_path, const char *new_path) {
    char buffer[1024];
    int source;
    int destination;
    int result = -1;

    source = open(old_path, 0);
    if (source < 0) {
        return -1;
    }
    destination = open(new_path, O_CREAT | O_TRUNC);
    if (destination < 0) {
        close(source);
        return -1;
    }
    for (;;) {
        ssize_t got = read(source, buffer, sizeof(buffer));

        if (got < 0) {
            break;
        }
        if (got == 0) {
            result = 0;
            break;
        }
        if (write(destination, buffer, (size_t)got) != got) {
            break;
        }
    }
    close(destination);
    close(source);
    if (result == 0) {
        result = remove(old_path);
    }
    return result;
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
    (void)syscall4(SYS_CLEAR, 0, 0, 0, 0);
}
