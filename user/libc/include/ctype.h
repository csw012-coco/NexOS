#pragma once

static inline int isspace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

static inline int isalpha(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static inline int isdigit(int ch) {
    return ch >= '0' && ch <= '9';
}

static inline int toupper(int ch) {
    return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch;
}

static inline int tolower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
}
