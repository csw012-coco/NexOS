#include "user/libc/include/fcntl.h"
#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"
#include "user/libc/include/string.h"
#include "user/libc/include/unistd.h"
#include "user/libc/include/nexos/string.h"

int main(int argc, char **argv) {
    char path[32];
    char buf[64];
    int fd;
    uint32_t got;

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
        uint32_t bytes = (uint32_t)read(fd, buf, sizeof(buf));

        if (bytes == 0) {
            break;
        }
        (void)write(STDOUT_FILENO, buf, bytes);
        if (bytes < sizeof(buf)) {
            break;
        }
    }

    close((uint32_t)fd);
    printf("\n");
    return 0;
}
