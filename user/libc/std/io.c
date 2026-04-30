#include "user/libc/include/nlibc.h"

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
