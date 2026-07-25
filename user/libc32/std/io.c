#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE standard_input = {STDIN_FILENO, 0, 0, 0};
static FILE standard_output = {STDOUT_FILENO, 0, 0, 0};
static FILE standard_error = {STDERR_FILENO, 0, 0, 0};

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
    stream->owned = 1;
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

    if (stream == 0 || !stream->owned) {
        return EOF;
    }
    result = close(stream->fd);
    free(stream);
    return result;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int fgetc(FILE *stream) {
    char value;
    ssize_t result;

    if (stream == 0) {
        return EOF;
    }
    result = read(stream->fd, &value, 1u);
    if (result < 0) {
        stream->error = 1;
        return EOF;
    }
    if (result == 0) {
        stream->eof = 1;
        return EOF;
    }
    return (unsigned char)value;
}

char *fgets(char *buffer, int size, FILE *stream) {
    int used = 0;

    if (buffer == 0 || size <= 0 || stream == 0) {
        return 0;
    }
    while (used + 1 < size) {
        int ch = fgetc(stream);

        if (ch == EOF) {
            break;
        }
        buffer[used++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (used == 0) {
        return 0;
    }
    buffer[used] = '\0';
    return buffer;
}

int getchar(void) {
    return fgetc(stdin);
}

int rename(const char *old_path, const char *new_path) {
    char buffer[1024];
    int source;
    int destination;
    int result = -1;

    if (old_path == 0 || new_path == 0) {
        return -1;
    }
    source = open(old_path, O_RDONLY);
    if (source < 0) {
        return -1;
    }
    destination = open(new_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (destination < 0) {
        (void)close(source);
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
    (void)close(destination);
    (void)close(source);
    if (result == 0) {
        result = remove(old_path);
    }
    return result;
}

int fseek(FILE *stream, long offset, int whence) {
    if (stream == 0 || lseek(stream->fd, offset, whence) < 0) {
        if (stream != 0) {
            stream->error = 1;
        }
        return -1;
    }
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    long result;

    if (stream == 0) {
        return -1;
    }
    result = lseek(stream->fd, 0, SEEK_CUR);
    if (result < 0) {
        stream->error = 1;
    }
    return result;
}

int feof(FILE *stream) {
    return stream != 0 ? stream->eof : 0;
}

int ferror(FILE *stream) {
    return stream != 0 ? stream->error : 0;
}

void clearerr(FILE *stream) {
    if (stream != 0) {
        stream->error = 0;
        stream->eof = 0;
    }
}
