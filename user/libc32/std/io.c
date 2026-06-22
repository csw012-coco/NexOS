#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
