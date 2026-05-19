#pragma once

#include "user/apps/elf/nexbox/core/cmdsuite_shared.h"
#include "user/apps/elf/nexbox/applets/fs/cmd_ls_shared.h"

#define CMD_CAT_BUFFER_SIZE 512u

static inline int starts_with_text_local(const char *text, const char *prefix) {
    uint32_t i = 0;

    if (text == NULL || prefix == NULL) {
        return 0;
    }
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static inline int text_contains_local(const char *text, const char *pattern) {
    uint32_t i = 0;

    if (text == NULL || pattern == NULL || pattern[0] == '\0') {
        return 0;
    }
    while (text[i] != '\0') {
        uint32_t j = 0;

        while (pattern[j] != '\0' && text[i + j] != '\0' && text[i + j] == pattern[j]) {
            j++;
        }
        if (pattern[j] == '\0') {
            return 1;
        }
        i++;
    }
    return 0;
}

static inline int parse_sleep_ticks_local(const char *text, uint32_t *ticks_out) {
    char *endptr = 0;
    unsigned long value;

    if (text == 0 || text[0] == '\0' || ticks_out == 0) {
        return 0;
    }
    value = strtoul(text, &endptr, 10);
    if (endptr == text || value > 0xfffffffful) {
        return 0;
    }
    if (*endptr == '\0' || streq_local(endptr, "s")) {
        if (value > 4294967ul) {
            return 0;
        }
        *ticks_out = (uint32_t)(value * 1000ul);
        return 1;
    }
    if (streq_local(endptr, "ms")) {
        *ticks_out = (uint32_t)value;
        return 1;
    }
    if (streq_local(endptr, "tick") || streq_local(endptr, "ticks")) {
        *ticks_out = (uint32_t)(value * 10ul);
        return 1;
    }
    return 0;
}
