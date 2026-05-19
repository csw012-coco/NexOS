#include "user/libc/include/fcntl.h"
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/string.h"
#include "user/libc/include/unistd.h"
#include "user/libc/include/nexos/string.h"

#define CAT_BUFFER_SIZE 512u

static int cat_write_all(const char *buf, uint32_t bytes) {
    uint32_t offset = 0;

    while (offset < bytes) {
        ssize_t written = write(STDOUT_FILENO, buf + offset, bytes - offset);

        if (written < 0) {
            return -1;
        }
        if (written == 0) {
            return 0;
        }
        offset += (uint32_t)written;
    }
    return 1;
}

int main(int argc, char **argv) {
    char path[32];
    char buf[CAT_BUFFER_SIZE];
    int fd;
    uint32_t got;
    int output_closed = 0;

    if (argc > 1) {
        strlcpy(path, sizeof(path), argv[1]);
    } else {
        printf("cat ELF: enter filename\n");
        printf("file> ");
        got = read_line(STDIN_FILENO, path, sizeof(path));
        if (got == 0) {
            eprintf("read failed\n");
            return 1;
        }
        trim_line(path);
    }

    fd = open(path, 0);
    if (fd < 0) {
        eprintf("open failed\n");
        return 1;
    }

    for (;;) {
        ssize_t read_rc = read(fd, buf, sizeof(buf));
        uint32_t bytes;
        int write_rc;

        if (read_rc < 0) {
            eprintf("read failed\n");
            close((uint32_t)fd);
            return 1;
        }
        if (read_rc == 0) {
            break;
        }
        bytes = (uint32_t)read_rc;
        write_rc = cat_write_all(buf, bytes);
        if (write_rc < 0) {
            eprintf("write failed\n");
            close((uint32_t)fd);
            return 1;
        }
        if (write_rc == 0) {
            output_closed = 1;
            break;
        }
    }

    close((uint32_t)fd);
    if (!output_closed) {
        printf("\n");
    }
    return 0;
}
