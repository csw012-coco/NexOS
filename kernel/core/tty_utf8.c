#include "kernel/internal/core/tty_internal.h"

uint8_t tty_codepoint_width(uint32_t codepoint) {
    return ((codepoint >= 0x1100u && codepoint <= 0x115fu) ||
            (codepoint >= 0x2e80u && codepoint <= 0xa4cfu) ||
            (codepoint >= 0xac00u && codepoint <= 0xd7a3u) ||
            (codepoint >= 0xf900u && codepoint <= 0xfaffu) ||
            (codepoint >= 0xff00u && codepoint <= 0xff60u)) ? 2u : 1u;
}

uint8_t tty_utf8_encode(uint32_t codepoint, char out[4]) {
    if (codepoint <= 0x7fu) {
        out[0] = (char)codepoint;
        return 1u;
    }
    if (codepoint <= 0x7ffu) {
        out[0] = (char)(0xc0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3fu));
        return 2u;
    }
    out[0] = (char)(0xe0u | (codepoint >> 12));
    out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
    out[2] = (char)(0x80u | (codepoint & 0x3fu));
    return 3u;
}

int tty_utf8_is_continuation(uint8_t ch) {
    return (ch & 0xc0u) == 0x80u;
}

uint8_t tty_utf8_expected_length(uint8_t first) {
    if (first < 0x80u) {
        return 1u;
    }
    if ((first & 0xe0u) == 0xc0u) {
        return 2u;
    }
    if ((first & 0xf0u) == 0xe0u) {
        return 3u;
    }
    if ((first & 0xf8u) == 0xf0u) {
        return 4u;
    }
    return 0u;
}

uint32_t tty_utf8_decode_next(const char *data, uint32_t len, uint32_t offset, uint32_t *codepoint) {
    uint8_t first;
    uint32_t needed = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;

    if (codepoint == 0 || offset >= len) {
        return 0;
    }

    first = (uint8_t)data[offset];
    if (first < 0x80u) {
        *codepoint = first;
        return 1;
    }
    if ((first & 0xe0u) == 0xc0u) {
        needed = 2;
        value = first & 0x1fu;
        minimum = 0x80u;
    } else if ((first & 0xf0u) == 0xe0u) {
        needed = 3;
        value = first & 0x0fu;
        minimum = 0x800u;
    } else if ((first & 0xf8u) == 0xf0u) {
        needed = 4;
        value = first & 0x07u;
        minimum = 0x10000u;
    } else {
        *codepoint = 0xfffdu;
        return 1;
    }

    if (offset + needed > len) {
        *codepoint = 0xfffdu;
        return 1;
    }
    for (uint32_t i = 1; i < needed; i++) {
        uint8_t next = (uint8_t)data[offset + i];

        if (next == 0 || !tty_utf8_is_continuation(next)) {
            *codepoint = 0xfffdu;
            return 1;
        }
        value = (value << 6) | (uint32_t)(next & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) {
        *codepoint = 0xfffdu;
        return 1;
    }

    *codepoint = value;
    return needed;
}

uint8_t tty_utf8_previous_offset(const char *data, uint8_t offset) {
    uint8_t previous;

    if (data == NULL || offset == 0u) {
        return 0u;
    }
    previous = (uint8_t)(offset - 1u);
    while (previous > 0u && tty_utf8_is_continuation((uint8_t)data[previous])) {
        previous--;
    }
    return previous;
}

uint8_t tty_utf8_next_offset(const char *data, uint8_t len, uint8_t offset) {
    uint32_t codepoint;
    uint32_t consumed;

    if (data == NULL || offset >= len) {
        return len;
    }
    consumed = tty_utf8_decode_next(data, len, offset, &codepoint);
    if (consumed == 0u || consumed > (uint32_t)(len - offset)) {
        consumed = 1u;
    }
    return (uint8_t)(offset + consumed);
}
