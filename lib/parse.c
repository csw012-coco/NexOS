#include "lib/parse.h"
#include "lib/string.h"

int parse_u32(const char *text, uint32_t *value) {
    uint32_t base = 10;
    uint32_t result = 0;
    int saw_digit = 0;

    text = skip_spaces(text);
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }

    while (*text != '\0') {
        uint32_t digit;

        if (*text >= '0' && *text <= '9') {
            digit = (uint32_t)(*text - '0');
        } else if (base == 16 && *text >= 'a' && *text <= 'f') {
            digit = 10u + (uint32_t)(*text - 'a');
        } else if (base == 16 && *text >= 'A' && *text <= 'F') {
            digit = 10u + (uint32_t)(*text - 'A');
        } else if (*text == ' ') {
            break;
        } else {
            return 0;
        }

        if (digit >= base) {
            return 0;
        }

        result = result * base + digit;
        saw_digit = 1;
        text++;
    }

    if (!saw_digit) {
        return 0;
    }

    *value = result;
    return 1;
}

int parse_u64(const char *text, uint64_t *value) {
    uint64_t base = 10;
    uint64_t result = 0;
    int saw_digit = 0;

    text = skip_spaces(text);
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }

    while (*text != '\0') {
        uint64_t digit;

        if (*text >= '0' && *text <= '9') {
            digit = (uint64_t)(*text - '0');
        } else if (base == 16 && *text >= 'a' && *text <= 'f') {
            digit = 10u + (uint64_t)(*text - 'a');
        } else if (base == 16 && *text >= 'A' && *text <= 'F') {
            digit = 10u + (uint64_t)(*text - 'A');
        } else if (*text == ' ') {
            break;
        } else {
            return 0;
        }

        if (digit >= base) {
            return 0;
        }

        result = result * base + digit;
        saw_digit = 1;
        text++;
    }

    if (!saw_digit) {
        return 0;
    }

    *value = result;
    return 1;
}

int parse_token(const char *text, char *out, uint32_t max_len) {
    uint32_t pos = 0;

    text = skip_spaces(text);
    if (*text == '\0') {
        return 0;
    }

    while (*text != '\0' && *text != ' ') {
        if (pos + 1 >= max_len) {
            return 0;
        }
        out[pos++] = *text++;
    }
    out[pos] = '\0';
    return 1;
}
