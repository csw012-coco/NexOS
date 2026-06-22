#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE standard_input = {STDIN_FILENO, 0, 0};
static FILE standard_output = {STDOUT_FILENO, 0, 0};
static FILE standard_error = {STDERR_FILENO, 0, 0};

FILE *stdin = &standard_input;
FILE *stdout = &standard_output;
FILE *stderr = &standard_error;

int putchar(int ch) {
    char value = (char)ch;

    return write(STDOUT_FILENO, &value, 1u) == 1 ? (unsigned char)value : EOF;
}

int puts(const char *text) {
    size_t length;

    if (text == 0) {
        return EOF;
    }
    length = strlen(text);
    if (write(STDOUT_FILENO, text, length) != (ssize_t)length ||
        write(STDOUT_FILENO, "\n", 1u) != 1) {
        return EOF;
    }
    return 0;
}

FILE *fopen(const char *path, const char *mode) {
    FILE *stream;
    int flags;
    int fd;

    if (path == 0 || mode == 0) {
        return 0;
    }
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        flags = O_RDONLY;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        return 0;
    }
    fd = open(path, flags);
    if (fd < 0) {
        return 0;
    }
    stream = malloc(sizeof(*stream));
    if (stream == 0) {
        (void)close(fd);
        return 0;
    }
    stream->fd = fd;
    stream->error = 0;
    stream->eof = 0;
    return stream;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    size_t requested;
    size_t total = 0u;

    if (buffer == 0 || stream == 0 || size == 0u || count == 0u ||
        count > (size_t)-1 / size) {
        return 0u;
    }
    requested = size * count;
    while (total < requested) {
        size_t chunk = requested - total;
        ssize_t result = write(stream->fd,
                               (const uint8_t *)buffer + total,
                               chunk > 4096u ? 4096u : chunk);

        if (result <= 0) {
            stream->error = 1;
            break;
        }
        total += (size_t)result;
    }
    return total / size;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    size_t requested;
    size_t total = 0u;

    if (buffer == 0 || stream == 0 || size == 0u || count == 0u ||
        count > (size_t)-1 / size) {
        return 0u;
    }
    requested = size * count;
    while (total < requested) {
        size_t chunk = requested - total;
        ssize_t result = read(stream->fd,
                              (uint8_t *)buffer + total,
                              chunk > 4096u ? 4096u : chunk);

        if (result < 0) {
            stream->error = 1;
            break;
        }
        if (result == 0) {
            stream->eof = 1;
            break;
        }
        total += (size_t)result;
    }
    return total / size;
}

int fclose(FILE *stream) {
    int result;

    if (stream == 0) {
        return EOF;
    }
    result = close(stream->fd);
    free(stream);
    return result;
}
