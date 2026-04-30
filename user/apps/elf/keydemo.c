#include "user/libc/include/file.h"
#include "user/libc/include/stdio.h"

int main(int argc, char **argv) {
    char ch;

    (void)argc;
    (void)argv;

    printf("ELF keydemo running, press keys, q to quit\n");
    for (;;) {
        if (read_char_nonblock(&ch) == 0) {
            continue;
        }
        printf("key: %c\n", ch);
        if (ch == 'q' || ch == 'Q') {
            break;
        }
    }
    return 0;
}
